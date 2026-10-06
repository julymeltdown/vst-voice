"""Owned engineering sampling sessions; no physical or release authority.

The private worker performs OS sampling only when explicitly launched by the
adapter. Tests replace measurements/launches; this module has no RSS fallback.
"""
from __future__ import annotations

import hashlib
import math
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import time
from typing import Any, Callable
import uuid

from .soak_collector import collect_soak_samples
from .soak_supervisor import _parse_json, _stop_children, commit_final_sample, supervise_soak


MAX_FRAME = 1024
MAX_ROW = 4096
MAX_METADATA = 4096
MAX_RECEIPT = 16 * 1024 * 1024
STARTUP_SECONDS = 5
SUPERVISOR_POLL_SECONDS = .1


def _encode(value: Any, limit: int) -> bytes:
    import json
    data = (json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode()
    if len(data) > limit:
        raise ValueError("session data exceeds its byte limit")
    return data


def _write_all(fd: int, data: bytes) -> None:
    offset = 0
    while offset < len(data):
        count = os.write(fd, data[offset:])
        if count <= 0:
            raise OSError("session write made no progress")
        offset += count


def _write_new(path: Path, value: Any, limit: int = MAX_METADATA) -> bytes:
    data = _encode(value, limit)
    with path.open("xb") as stream:
        _write_all(stream.fileno(), data)
        os.fsync(stream.fileno())
    return data


def _send(fd: int, value: Any) -> None:
    data = _encode(value, MAX_FRAME)
    os.set_blocking(fd, False)
    if os.write(fd, data) != len(data):
        raise OSError("session control frame was incomplete")


def _time(clock: Callable[[], float]) -> float:
    now = clock()
    if isinstance(now, bool) or not isinstance(now, (int, float)) or not math.isfinite(now) or now < 0:
        raise ValueError("session clock must be finite and nonnegative")
    return now


def _receive(fd: int, deadline: float, clock, sleep) -> dict[str, Any]:
    os.set_blocking(fd, False)
    pending = b""
    previous = _time(clock)
    while True:
        now = _time(clock)
        if now < previous or now >= deadline:
            raise ValueError("session startup clock reversed or deadline expired")
        previous = now
        try:
            chunk = os.read(fd, MAX_FRAME + 1)
        except BlockingIOError:
            chunk = None
        if chunk == b"":
            raise ValueError("session control pipe closed")
        if chunk is not None:
            pending += chunk
            if len(pending) > MAX_FRAME:
                raise ValueError("session control frame exceeds its byte limit")
            if b"\n" in pending:
                line, trailing = pending.split(b"\n", 1)
                value = _parse_json(line)
                if trailing or not isinstance(value, dict):
                    raise ValueError("session control requires one object frame")
                return value
        sleep(min(.01, deadline - now))


def _read_regular(path: Path, limit: int) -> bytes:
    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | getattr(os, "O_NOFOLLOW", 0))
    with os.fdopen(fd, "rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_size > limit:
            raise ValueError("session artifact is not a bounded regular file")
        parts, size = [], 0
        while chunk := stream.read(min(1024 * 1024, limit + 1 - size)):
            parts.append(chunk)
            size += len(chunk)
            if size > limit:
                raise ValueError("session artifact exceeds its byte limit")
        after = os.fstat(stream.fileno())
        if any(getattr(before, key) != getattr(after, key) for key in
               ("st_dev", "st_ino", "st_size", "st_mtime_ns", "st_ctime_ns")):
            raise ValueError("session artifact changed while reading")
        return b"".join(parts)


def _manifest_fields(value: dict[str, Any]) -> None:
    if type(value.get("durationSeconds")) is not int or value["durationSeconds"] not in (1800, 7200):
        raise ValueError("session duration must be exactly 1800 or 7200")
    for key in ("installedTreeSha256", "workloadSha256", "machineProfileSha256"):
        if not isinstance(value.get(key), str) or re.fullmatch(r"[0-9a-f]{64}", value[key]) is None:
            raise ValueError(f"session {key} must be a lowercase SHA-256 digest")
    if not isinstance(value.get("machineProfileId"), str) or not value["machineProfileId"].strip():
        raise ValueError("session machine profile identity is required")


def _load_manifest(directory: Path, digest: str) -> dict[str, Any]:
    data = _read_regular(directory / "session.json", MAX_METADATA)
    if hashlib.sha256(data).hexdigest() != digest:
        raise ValueError("session manifest bytes do not match")
    value = _parse_json(data)
    if not isinstance(value, dict):
        raise ValueError("session manifest must be an object")
    _manifest_fields(value)
    if (not isinstance(value.get("sessionId"), str) or not value["sessionId"]
            or type(value.get("schemaVersion")) is not int or value["schemaVersion"] != 1
            or value.get("recordType") != "external-beta-engineering-soak-session"
            or value.get("evidenceScope") != "engineering" or value.get("releaseEligible") is not False):
        raise ValueError("session manifest has invalid identity or authority")
    return value


class _SampleWriter:
    def __init__(self, directory, manifest, digest, product_pid, collector_pid,
                 raw_fd, final_fd, heartbeat_fd, finished_fd, epoch, clock):
        self.directory, self.manifest, self.digest = directory, manifest, digest
        self.product_pid, self.collector_pid = product_pid, collector_pid
        self.raw_fd, self.final_fd = raw_fd, final_fd
        self.heartbeat_fd, self.finished_fd = heartbeat_fd, finished_fd
        self.epoch, self.clock, self.sequence, self.complete = epoch, clock, 0, False

    def __call__(self, sample):
        duration = self.manifest["durationSeconds"]
        target = self.sequence
        elapsed = sample.get("elapsedSeconds")
        deadline = self.epoch + target + 1
        if (self.complete or target > duration or isinstance(elapsed, bool)
                or not isinstance(elapsed, (int, float)) or not math.isfinite(elapsed)
                or not target <= elapsed < target + 1 or _time(self.clock) >= deadline):
            raise ValueError("sample missed its absolute target or sequence")
        sequence = target + 1
        row = {"sessionId": self.manifest["sessionId"], "manifestSha256": self.digest,
               "productPid": self.product_pid, "sequence": sequence,
               "targetElapsedSeconds": target, "sample": sample}
        _write_all(self.raw_fd, _encode(row, MAX_ROW))
        os.fsync(self.raw_fd)
        if _time(self.clock) >= deadline:
            raise ValueError("raw sample fsync exceeded its absolute deadline")
        if target == duration:
            commit_final_sample(self.final_fd, self.finished_fd,
                                record_id=self.manifest["sessionId"], duration_seconds=duration,
                                heartbeat_sequence=sequence, sample=sample)
            _write_new(self.directory / "worker-result.json", {
                "status": "FINISHED", "sessionId": self.manifest["sessionId"],
                "manifestSha256": self.digest, "sampleCount": sequence,
                "productPid": self.product_pid, "collectorPid": self.collector_pid,
            })
        if _time(self.clock) >= deadline:
            raise ValueError("final sample commit exceeded its absolute deadline")
        os.set_blocking(self.heartbeat_fd, False)
        frame = f"{sequence}\n".encode()
        if os.write(self.heartbeat_fd, frame) != len(frame):
            raise OSError("sample heartbeat was incomplete")
        self.sequence, self.complete = sequence, target == duration


def _run_worker(directory: Path, digest: str, product_pid: int,
                raw_fd: int, final_fd: int, heartbeat_fd: int, finished_fd: int,
                ready_fd: int, go_fd: int, *, clock=time.monotonic, sleep=time.sleep,
                collect=collect_soak_samples) -> None:
    try:
        manifest = _load_manifest(directory, digest)
        binding = {"sessionId": manifest["sessionId"], "manifestSha256": digest, "productPid": product_pid}
        _send(ready_fd, {"type": "READY", **binding})
        go = _receive(go_fd, _time(clock) + STARTUP_SECONDS, clock, sleep)
        if (set(go) != {"type", "startMonotonic", *binding} or go.get("type") != "GO"
                or any(go.get(key) != value for key, value in binding.items())):
            raise ValueError("GO session binding does not match")
        epoch = _time(lambda: go["startMonotonic"])
        writer = _SampleWriter(directory, manifest, digest, product_pid, os.getpid(),
                               raw_fd, final_fd, heartbeat_fd, finished_fd, epoch, clock)
        collect(product_pid, manifest["durationSeconds"], 1, on_sample=writer,
                start_monotonic=epoch, maximum_lateness_seconds=1, clock=clock, sleep=sleep)
        if not writer.complete:
            raise ValueError("worker returned without a committed final sample")
        lease = epoch + manifest["durationSeconds"] + 1 + 8
        while _time(clock) < lease:
            sleep(min(.05, lease - _time(clock)))
        raise ValueError("worker cleanup lease expired")
    except BaseException as exc:
        # Failed measurement has no fallback, timer heartbeat or successful result.
        try:
            _write_new(directory / "worker-error.json", {"error": f"{type(exc).__name__}: {exc}"[:2048]})
        except Exception:
            pass  # original failure wins; a denied persistence action is not retried
        raise


def _join_packet(directory, manifest, digest, receipt, product_pid, collector_pid):
    if (receipt.get("status") != "COMPLETE" or receipt.get("cleanupErrors")
            or receipt.get("evidenceScope") != "engineering" or receipt.get("releaseEligible") is not False
            or receipt.get("recordId") != manifest["sessionId"]
            or receipt.get("requiredSeconds") != manifest["durationSeconds"]
            or receipt.get("installedTreeSha256") != manifest["installedTreeSha256"]
            or receipt.get("workloadSha256") != manifest["workloadSha256"]
            or receipt.get("productPid") != product_pid or receipt.get("collectorPid") != collector_pid):
        raise ValueError("supervision and session binding do not match")
    _load_manifest(directory, digest)
    if (directory / "worker-error.json").exists():
        raise ValueError("worker retained a failure")
    worker = _parse_json(_read_regular(directory / "worker-result.json", MAX_METADATA))
    expected_worker = {"status": "FINISHED", "sessionId": manifest["sessionId"], "manifestSha256": digest,
                       "sampleCount": manifest["durationSeconds"] + 1,
                       "productPid": product_pid, "collectorPid": collector_pid}
    if worker != expected_worker:
        raise ValueError("worker result and session binding do not match")
    rows = _read_regular(directory / "samples.jsonl", (manifest["durationSeconds"] + 1) * MAX_ROW).splitlines()
    if len(rows) != manifest["durationSeconds"] + 1:
        raise ValueError("raw sample count does not cover the session")
    last = None
    for index, line in enumerate(rows):
        if len(line) + 1 > MAX_ROW:
            raise ValueError("raw sample frame exceeds its byte limit")
        row = _parse_json(line)
        if (not isinstance(row, dict) or set(row) != {"sessionId", "manifestSha256", "productPid", "sequence", "targetElapsedSeconds", "sample"}
                or row["sessionId"] != manifest["sessionId"] or row["manifestSha256"] != digest
                or row["productPid"] != product_pid or type(row["sequence"]) is not int or row["sequence"] != index + 1
                or type(row["targetElapsedSeconds"]) is not int or row["targetElapsedSeconds"] != index
                or not isinstance(row["sample"], dict)):
            raise ValueError("raw sample session or sequence binding does not match")
        elapsed = row["sample"].get("elapsedSeconds")
        if isinstance(elapsed, bool) or not isinstance(elapsed, (int, float)) or not index <= elapsed < index + 1:
            raise ValueError("raw sample absolute timing does not match")
        last = row
    snapshot_bytes = _read_regular(directory / "final-sample.json", MAX_ROW)
    snapshot = _parse_json(snapshot_bytes)
    acknowledgement = receipt.get("finishedAcknowledgement")
    if (not isinstance(acknowledgement, dict) or acknowledgement.get("finalSample") != snapshot
            or snapshot.get("sample") != last["sample"] or snapshot.get("heartbeatSequence") != last["sequence"]
            or acknowledgement.get("frame", {}).get("sampleSha256") != hashlib.sha256(snapshot_bytes).hexdigest()
            or acknowledgement.get("frame", {}).get("sampleBytes") != len(snapshot_bytes)):
        raise ValueError("acknowledged final sample differs from persisted raw sample")


def run_engineering_soak_session(product_argv, identity_manifest, output_dir: Path, *,
                                 _launch=subprocess.Popen, _clock=time.monotonic, _sleep=time.sleep,
                                 _supervise=supervise_soak) -> dict[str, Any]:
    """Own two children and retain an engineering packet; private injections are for tests."""
    if (not isinstance(product_argv, (tuple, list)) or not product_argv
            or any(not isinstance(arg, str) or not arg for arg in product_argv)):
        raise ValueError("product command must be a nonempty argument list")
    if not isinstance(identity_manifest, dict) or set(identity_manifest) & {
        "sessionId", "recordType", "schemaVersion", "evidenceScope", "releaseEligible", "startMonotonic",
    }:
        raise ValueError("identity manifest must not supply session authority fields")
    _manifest_fields(identity_manifest)
    manifest = {**identity_manifest, "sessionId": str(uuid.uuid4()), "schemaVersion": 1,
                "recordType": "external-beta-engineering-soak-session",
                "evidenceScope": "engineering", "releaseEligible": False}
    directory = Path(output_dir)
    directory.mkdir(exist_ok=False)
    owned, children, receipt, cause, cleanup = {}, [], None, None, []
    transferred = False
    digest = hashlib.sha256(_encode(manifest, MAX_METADATA)).hexdigest()
    result = {"status": "FAILED", "evidenceScope": "engineering", "releaseEligible": False,
              "sessionId": manifest["sessionId"], "manifestSha256": digest,
              "durationSeconds": manifest["durationSeconds"]}
    def remember(exc):
        nonlocal cause
        if cause is None or (not isinstance(exc, Exception) and isinstance(cause, Exception)):
            cause = exc
    def close(fd):
        label = owned.pop(fd)
        try:
            os.close(fd)  # never retry a denied close through another route
        except BaseException as exc:
            cleanup.append(f"{label}: {type(exc).__name__}: {exc}"[:2048])
            remember(exc)
            raise
    def pipe(label):
        pair = os.pipe()
        owned.update({fd: label for fd in pair})
        return pair
    try:
        _write_new(directory / "session.json", manifest)
        raw = os.open(directory / "samples.jsonl", os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        owned[raw] = "raw samples"
        final = os.open(directory / "final-sample.json", os.O_CREAT | os.O_EXCL | os.O_RDWR, 0o600)
        owned[final] = "final sample reader"
        final_writer = os.dup(final)
        owned[final_writer] = "final sample writer"
        heartbeat_read, heartbeat_write = pipe("heartbeat")
        finished_read, finished_write = pipe("FINISHED")
        ready_read, ready_write = pipe("READY")
        go_read, go_write = pipe("GO")
        children.append(_launch(list(product_argv), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL, close_fds=True))
        pass_fds = (raw, final_writer, heartbeat_write, finished_write, ready_write, go_read)
        command = [sys.executable, "-m", "tools.external_beta.soak_session", "--worker",
                   str(directory.resolve()), digest, str(children[0].pid), *(str(fd) for fd in pass_fds)]
        children.append(_launch(command, pass_fds=pass_fds, close_fds=True,
                                cwd=str(Path(__file__).resolve().parents[2]),
                                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        for fd in pass_fds:
            close(fd)
        binding = {"sessionId": manifest["sessionId"], "manifestSha256": digest, "productPid": children[0].pid}
        ready = _receive(ready_read, _time(_clock) + STARTUP_SECONDS, _clock, _sleep)
        if ready != {"type": "READY", **binding}:
            raise ValueError("READY session binding does not match")
        close(ready_read)
        readers = (heartbeat_read, finished_read, final)
        def supervisor_clock():
            nonlocal transferred
            now = _time(_clock)
            if not transferred:
                transferred = True  # supervisor has entered observation ownership
                for fd in readers:
                    owned.pop(fd)
                _send(go_write, {"type": "GO", **binding, "startMonotonic": now})
                close(go_write)
            return now
        receipt = _supervise(*children, heartbeat_read, record_id=manifest["sessionId"],
                             installed_tree_sha256=manifest["installedTreeSha256"],
                             workload_sha256=manifest["workloadSha256"],
                             duration_seconds=manifest["durationSeconds"], finished_fd=finished_read,
                             final_sample_fd=final, poll_interval_seconds=SUPERVISOR_POLL_SECONDS,
                             clock=supervisor_clock, sleep=_sleep)
    except BaseException as exc:
        receipt = getattr(exc, "receipt", receipt)
        remember(exc)
    finally:
        if receipt is not None:
            transferred = True  # exceptions with receipts also follow supervisor cleanup
            for fd in (locals().get("heartbeat_read"), locals().get("finished_read"), locals().get("final")):
                owned.pop(fd, None)
        if not transferred:
            errors, interruption = _stop_children(tuple(children))
            cleanup.extend(errors)
            if interruption is not None:
                remember(interruption)
        for fd, label in list(owned.items()):
            try:
                close(fd)
            except BaseException as exc:
                remember(exc)
    # No packet hashing occurs while a child still owns its writer.
    try:
        if receipt is not None:
            _write_new(directory / "supervision.json", receipt, MAX_RECEIPT)
        if cleanup or (receipt is not None and receipt.get("cleanupErrors")):
            raise ValueError("session cleanup failed; packet cannot be sealed")
        if cause is None:
            _join_packet(directory, manifest, digest, receipt, children[0].pid, children[1].pid)
            result["status"] = "COMPLETE"
    except BaseException as exc:
        remember(exc)
    if cause is not None:
        result["status"], result["error"] = "FAILED", f"{type(cause).__name__}: {cause}"[:2048]
    result["cleanupErrors"] = cleanup
    try:
        if not isinstance(cause, PermissionError):
            if cause is not None:
                _write_new(directory / "session-error.json", result)
            if not cleanup and not (receipt and receipt.get("cleanupErrors")):
                references = {}
                limits = {"session.json": MAX_METADATA, "samples.jsonl": (manifest["durationSeconds"] + 1) * MAX_ROW,
                          "final-sample.json": MAX_ROW, "worker-result.json": MAX_METADATA,
                          "worker-error.json": MAX_METADATA, "supervision.json": MAX_RECEIPT,
                          "session-error.json": MAX_METADATA}
                for name, limit in limits.items():
                    if (directory / name).exists():
                        data = _read_regular(directory / name, limit)
                        references[name] = {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}
                data = _write_new(directory / "packet-index.pending.json", {**result, "files": references}, 65536)
                os.link(directory / "packet-index.pending.json", directory / "packet-index.json")
                result["packetIndexSha256"] = hashlib.sha256(data).hexdigest()
    except BaseException as exc:
        remember(exc)
        result["status"], result["error"] = "FAILED", f"{type(cause).__name__}: {cause}"[:2048]
    if cause is not None:
        cause.session_result = result
        raise cause
    return result


if __name__ == "__main__":
    args = sys.argv[1:]
    if len(args) != 10 or args[0] != "--worker":
        raise SystemExit("Private session worker requires its exact descriptor arguments")
    _run_worker(Path(args[1]), args[2], *(int(value) for value in args[3:]))
