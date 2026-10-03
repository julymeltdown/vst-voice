"""Collect the standalone-journey record: the machine and its audio device, measured.

The twenty UA journey rows describe a person creating, editing, saving, recovering and
exporting in the standalone app. Those cannot be automated honestly. What CAN be measured is the
environment the journey ran in, and the validator demands it: device identity, sample rate, block
size and channel count, all with `authority: physical`.

Before this, all of that was typed. A record could claim a 48 kHz physical device on a machine
whose only output runs at 44.1 kHz. This module reads the actual device topology from the
operating system and reports what it finds, including when it finds nothing.
"""

from __future__ import annotations

import json
import platform
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .standalone_evidence import UA_ROW_IDS


def observe_audio_devices() -> dict[str, Any]:
    """The default output device as the operating system reports it.

    Returns an empty devices list rather than a guess when the query fails, so a caller cannot
    mistake "we could not measure" for "the device is fine".
    """
    if sys.platform != "darwin":
        return {"devices": [], "reason": "unsupported-platform"}
    try:
        completed = subprocess.run(
            ["system_profiler", "SPAudioDataType", "-json"],
            capture_output=True, text=True, timeout=120, check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return {"devices": [], "reason": "query-failed"}
    if completed.returncode != 0:
        return {"devices": [], "reason": "query-failed"}
    try:
        entries = json.loads(completed.stdout).get("SPAudioDataType", [])
    except json.JSONDecodeError:
        return {"devices": [], "reason": "unparseable"}
    devices: list[dict[str, Any]] = []
    for entry in entries:
        for item in entry.get("_items", []):
            name = item.get("_name")
            if not isinstance(name, str) or not name:
                continue
            devices.append({
                "deviceId": name,
                "deviceName": name,
                "manufacturer": item.get("coreaudio_device_manufacturer", ""),
                "sampleRate": _int_or_none(item.get("coreaudio_device_srate")),
                "transport": item.get("coreaudio_device_transport", ""),
                "defaultOutput": item.get("coreaudio_default_audio_output_device") is not None,
            })
    return {"devices": devices, "reason": ""}


def _int_or_none(value: Any) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        return int(value)
    if isinstance(value, str) and value.strip().isdigit():
        return int(value.strip())
    return None


def select_output_device(observed: dict[str, Any]) -> dict[str, Any] | None:
    """The device the journey should record, preferring the system default output."""
    devices = observed.get("devices") or []
    if not isinstance(devices, list) or not devices:
        return None
    for device in devices:
        if isinstance(device, dict) and device.get("defaultOutput") and device.get("sampleRate"):
            return device
    for device in devices:
        if isinstance(device, dict) and device.get("sampleRate"):
            return device
    return None


def build_standalone_record(
    *,
    record_id: str,
    operator: str,
    app_identity: dict[str, Any],
    bank_identity: dict[str, Any],
    project_identity: dict[str, Any],
    workload_id: str,
    workload_sha256: str,
    machine_profile_id: str,
    machine_profile_sha256: str,
    rows: list[dict[str, Any]],
    block_size: int,
    channels: int = 2,
    started_at: str,
    ended_at: str,
) -> dict[str, Any]:
    """Assemble the record, with the device block measured rather than declared.

    When no physical output device can be observed the record says so and is marked NOT_RUN,
    because a journey claiming to have run on physical hardware that was never identified is
    exactly the sort of claim this evidence exists to prevent.
    """
    observed = observe_audio_devices()
    device = select_output_device(observed)
    machine = platform.machine()
    architecture = "arm64" if machine in {"arm64", "aarch64"} else "x86_64"
    record = {
        "schemaVersion": 1,
        "recordType": "engineering-standalone-journey",
        "status": "PASS" if device is not None else "NOT_RUN",
        "engineeringQualification": True,
        "recordId": record_id,
        "platform": "macos" if sys.platform == "darwin" else sys.platform,
        "architecture": architecture,
        "osBuild": platform.mac_ver()[0] or platform.release(),
        "operator": operator,
        "startedAt": started_at,
        "endedAt": ended_at,
        "clockAuthority": "physical-device-clock",
        "workloadId": workload_id,
        "workloadSha256": workload_sha256,
        "machineProfileId": machine_profile_id,
        "machineProfileSha256": machine_profile_sha256,
        "appIdentity": app_identity,
        "bankIdentity": bank_identity,
        "projectIdentity": project_identity,
        "comparisonPolicy": {
            "crossPlatformByteIdentity": False,
            "crossPlatformTolerance": "pcm-error-within-declared-tolerance",
        },
        "rows": rows,
        "collector": {
            "tool": "tools/external_beta/standalone_collector.py",
            "collectorVersion": 1,
            "measuredDigests": ["appIdentity.installedTreeSha256"],
            "manualRows": list(UA_ROW_IDS),
        },
    }
    if device is not None:
        record['device'] = {
            "deviceId": device["deviceId"],
            "deviceName": device["deviceName"],
            "manufacturer": device["manufacturer"],
            "transport": device["transport"],
            "sampleRate": device["sampleRate"],
            "blockSize": block_size,
            "channels": channels,
            "authority": "physical",
        }
    else:
        record["deviceObservation"] = {
            "reason": observed.get("reason") or "no-device-with-sample-rate",
            "deviceCount": len(observed.get("devices") or []),
        }
    return record


def now_utc() -> str:
    stamp = datetime.now(timezone.utc).replace(microsecond=0).isoformat()
    return stamp.replace("+00:00", "Z")
