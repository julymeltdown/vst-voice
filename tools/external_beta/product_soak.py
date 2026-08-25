from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path, PurePosixPath
from typing import Any


HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")
PLATFORMS = {"macos": "arm64", "windows": "x86_64"}
REQUIRED_FAULT_IDS = (
    "device-loss-reconnect",
    "sleep-wake",
    "bank-disappearance",
    "media-disappearance",
    "cache-preferences-corruption",
    "save-export-interruption",
    "disk-full",
    "kill-during-autosave",
    "safe-mode-startup",
)
DEFAULT_THRESHOLDS = {
    "maxRssGrowthBytes": 64 * 1024 * 1024,
    "maxHandleGrowth": 32,
    "maxThreadGrowth": 8,
    "maxRssBytes": 2 * 1024 * 1024 * 1024,
    "maxCpuPercent": 95.0,
    "maxCallbackLatencyUs": 5000.0,
    "maxRenderLatencyMs": 1000.0,
    "maxQueueDepth": 256,
    "maxQueueAgeMs": 1000.0,
    "maxMediaBudgetHighWaterBytes": 512 * 1024 * 1024,
    "maxCacheEvictionStallMs": 250.0,
}


@dataclass(frozen=True, slots=True)
class ProductSoakResult:
    passed: bool
    errors: tuple[str, ...] = ()
    blocked: tuple[str, ...] = ()

    def as_dict(self) -> dict[str, Any]:
        return {"passed": self.passed, "errors": list(self.errors), "blocked": list(self.blocked)}


def _hex(value: Any) -> bool:
    return isinstance(value, str) and HEX64.fullmatch(value) is not None


def _time(value: Any) -> bool:
    if not isinstance(value, str) or not value:
        return False
    try:
        datetime.fromisoformat(value.replace("Z", "+00:00"))
        return True
    except ValueError:
        return False


def _safe_relative(value: Any) -> bool:
    if not isinstance(value, str) or not value or "\\" in value:
        return False
    path = PurePosixPath(value)
    return not path.is_absolute() and all(part not in {"", ".", ".."} for part in path.parts)


def _evidence(root: Path, item: Any, label: str, errors: list[str]) -> None:
    if not isinstance(item, dict):
        errors.append(f"{label} must be an object")
        return
    for key in ("kind", "path", "sha256", "capturedAt", "reviewer"):
        if not item.get(key):
            errors.append(f"{label}.{key} is required")
    if not _safe_relative(item.get("path")):
        errors.append(f"{label}.path must be a safe relative path")
        return
    if not _hex(item.get("sha256")):
        errors.append(f"{label}.sha256 must be a 64-character digest")
    path = root / item["path"]
    try:
        root_resolved = root.resolve(strict=True)
        if path.is_symlink():
            errors.append(f"{label}.path must not be a symbolic link")
            return
        resolved = path.resolve(strict=True)
        if root_resolved != resolved and root_resolved not in resolved.parents:
            errors.append(f"{label}.path escapes evidence root")
            return
        if not resolved.is_file():
            errors.append(f"{label}.path is not a regular file")
            return
        digest = hashlib.sha256(resolved.read_bytes()).hexdigest()
        if _hex(item.get("sha256")) and digest != item["sha256"].lower():
            errors.append(f"{label}.sha256 does not match artifact bytes")
    except FileNotFoundError:
        errors.append(f"{label}.path does not exist")
    except OSError as exc:
        errors.append(f"{label}.path cannot be inspected: {exc}")


def _identity(record: dict[str, Any], errors: list[str]) -> None:
    for name in ("appIdentity", "bankIdentity", "projectIdentity"):
        value = record.get(name)
        if not isinstance(value, dict):
            errors.append(f"{name} must be an object")
            continue
        required = {
            "appIdentity": ("version", "buildId", "installedTreeSha256"),
            "bankIdentity": ("id", "version", "contentSha256", "installedProvenanceTreeSha256"),
            "projectIdentity": ("projectSha256", "mediaSha256"),
        }[name]
        for key in required:
            if not value.get(key):
                errors.append(f"{name}.{key} is required")
        for key in ("installedTreeSha256", "contentSha256", "installedProvenanceTreeSha256", "projectSha256", "mediaSha256"):
            if key in value and not _hex(value.get(key)):
                errors.append(f"{name}.{key} must be a 64-character digest")
    if isinstance(record.get("bankIdentity"), dict) and record["bankIdentity"].get("id") == "official.voice.01":
        errors.append("product soak cannot use the Phase 13B Official Voicebank fixture")


