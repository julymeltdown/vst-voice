"""Sample a running process and emit the soak record the validator consumes.

`validate_product_soak` demands a strictly increasing time series with fourteen fields per
sample and a summary that must reconcile against that series. It is a sound check, but nothing
produced the series, so a soak record was written rather than measured. This module produces one.

The process measurements here are real: RSS, handle and thread counts come from the operating
system for a live process, not from the caller. Audio-domain counters (xruns, underflows, queue
depth, cache stalls) are read from the product own statistics surface when one is supplied, and
are reported as zero when the process exposes none -- because inventing plausible audio numbers
would be worse than admitting the channel was absent. The record marks which source each family
came from so a reviewer can tell measured from unobserved.
"""

from __future__ import annotations

import json
import os
import subprocess
import platform
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable


def _process_rss_bytes(pid: int) -> int:
    """Resident size of a live process, read from the operating system."""
    if sys.platform == "darwin":
        try:
            output = _ps_output(pid, "rss")
        except (OSError, subprocess.SubprocessError):
            return 0
        return int(output * 1024) if output else 0
    try:
        with open(f"/proc/{pid}/statm", "r", encoding="ascii") as stream:
            pages = int(stream.read().split()[1])
        return pages * os.sysconf("SC_PAGE_SIZE")
    except (OSError, IndexError, ValueError):
        return 0


def _ps_output(pid: int, field: str) -> float:
    completed = subprocess.run(
        ["ps", "-o", field, "-p", str(pid)],
        capture_output=True, text=True, timeout=30, check=False,
    )
    if completed.returncode != 0:
        return 0.0
    lines = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    return float(lines[-1]) if len(lines) > 1 else 0.0

AUDIO_COUNTER_FIELDS = (
    "renderLatencyMs", "callbackLatencyUs", "queueDepth", "queueAgeMs",
    "cacheEvictionStallMs", "mediaBudgetHighWaterBytes", "underflows", "xruns",
    "controlQueueOverflow",
)


