"""Independent, bounded liveness supervision for caller-owned soak children.

The collector writes one contiguous integer heartbeat per second to a dedicated
pipe and commits a final sample before FINISHED on a second pipe. This supervisor
owns both readers and the pre-opened sample file and timestamps receipts itself;
it must run outside both children. No process attachment or RSS API is used.
Receipts are engineering evidence only. Product progress, physical audio and
installed identity authenticity require their separate collectors and review.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
import re
import stat
import subprocess
import time
from typing import Any, Callable, Protocol


HEARTBEAT_INTERVAL_SECONDS = 1
# One canonical sampling interval permits startup/receipt scheduling lateness.
HEARTBEAT_LATENESS_SECONDS = 1
ENDPOINT_ACK_SECONDS = 1
MAXIMUM_FINISHED_BYTES = 1024
MAXIMUM_FINAL_SAMPLE_BYTES = 4096


class ChildProcess(Protocol):
    pid: int

    def poll(self) -> int | None: ...
    def terminate(self) -> None: ...
    def kill(self) -> None: ...
    def wait(self, timeout: float) -> int: ...


class SupervisionError(RuntimeError):
    def __init__(self, reason: str, receipt: dict[str, Any]):
        super().__init__(reason)
        self.receipt = receipt


def _number(value: Any, low: float, high: float) -> bool:
    return (not isinstance(value, bool) and isinstance(value, (int, float))
            and low <= value <= high and math.isfinite(value))


def _json_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)
            + "\n").encode("utf-8")


def _parse_json(data: bytes) -> Any:
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("FINISHED JSON contains duplicate keys")
            result[key] = value
        return result
    def constant(value):
        raise ValueError("FINISHED JSON must contain finite values")
    return json.loads(data, object_pairs_hook=pairs, parse_constant=constant)


def commit_final_sample(
    final_sample_fd: int,
    finished_fd: int,
    *,
    record_id: str,
    duration_seconds: int,
    heartbeat_sequence: int,
    sample: dict[str, Any],
) -> None:
    """Write a fresh final snapshot, fsync it, then emit one bounded FINISHED frame.

    The caller retains these write descriptors. It must emit the final heartbeat
    only after this commit succeeds and keep both children alive until cleanup.
    FINISHED may be read first; the supervisor joins its sequence within the deadline. Failure
    preserves partial bytes and emits no successful acknowledgement.
    """
    if (not isinstance(record_id, str) or not record_id.strip()
            or type(duration_seconds) is not int or duration_seconds not in (1800, 7200)
            or type(heartbeat_sequence) is not int or not 1 <= heartbeat_sequence <= duration_seconds + 2
            or not isinstance(sample, dict)
            or not _number(sample.get("elapsedSeconds"), duration_seconds,
                           duration_seconds + ENDPOINT_ACK_SECONDS)):
        raise ValueError("final sample requires session, duration, sequence and endpoint coverage")
    payload = _json_bytes({
        "schemaVersion": 1, "recordType": "external-beta-final-soak-sample",
        "recordId": record_id, "durationSeconds": duration_seconds,
        "heartbeatSequence": heartbeat_sequence, "sample": sample,
    })
    frame = _json_bytes({
        "type": "FINISHED", "recordId": record_id, "durationSeconds": duration_seconds,
        "heartbeatSequence": heartbeat_sequence, "sampleBytes": len(payload),
        "sampleSha256": hashlib.sha256(payload).hexdigest(),
    })
    if len(payload) > MAXIMUM_FINAL_SAMPLE_BYTES or len(frame) > MAXIMUM_FINISHED_BYTES:
        raise ValueError("final sample or FINISHED frame exceeds its byte limit")
    info = os.fstat(final_sample_fd)
    if not stat.S_ISREG(info.st_mode) or info.st_size != 0:
        raise ValueError("final sample must use a fresh empty regular file")
    os.lseek(final_sample_fd, 0, os.SEEK_SET)
    offset = 0
    while offset < len(payload):
        written = os.write(final_sample_fd, payload[offset:])
        if written <= 0:
            raise OSError("final sample write made no progress")
        offset += written
    if os.fstat(final_sample_fd).st_size != len(payload):
        raise ValueError("final sample size changed during commit")
    os.fsync(final_sample_fd)
    os.set_blocking(finished_fd, False)
    if os.write(finished_fd, frame) != len(frame):
        raise OSError("FINISHED frame was not written completely")


def _verify_finished(frame: dict[str, Any], final_sample_fd: int,
                     record_id: str, duration_seconds: int) -> dict[str, Any]:
    fields = {"type", "recordId", "durationSeconds", "heartbeatSequence", "sampleBytes", "sampleSha256"}
    if not isinstance(frame, dict) or set(frame) != fields or frame.get("type") != "FINISHED":
        raise ValueError("FINISHED frame has the wrong shape")
    if (frame["recordId"] != record_id or type(frame["durationSeconds"]) is not int
            or frame["durationSeconds"] != duration_seconds):
        raise ValueError("FINISHED session or duration does not match")
    if (type(frame["heartbeatSequence"]) is not int
            or not 1 <= frame["heartbeatSequence"] <= duration_seconds + 2):
        raise ValueError("FINISHED heartbeat sequence is invalid")
    if (type(frame["sampleBytes"]) is not int or not 1 <= frame["sampleBytes"] <= MAXIMUM_FINAL_SAMPLE_BYTES
            or not isinstance(frame["sampleSha256"], str)
            or re.fullmatch(r"[0-9a-f]{64}", frame["sampleSha256"]) is None):
        raise ValueError("FINISHED sample byte identity is invalid")
    before = os.fstat(final_sample_fd)
    if not stat.S_ISREG(before.st_mode) or before.st_size != frame["sampleBytes"]:
        raise ValueError("FINISHED final sample was not committed with the declared size")
    data = os.pread(final_sample_fd, MAXIMUM_FINAL_SAMPLE_BYTES + 1, 0)
    after = os.fstat(final_sample_fd)
    unchanged = all(getattr(before, field) == getattr(after, field)
                    for field in ("st_dev", "st_ino", "st_size", "st_mtime_ns", "st_ctime_ns"))
    if (not unchanged or len(data) != frame["sampleBytes"]
            or hashlib.sha256(data).hexdigest() != frame["sampleSha256"]):
        raise ValueError("FINISHED final sample bytes changed or do not match")
    snapshot = _parse_json(data)
    if (not isinstance(snapshot, dict)
            or set(snapshot) != {"schemaVersion", "recordType", "recordId", "durationSeconds", "heartbeatSequence", "sample"}
            or type(snapshot["schemaVersion"]) is not int or snapshot["schemaVersion"] != 1
            or snapshot["recordType"] != "external-beta-final-soak-sample"
            or snapshot["recordId"] != record_id
            or type(snapshot["durationSeconds"]) is not int or snapshot["durationSeconds"] != duration_seconds
            or type(snapshot["heartbeatSequence"]) is not int
            or snapshot["heartbeatSequence"] != frame["heartbeatSequence"]
            or not isinstance(snapshot["sample"], dict)
            or not _number(snapshot["sample"].get("elapsedSeconds"), duration_seconds,
                           duration_seconds + ENDPOINT_ACK_SECONDS)):
        raise ValueError("FINISHED final sample lacks matching session, sequence or duration coverage")
    return snapshot


def _stop_children(children: tuple[ChildProcess, ...]) -> tuple[list[str], BaseException | None]:
    errors = []
    interruption = None

    def remember(child: ChildProcess, stage: str, exc: BaseException) -> None:
        nonlocal interruption
        errors.append(f"process {child.pid} {stage} failed: {type(exc).__name__}: {exc}")
        if not isinstance(exc, Exception) and interruption is None:
            interruption = exc

    for child in children:
        try:
            if child.poll() is None:
                child.terminate()
        except PermissionError as exc:
            errors.append(f"process {child.pid} cleanup permission denied: {exc}")
            continue
        except BaseException as exc:
            remember(child, "termination", exc)
        needs_kill = False
        try:
            child.wait(timeout=2)
        except subprocess.TimeoutExpired:
            needs_kill = True
        except PermissionError as exc:
            errors.append(f"process {child.pid} cleanup permission denied: {exc}")
            continue
        except BaseException as exc:
            remember(child, "reap", exc)
            needs_kill = not isinstance(exc, Exception)
        if needs_kill:
            try:
                child.kill()
            except PermissionError as exc:
                errors.append(f"process {child.pid} cleanup permission denied: {exc}")
                continue
            except BaseException as exc:
                remember(child, "kill", exc)
            try:
                child.wait(timeout=2)
            except BaseException as exc:
                remember(child, "reap after kill", exc)
    return errors, interruption


def supervise_soak(
    product: ChildProcess,
    collector: ChildProcess,
    heartbeat_fd: int,
    *,
    record_id: str,
    installed_tree_sha256: str,
    workload_sha256: str,
    duration_seconds: int,
    finished_fd: int | None = None,
    final_sample_fd: int | None = None,
    poll_interval_seconds: float = 1,
    max_gap_seconds: float = 5,
    clock: Callable[[], float] = time.monotonic,
    sleep: Callable[[float], None] = time.sleep,
) -> dict[str, Any]:
    """Observe through a committed endpoint, stop/reap children and close readers.

    Input errors leave child/descriptor ownership with the caller. Once observation
    starts, ownership transfers here even on failure. Clock/sleep injection is for
    controlled protocol tests; those receipts cannot qualify a physical soak.
    FINISHED and final-sample descriptors are mandatory; omitted ones fail closed.
    """
    if type(duration_seconds) is not int or duration_seconds not in (1800, 7200):
        raise ValueError("duration must be exactly 1800 or 7200 integer seconds")
    if not _number(poll_interval_seconds, .1, 1) or not _number(max_gap_seconds, 1, 5):
        raise ValueError("poll interval must be 0.1-1 second and maximum gap 1-5 seconds")
    if not isinstance(record_id, str) or not record_id.strip():
        raise ValueError("record identity is required")
    for digest in (installed_tree_sha256, workload_sha256):
        if not isinstance(digest, str) or re.fullmatch(r"[0-9a-fA-F]{64}", digest) is None:
            raise ValueError("installed tree and workload SHA-256 identities are required")
    supervisor_pid = os.getpid()
    pids = (product.pid, collector.pid, supervisor_pid)
    if any(type(pid) is not int or pid <= 0 for pid in pids) or len(set(pids)) != 3:
        raise ValueError("product, collector and supervisor must be distinct live process identities")
    if type(heartbeat_fd) is not int or heartbeat_fd < 0:
        raise ValueError("a dedicated heartbeat read descriptor is required")
    if (type(finished_fd) is not int or finished_fd < 0
            or type(final_sample_fd) is not int or final_sample_fd < 0
            or len({heartbeat_fd, finished_fd, final_sample_fd}) != 3):
        raise ValueError("dedicated FINISHED reader and final-sample descriptor are required")
    sample_info = os.fstat(final_sample_fd)
    if not stat.S_ISREG(sample_info.st_mode) or sample_info.st_size != 0:
        raise ValueError("final sample must start as an empty regular file")
    if not stat.S_ISFIFO(os.fstat(finished_fd).st_mode):
        raise ValueError("FINISHED reader must be a dedicated pipe")
    receipt: dict[str, Any] = {
        "schemaVersion": 2, "recordType": "external-beta-soak-supervision",
        "evidenceScope": "engineering", "releaseEligible": False, "status": "RUNNING",
        "recordId": record_id, "installedTreeSha256": installed_tree_sha256,
        "workloadSha256": workload_sha256, "requiredSeconds": duration_seconds,
        "supervisorPid": supervisor_pid, "productPid": product.pid, "collectorPid": collector.pid,
        "clockAuthority": "supervisor-monotonic", "pollIntervalSeconds": poll_interval_seconds,
        "maxGapSeconds": max_gap_seconds, "observations": [], "cleanupErrors": [],
        "heartbeatIntervalSeconds": HEARTBEAT_INTERVAL_SECONDS,
        "heartbeatLatenessSeconds": HEARTBEAT_LATENESS_SECONDS,
        "endpointProtocol": "durable-final-sample-v1", "endpointAckSeconds": ENDPOINT_ACK_SECONDS,
        "finishedAcknowledgement": None,
    }
    failure = None
    cause = None
    interruption = None
    try:
        os.set_blocking(heartbeat_fd, False)
        os.set_blocking(finished_fd, False)
        started = clock()
        if not _number(started, 0, float("inf")):
            raise ValueError("supervisor clock must be finite and nonnegative")
        previous_elapsed = None
        next_poll = None
        last_heartbeat = 0.0
        sequence = 0
        pending = b""
        finished_pending = b""
        finished = None
        endpoint_deadline = duration_seconds + ENDPOINT_ACK_SECONDS
        maximum_observations = math.ceil(endpoint_deadline / poll_interval_seconds) + 2
        while True:
            if len(receipt["observations"]) >= maximum_observations:
                raise ValueError("supervisor observation count exceeds its bounded cadence")
            now = clock()
            if not _number(now, started, float("inf")):
                raise ValueError("supervisor clock must advance monotonically")
            elapsed = now - started
            if previous_elapsed is None and elapsed > poll_interval_seconds:
                raise ValueError("supervisor initial observation missed its declared cadence")
            if previous_elapsed is not None:
                if elapsed <= previous_elapsed:
                    raise ValueError("supervisor clock must advance monotonically")
                if elapsed + 1e-6 < next_poll:
                    raise ValueError("supervisor poll completed before its declared cadence")
                if elapsed - previous_elapsed > max_gap_seconds:
                    raise ValueError("supervisor observation gap exceeds its declared limit")
            if elapsed > endpoint_deadline:
                raise ValueError("FINISHED acknowledgement missed its bounded endpoint deadline")
            for label, child in (("product", product), ("collector", collector)):
                code = child.poll()
                if code is not None:
                    raise ValueError(f"{label} exited before supervision completed (exit {code})")
            try:
                chunk = os.read(heartbeat_fd, 4096)
            except BlockingIOError:
                chunk = None
            if chunk == b"":
                raise ValueError("heartbeat pipe closed before supervision completed")
            if chunk is not None:
                pending += chunk
                lines = pending.split(b"\n")
                pending = lines.pop()
                for line in lines:
                    if not line.isdigit() or len(line) > 20:
                        raise ValueError("heartbeat must be an unsigned integer sequence")
                    observed = int(line)
                    if observed != sequence + 1 or observed > 2**63 - 1:
                        raise ValueError("heartbeat sequence must start at one and advance without gaps or replay")
                    if observed > int(elapsed) + 2:
                        raise ValueError("heartbeat sequence advanced faster than its one-second cadence")
                    sequence = observed
                    last_heartbeat = elapsed
                if len(pending) > 20:
                    raise ValueError("heartbeat frame exceeds its bounded size")
            if elapsed - last_heartbeat > max_gap_seconds:
                raise ValueError("heartbeat unavailable or stale")
            minimum_sequence = max(0, int(elapsed / HEARTBEAT_INTERVAL_SECONDS)
                                   + 1 - HEARTBEAT_LATENESS_SECONDS)
            if sequence < minimum_sequence:
                raise ValueError("heartbeat unavailable or stale: sequence behind its one-second cadence")
            try:
                chunk = os.read(finished_fd, MAXIMUM_FINISHED_BYTES + 1)
            except BlockingIOError:
                chunk = None
            if chunk == b"":
                raise ValueError("FINISHED pipe closed before completion")
            if chunk is not None:
                if finished is not None:
                    raise ValueError("FINISHED requires exactly one frame")
                else:
                    finished_pending += chunk
                    if len(finished_pending) > MAXIMUM_FINISHED_BYTES:
                        raise ValueError("FINISHED frame exceeds its byte limit")
                    if b"\n" in finished_pending:
                        line, trailing = finished_pending.split(b"\n", 1)
                        if trailing:
                            raise ValueError("FINISHED requires exactly one frame")
                        if elapsed < duration_seconds:
                            raise ValueError("FINISHED arrived before the declared endpoint")
                        finished = _parse_json(line)
                        snapshot = _verify_finished(finished, final_sample_fd, record_id, duration_seconds)
                        receipt["finishedAcknowledgement"] = {
                            "frame": finished, "receivedElapsedSeconds": elapsed, "finalSample": snapshot,
                        }
            if finished is not None and finished["heartbeatSequence"] < sequence:
                raise ValueError("FINISHED sequence differs from the final heartbeat")
            receipt["observations"].append({
                "elapsedSeconds": elapsed, "heartbeatSequence": sequence,
                "heartbeatAgeSeconds": elapsed - last_heartbeat,
            })
            if elapsed >= duration_seconds:
                if sequence == 0 or pending:
                    raise ValueError("heartbeat unavailable or incomplete at the declared endpoint")
                if finished is not None and finished["heartbeatSequence"] == sequence:
                    _verify_finished(finished, final_sample_fd, record_id, duration_seconds)
                    receipt["status"] = "COMPLETE"
                    break
                if elapsed >= endpoint_deadline:
                    raise ValueError("FINISHED acknowledgement unavailable or incomplete at declared endpoint")
            previous_elapsed = elapsed
            next_poll = min(elapsed + poll_interval_seconds,
                            duration_seconds if elapsed < duration_seconds else endpoint_deadline)
            sleep(next_poll - elapsed)
    except BaseException as exc:
        failure, cause = str(exc) or type(exc).__name__, exc
        if not isinstance(exc, Exception):
            interruption = exc
    finally:
        try:
            for child in (product, collector):
                try:
                    errors, child_interruption = _stop_children((child,))
                    receipt["cleanupErrors"].extend(errors)
                    if interruption is None:
                        interruption = child_interruption
                except BaseException as exc:
                    receipt["cleanupErrors"].append(f"process {child.pid} cleanup failed: {exc}")
                    if not isinstance(exc, Exception) and interruption is None:
                        interruption = exc
        finally:
            for label, descriptor in (("heartbeat reader", heartbeat_fd),
                                      ("FINISHED reader", finished_fd), ("final sample", final_sample_fd)):
                try:
                    os.close(descriptor)
                except BaseException as exc:
                    receipt["cleanupErrors"].append(f"{label} close failed: {exc}")
                    if not isinstance(exc, Exception) and interruption is None:
                        interruption = exc
    if receipt["cleanupErrors"]:
        failure = (failure + "; " if failure else "") + "supervisor cleanup failed"
    if failure is not None:
        receipt["status"], receipt["failure"] = "FAILED", failure
        if interruption is not None:
            interruption.receipt = receipt
            raise interruption
        raise SupervisionError(failure, receipt) from cause
    return receipt