def _summary_errors(record: dict[str, Any], samples: list[dict[str, Any]], thresholds: dict[str, Any], errors: list[str]) -> None:
    if not samples:
        return
    summary = record.get("summary")
    if not isinstance(summary, dict):
        errors.append("summary is required")
        return
    first = samples[0]
    last = samples[-1]
    rss_growth = last.get("rssBytes", 0) - first.get("rssBytes", 0)
    handle_growth = last.get("handles", 0) - first.get("handles", 0)
    thread_growth = last.get("threads", 0) - first.get("threads", 0)
    if summary.get("rssGrowthBytes") != rss_growth:
        errors.append("summary.rssGrowthBytes does not match sample series")
    if summary.get("handleGrowth") != handle_growth:
        errors.append("summary.handleGrowth does not match sample series")
    if summary.get("threadGrowth") != thread_growth:
        errors.append("summary.threadGrowth does not match sample series")
    numeric_checks = {
        "maxRssBytes": max(sample.get("rssBytes", 0) for sample in samples),
        "maxCpuPercent": max(sample.get("cpuPercent", 0.0) for sample in samples),
        "maxCallbackLatencyUs": max(sample.get("callbackLatencyUs", 0.0) for sample in samples),
        "maxRenderLatencyMs": max(sample.get("renderLatencyMs", 0.0) for sample in samples),
        "maxQueueDepth": max(sample.get("queueDepth", 0) for sample in samples),
        "maxQueueAgeMs": max(sample.get("queueAgeMs", 0.0) for sample in samples),
        "maxMediaBudgetHighWaterBytes": max(sample.get("mediaBudgetHighWaterBytes", 0) for sample in samples),
        "maxCacheEvictionStallMs": max(sample.get("cacheEvictionStallMs", 0.0) for sample in samples),
        "underflowCount": max(sample.get("underflows", 0) for sample in samples),
        "xrunCount": max(sample.get("xruns", 0) for sample in samples),
        "controlQueueOverflowCount": max(sample.get("controlQueueOverflow", 0) for sample in samples),
    }
    for key, value in numeric_checks.items():
        if summary.get(key) != value:
            errors.append(f"summary.{key} does not match sample series")
    if summary.get("restartCount") != 0:
        errors.append("restartCount must be zero; restart cannot hide growth")
    if summary.get("dataLoss") is not False:
        errors.append("summary.dataLoss must be false")
    comparisons = (
        ("rssGrowthBytes", thresholds["maxRssGrowthBytes"]),
        ("handleGrowth", thresholds["maxHandleGrowth"]),
        ("threadGrowth", thresholds["maxThreadGrowth"]),
        ("maxRssBytes", thresholds["maxRssBytes"]),
        ("maxCpuPercent", thresholds["maxCpuPercent"]),
        ("maxCallbackLatencyUs", thresholds["maxCallbackLatencyUs"]),
        ("maxRenderLatencyMs", thresholds["maxRenderLatencyMs"]),
        ("maxQueueDepth", thresholds["maxQueueDepth"]),
        ("maxQueueAgeMs", thresholds["maxQueueAgeMs"]),
        ("maxMediaBudgetHighWaterBytes", thresholds["maxMediaBudgetHighWaterBytes"]),
        ("maxCacheEvictionStallMs", thresholds["maxCacheEvictionStallMs"]),
    )
    for key, maximum in comparisons:
        value = summary.get(key)
        if isinstance(value, (int, float)) and value > maximum:
            errors.append(f"{key} exceeds declared threshold")
    for key in ("underflowCount", "xrunCount", "controlQueueOverflowCount"):
        if summary.get(key) != 0:
            errors.append(f"{key} must remain zero")