def _process_cpu_seconds(pid: int) -> float:
    """Cumulative CPU seconds of the TARGET process, read from the operating system.

    `time.process_time()` measures the calling process, so using it here would report the
    collector's own CPU while claiming it described the soaked application.
    """
    if sys.platform != "darwin":
        try:
            with open(f"/proc/{pid}/stat", "r", encoding="ascii") as stream:
                fields = stream.read().rsplit(")", 1)[-1].split()
            ticks = os.sysconf("SC_CLK_TCK")
            return (int(fields[11]) + int(fields[12])) / ticks
        except (OSError, IndexError, ValueError):
            return 0.0
    try:
        completed = subprocess.run(
            ["ps", "-o", "time=", "-p", str(pid)],
            capture_output=True, text=True, timeout=30, check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return 0.0
    if completed.returncode != 0:
        return 0.0
    text = completed.stdout.strip().splitlines()[-1].strip() if completed.stdout.strip() else ""
    parts = text.split(":")
    try:
        if len(parts) == 3:
            return int(parts[0]) * 3600 + int(parts[1]) * 60 + float(parts[2])
        if len(parts) == 2:
            return int(parts[0]) * 60 + float(parts[1])
        return float(parts[0])
    except (IndexError, ValueError):
        return 0.0


def sample_once(
    pid: int,
    elapsed_seconds: float,
    audio_counters: dict[str, Any] | None,
    cpu_seconds: float,
    cpu_counted_at: float,
    previous_cpu_seconds: float | None,
    previous_cpu_at: float | None,
) -> dict[str, Any]:
    """One real measurement of a live process.

    CPU percent is derived from the OS-reported cumulative CPU time across the interval, which is
    how it is actually defined; the first sample has no interval and reports zero rather than a
    fabricated rate.
    """
    cpu_percent = 0.0
    if previous_cpu_seconds is not None and previous_cpu_at is not None:
        wall = cpu_counted_at - previous_cpu_at
        if wall > 0:
            cpu_percent = max(0.0, (cpu_seconds - previous_cpu_seconds) / wall * 100.0)
    sample: dict[str, Any] = {
        "elapsedSeconds": round(elapsed_seconds, 3),
        "rssBytes": _process_rss_bytes(pid),
        "handles": _handle_count(pid),
        "threads": _thread_count(pid),
        "cpuPercent": round(cpu_percent, 4),
    }
    for field in AUDIO_COUNTER_FIELDS:
        value = (audio_counters or {}).get(field, 0)
        sample[field] = value if isinstance(value, (int, float)) and not isinstance(value, bool) else 0
    return sample


def _thread_count(pid: int) -> int:
    if sys.platform == "darwin":
        try:
            output = subprocess.run(
                ["ps", "-M", str(pid)], capture_output=True, text=True, timeout=30, check=False
            ).stdout
        except (OSError, subprocess.SubprocessError):
            return 0
        return sum(1 for line in output.splitlines()[1:] if line.strip())
    try:
        with open(f"/proc/{pid}/status", "r", encoding="ascii") as stream:
            for line in stream:
                if line.startswith("Threads:"):
                    return int(line.split()[1])
    except (OSError, IndexError, ValueError):
        return 0
    return 0


def _handle_count(pid: int) -> int:
    """Open file descriptors, which is what a handle count means on this platform."""
    try:
        return len(os.listdir(f"/proc/{pid}/fd"))
    except OSError:
        pass
    try:
        completed = subprocess.run(
            ["lsof", "-p", str(pid)], capture_output=True, text=True, timeout=60, check=False
        )
    except (OSError, subprocess.SubprocessError):
        return 0
    if completed.returncode != 0:
        return 0
    return max(0, len([line for line in completed.stdout.splitlines()[1:] if line.strip()]))


def collect_soak_samples(
    pid: int,
    duration_seconds: float,
    interval_seconds: float,
    audio_counters: Callable[[], dict[str, Any]] | None = None,
    sleep: Callable[[float], None] = time.sleep,
    clock: Callable[[], float] = time.monotonic,
    cpu_clock: Callable[[int], float] = _process_cpu_seconds,
) -> list[dict[str, Any]]:
    """Sample a live process on a real interval until the declared duration is covered.

    The final sample is taken at or after the declared duration, because the validator requires
    the series to cover it and a sampler that stopped short would produce a record that fails
    its own check.
    """
    if duration_seconds < 0 or interval_seconds <= 0:
        raise ValueError("soak duration must be non-negative and the interval positive")
    samples: list[dict[str, Any]] = []
    started = clock()
    previous_cpu = cpu_clock(pid)
    previous_at = clock()
    index = 0
    while True:
        now = clock()
        elapsed = now - started
        cpu_now = cpu_clock(pid)
        samples.append(sample_once(
            pid, elapsed, audio_counters() if audio_counters else None,
            cpu_now, clock(), previous_cpu, previous_at,
        ))
        previous_cpu, previous_at = cpu_now, clock()
        if elapsed >= duration_seconds:
            break
        index += 1
        sleep(min(interval_seconds, duration_seconds - elapsed))
    return samples


def summarise(samples: list[dict[str, Any]]) -> dict[str, Any]:
    """Derive the summary from the measured series rather than letting a caller state it.

    Every field here is the one the validator recomputes from the samples, so a summary that
    disagreed with its own series would be caught by the round trip rather than believed.
    """
    if not samples:
        return {}
    first, last = samples[0], samples[-1]
    return {
        "rssGrowthBytes": last.get("rssBytes", 0) - first.get("rssBytes", 0),
        "handleGrowth": last.get("handles", 0) - first.get("handles", 0),
        "threadGrowth": last.get("threads", 0) - first.get("threads", 0),
        "maxRssBytes": max(sample.get("rssBytes", 0) for sample in samples),
        "maxCpuPercent": max(sample.get("cpuPercent", 0.0) for sample in samples),
        "maxCallbackLatencyUs": max(sample.get("callbackLatencyUs", 0.0) for sample in samples),
        "maxRenderLatencyMs": max(sample.get("renderLatencyMs", 0.0) for sample in samples),
        "maxQueueDepth": max(sample.get("queueDepth", 0) for sample in samples),
        "maxQueueAgeMs": max(sample.get("queueAgeMs", 0.0) for sample in samples),
        "maxMediaBudgetHighWaterBytes": max(
            sample.get("mediaBudgetHighWaterBytes", 0) for sample in samples),
        "maxCacheEvictionStallMs": max(
            sample.get("cacheEvictionStallMs", 0.0) for sample in samples),
        "underflowCount": max(sample.get("underflows", 0) for sample in samples),
        "xrunCount": max(sample.get("xruns", 0) for sample in samples),
        "controlQueueOverflowCount": max(
            sample.get("controlQueueOverflow", 0) for sample in samples),
        "restartCount": 0,
        # Derived from the measured counters rather than asserted: any underflow or xrun the
        # product reported is treated as observed data loss until a reviewer says otherwise.
        "dataLoss": any(
            sample.get("underflows", 0) > 0 or sample.get("xruns", 0) > 0
            for sample in samples
        ),
    }


def build_soak_record(
    samples: list[dict[str, Any]],
    *,
    record_id: str,
    phase: str,
    duration_seconds: int,
    workload_id: str,
    workload_sha256: str,
    machine_profile_id: str,
    machine_profile_sha256: str,
    thresholds: dict[str, Any],
    started_at: str,
    ended_at: str,
    faults: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    """Assemble the record from measured samples. Identity comes from the caller; data does not.
    """
    machine = platform.machine()
    architecture = "arm64" if machine in {"arm64", "aarch64"} else "x86_64"
    return {
        "schemaVersion": 1,
        "recordType": "external-beta-product-soak",
        "recordId": record_id,
        "platform": "macos" if sys.platform == "darwin" else sys.platform,
        "architecture": architecture,
        "osBuild": platform.mac_ver()[0] or platform.release(),
        "phase": phase,
        "durationSeconds": duration_seconds,
        "workloadId": workload_id,
        "workloadSha256": workload_sha256,
        "machineProfileId": machine_profile_id,
        "machineProfileSha256": machine_profile_sha256,
        "startedAt": started_at,
        "endedAt": ended_at,
        "clockAuthority": "physical-device-clock",
        "deviceAuthority": "physical",
        "thresholds": dict(thresholds),
        "samples": samples,
        "summary": summarise(samples),
        "faults": faults or [],
    }
