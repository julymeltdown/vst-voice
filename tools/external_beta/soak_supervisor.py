"""Independent, bounded liveness supervision for caller-owned soak children.

The collector writes one contiguous integer heartbeat per second to a dedicated
pipe. This supervisor owns the read descriptor and timestamps receipts itself;
it must run outside both children. No process attachment or RSS API is used.
Receipts are engineering evidence only. Product progress, physical audio and
installed identity authenticity require their separate collectors and review.
"""
from __future__ import annotations

import math
import os
import re
import subprocess
import time
from typing import Any, Callable, Protocol


HEARTBEAT_INTERVAL_SECONDS = 1
# One canonical sampling interval permits startup/receipt scheduling lateness.
HEARTBEAT_LATENESS_SECONDS = 1


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
    poll_interval_seconds: float = 1,
    max_gap_seconds: float = 5,
    clock: Callable[[], float] = time.monotonic,
    sleep: Callable[[float], None] = time.sleep,
) -> dict[str, Any]:
    """Observe, then stop/reap both owned children and close the heartbeat reader.

    Input errors leave child/descriptor ownership with the caller. Once observation
    starts, ownership transfers here even on failure. Clock/sleep injection is for
    controlled protocol tests; those receipts cannot qualify a physical soak.
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
    receipt: dict[str, Any] = {
        "schemaVersion": 1, "recordType": "external-beta-soak-supervision",
        "evidenceScope": "engineering", "releaseEligible": False, "status": "RUNNING",
        "recordId": record_id, "installedTreeSha256": installed_tree_sha256,
        "workloadSha256": workload_sha256, "requiredSeconds": duration_seconds,
        "supervisorPid": supervisor_pid, "productPid": product.pid, "collectorPid": collector.pid,
        "clockAuthority": "supervisor-monotonic", "pollIntervalSeconds": poll_interval_seconds,
        "maxGapSeconds": max_gap_seconds, "observations": [], "cleanupErrors": [],
        "heartbeatIntervalSeconds": HEARTBEAT_INTERVAL_SECONDS,
        "heartbeatLatenessSeconds": HEARTBEAT_LATENESS_SECONDS,
    }
    failure = None
    cause = None
    interruption = None
    try:
        os.set_blocking(heartbeat_fd, False)
        started = clock()
        if not _number(started, 0, float("inf")):
            raise ValueError("supervisor clock must be finite and nonnegative")
        previous_elapsed = None
        last_heartbeat = 0.0
        sequence = 0
        pending = b""
        maximum_observations = math.ceil(duration_seconds / poll_interval_seconds) + 2
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
                next_poll = min(previous_elapsed + poll_interval_seconds, duration_seconds)
                if elapsed + 1e-6 < next_poll:
                    raise ValueError("supervisor poll completed before its declared cadence")
                if elapsed - previous_elapsed > max_gap_seconds:
                    raise ValueError("supervisor observation gap exceeds its declared limit")
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
            receipt["observations"].append({
                "elapsedSeconds": elapsed, "heartbeatSequence": sequence,
                "heartbeatAgeSeconds": elapsed - last_heartbeat,
            })
            if elapsed >= duration_seconds:
                if sequence == 0 or pending:
                    raise ValueError("heartbeat unavailable or incomplete at the declared endpoint")
                receipt["status"] = "COMPLETE"
                break
            previous_elapsed = elapsed
            sleep(min(poll_interval_seconds, duration_seconds - elapsed))
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
            try:
                os.close(heartbeat_fd)
            except BaseException as exc:
                receipt["cleanupErrors"].append(f"heartbeat reader close failed: {exc}")
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