def validate_product_soak(record: dict[str, Any], root: Path, thresholds: dict[str, Any] | None = None) -> ProductSoakResult:
    errors: list[str] = []
    blocked: list[str] = []
    thresholds = {**DEFAULT_THRESHOLDS, **(thresholds or {})}
    if not isinstance(record, dict):
        return ProductSoakResult(False, ("product soak record must be an object",), ())
    if record.get("schemaVersion") != 1:
        errors.append("record.schemaVersion must be 1")
    if record.get("recordType") != "external-beta-product-soak":
        errors.append("record.recordType is invalid")
    duration = record.get("durationSeconds")
    phase = record.get("phase")
    if duration == 1800 and phase != "usable-alpha-30m":
        errors.append("1800-second soak must be phase usable-alpha-30m")
    elif duration == 7200 and phase != "external-beta-120m":
        errors.append("7200-second soak must be phase external-beta-120m")
    elif duration not in {1800, 7200}:
        errors.append("durationSeconds must be 1800 or 7200")
    platform = record.get("platform")
    if PLATFORMS.get(platform) != record.get("architecture"):
        errors.append("platform/architecture is outside the target matrix")
    for key in ("recordId", "osBuild", "workloadId", "workloadSha256", "machineProfileId", "machineProfileSha256", "startedAt", "endedAt", "clockAuthority", "deviceAuthority"):
        if not record.get(key):
            errors.append(f"record.{key} is required")
    for key in ("workloadSha256", "machineProfileSha256"):
        if not _hex(record.get(key)):
            errors.append(f"record.{key} must be a 64-character digest")
    if not _time(record.get("startedAt")) or not _time(record.get("endedAt")):
        errors.append("startedAt and endedAt must be ISO-8601 timestamps")
    if record.get("clockAuthority") != "physical-device-clock" or record.get("deviceAuthority") != "physical":
        errors.append("soak must use physical-device timing and device authority")
    _identity(record, errors)
    thresholds_record = record.get("thresholds")
    if not isinstance(thresholds_record, dict):
        errors.append("thresholds must be captured in the soak record")
    else:
        for key, value in thresholds.items():
            if thresholds_record.get(key) != value:
                errors.append(f"thresholds.{key} must equal the declared product threshold")
    samples = record.get("samples")
    if not isinstance(samples, list) or not samples:
        errors.append("samples must be a non-empty time series")
        samples = []
    last_elapsed = -1.0
    sample_fields = ("elapsedSeconds", "rssBytes", "handles", "threads", "cpuPercent", "renderLatencyMs", "callbackLatencyUs", "queueDepth", "queueAgeMs", "cacheEvictionStallMs", "mediaBudgetHighWaterBytes", "underflows", "xruns", "controlQueueOverflow")
    for index, sample in enumerate(samples):
        label = f"samples[{index}]"
        if not isinstance(sample, dict):
            errors.append(f"{label} must be an object")
            continue
        for key in sample_fields:
            if key not in sample:
                errors.append(f"{label}.{key} is required")
        elapsed = sample.get("elapsedSeconds")
        if not isinstance(elapsed, (int, float)) or isinstance(elapsed, bool):
            errors.append(f"{label}.elapsedSeconds must be numeric")
        elif elapsed <= last_elapsed:
            errors.append("sample elapsedSeconds must be strictly increasing")
        elif elapsed < 0:
            errors.append(f"{label}.elapsedSeconds cannot be negative")
        else:
            last_elapsed = elapsed
        for key in sample_fields[1:]:
            value = sample.get(key)
            if not isinstance(value, (int, float)) or isinstance(value, bool) or value < 0:
                errors.append(f"{label}.{key} must be a non-negative number")
    if samples and isinstance(duration, int) and last_elapsed < duration:
        errors.append("sample series does not cover the declared soak duration")
    _summary_errors(record, samples, thresholds, errors)
    faults = record.get("faults")
    if not isinstance(faults, list):
        errors.append("faults must be an array")
        faults = []
    fault_ids: set[str] = set()
    for index, fault in enumerate(faults):
        label = f"faults[{index}]"
        if not isinstance(fault, dict):
            errors.append(f"{label} must be an object")
            continue
        fault_id = fault.get("id")
        fault_ids.add(fault_id)
        if fault_id not in REQUIRED_FAULT_IDS:
            errors.append(f"{label}.id is not in the fault matrix")
        if fault.get("result") != "RECOVERED":
            errors.append(f"{label}.result must be RECOVERED")
        if not fault.get("userDecision") or not fault.get("evidenceRecordId"):
            errors.append(f"{label}.userDecision and evidenceRecordId are required")
        if fault.get("dataLoss") is not False:
            errors.append(f"{label}.dataLoss must be false")
    missing_faults = sorted(set(REQUIRED_FAULT_IDS) - fault_ids)
    errors.extend(f"fault matrix row is missing: {fault_id}" for fault_id in missing_faults)
    blocked.extend(missing_faults)
    evidence = record.get("evidence")
    if not isinstance(evidence, list) or not evidence:
        errors.append("soak evidence must be non-empty")
    else:
        for index, item in enumerate(evidence):
            _evidence(root, item, f"evidence[{index}]", errors)
    if record.get("status") != "PASS":
        errors.append("record.status must be PASS")
        blocked.append("record")
    return ProductSoakResult(not errors and not blocked, tuple(errors), tuple(sorted(set(blocked))))


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return value
