from __future__ import annotations

import hashlib
import json
import math
import re
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path, PurePosixPath
from typing import Any

try:
    from .soak_session_validation import (
        ENGINEERING_ONLY, SESSION_REQUIRED, PRODUCT_LIMIT, SoakReplayContext,
        parse_json, read_reference, reuse_reference, validate_soak_session_reference,
    )
except ImportError:
    from soak_session_validation import (
        ENGINEERING_ONLY, SESSION_REQUIRED, PRODUCT_LIMIT, SoakReplayContext,
        parse_json, read_reference, reuse_reference, validate_soak_session_reference,
    )


try:
    from tools.platform_identity import host_platforms
except ImportError:  # tooling imports this module with tools/external_beta on sys.path
    import sys

    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from platform_identity import host_platforms


HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")
PLATFORMS = host_platforms()
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
SAMPLE_INTEGER_FIELDS = frozenset((
    "rssBytes", "handles", "threads", "queueDepth", "mediaBudgetHighWaterBytes",
    "underflows", "xruns", "controlQueueOverflow",
))
SUMMARY_GROWTH_FIELDS = ("rssGrowthBytes", "handleGrowth", "threadGrowth")
SUMMARY_INTEGER_FIELDS = frozenset(SUMMARY_GROWTH_FIELDS) | frozenset((
    "maxRssBytes", "maxQueueDepth", "maxMediaBudgetHighWaterBytes", "underflowCount",
    "xrunCount", "controlQueueOverflowCount", "restartCount",
))
# The canonical soak uses a one-second performance time series. Allow one initial
# sampling interval, but do not shorten the required final elapsed time.
SAMPLE_START_TOLERANCE_SECONDS = 1


@dataclass(frozen=True, slots=True)
class ProductSoakResult:
    passed: bool
    errors: tuple[str, ...] = ()
    blocked: tuple[str, ...] = ()
    session: Any = None

    def as_dict(self) -> dict[str, Any]:
        return {"passed": self.passed, "errors": list(self.errors), "blocked": list(self.blocked)}


def _hex(value: Any) -> bool:
    return isinstance(value, str) and HEX64.fullmatch(value) is not None


def _metric_number(value: Any, *, integer: bool = False, nonnegative: bool = True) -> bool:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return False
    # Integers are finite without converting potentially large values to float.
    if isinstance(value, float) and (integer or not math.isfinite(value)):
        return False
    return not nonnegative or value >= 0


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
    for key in (*SUMMARY_GROWTH_FIELDS, *numeric_checks, "restartCount"):
        integer = key in SUMMARY_INTEGER_FIELDS
        nonnegative = key not in SUMMARY_GROWTH_FIELDS
        if not _metric_number(summary.get(key), integer=integer, nonnegative=nonnegative):
            kind = "integer" if integer else "number"
            bounds = "non-negative " if nonnegative else ""
            errors.append(f"summary.{key} must be a finite {bounds}{kind}")
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
        if _metric_number(value, nonnegative=False) and value > maximum:
            errors.append(f"{key} exceeds declared threshold")
    for key in ("underflowCount", "xrunCount", "controlQueueOverflowCount"):
        if summary.get(key) != 0:
            errors.append(f"{key} must remain zero")


def validate_product_soak(record: dict[str, Any], root: Path, thresholds: dict[str, Any] | None = None,
                          *, expected_session_bindings: dict | None = None,
                          soak_replay_context: SoakReplayContext | None = None) -> ProductSoakResult:
    errors: list[str] = []
    blocked: list[str] = []
    thresholds = {**DEFAULT_THRESHOLDS, **(thresholds or {})}
    context = soak_replay_context if soak_replay_context is not None else SoakReplayContext()
    session = None
    if not isinstance(record, dict):
        return ProductSoakResult(False, ("product soak record must be an object",), ())
    if root is None:
        return ProductSoakResult(False, ("explicit evidence root is required",), ("soak-session",))
    if record.get("schemaVersion") != 1:
        errors.append("record.schemaVersion must be 1")
    if record.get("recordType") != "external-beta-product-soak":
        errors.append("record.recordType is invalid")
    duration = record.get("durationSeconds")
    phase = record.get("phase")
    duration_valid = _metric_number(duration, integer=True) and duration in {1800, 7200}
    if not duration_valid:
        errors.append("durationSeconds must be an integer equal to 1800 or 7200")
    elif duration == 1800 and phase != "usable-alpha-30m":
        errors.append("1800-second soak must be phase usable-alpha-30m")
    elif duration == 7200 and phase != "external-beta-120m":
        errors.append("7200-second soak must be phase external-beta-120m")
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
    if not isinstance(samples, list) or len(samples) < 2:
        errors.append("samples must be a time series with at least two samples")
        samples = []
    last_elapsed = -1.0
    sample_values_valid = True
    sample_fields = ("elapsedSeconds", "rssBytes", "handles", "threads", "cpuPercent", "renderLatencyMs", "callbackLatencyUs", "queueDepth", "queueAgeMs", "cacheEvictionStallMs", "mediaBudgetHighWaterBytes", "underflows", "xruns", "controlQueueOverflow")
    for index, sample in enumerate(samples):
        label = f"samples[{index}]"
        if not isinstance(sample, dict):
            errors.append(f"{label} must be an object")
            sample_values_valid = False
            continue
        for key in sample_fields:
            if key not in sample:
                errors.append(f"{label}.{key} is required")
                sample_values_valid = False
                continue
            integer = key in SAMPLE_INTEGER_FIELDS
            if not _metric_number(sample[key], integer=integer):
                kind = "integer" if integer else "number"
                errors.append(f"{label}.{key} must be a finite non-negative {kind}")
                sample_values_valid = False
        elapsed = sample.get("elapsedSeconds")
        if _metric_number(elapsed):
            if elapsed <= last_elapsed:
                errors.append("sample elapsedSeconds must be strictly increasing")
            else:
                last_elapsed = elapsed
    if samples and sample_values_valid and duration_valid:
        first_elapsed = samples[0]["elapsedSeconds"]
        final_elapsed = samples[-1]["elapsedSeconds"]
        if first_elapsed > SAMPLE_START_TOLERANCE_SECONDS:
            errors.append("sample series must start within one second of soak start")
        if final_elapsed < duration:
            errors.append("sample series does not cover the declared soak duration")
        # Compare endpoints without subtracting mixed floats/large integers.
        if final_elapsed < first_elapsed + duration - SAMPLE_START_TOLERANCE_SECONDS:
            errors.append("sample series span does not cover the declared soak duration within one-second sampling tolerance")
    # Do not perform subtraction/max on malformed samples after recording their errors.
    if sample_values_valid:
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
    session_items = [item for item in evidence if isinstance(item, dict) and item.get("kind") == "soak-session-index"] if isinstance(evidence, list) else []
    if len(session_items) != 1:
        errors.append(SESSION_REQUIRED)
        blocked.append("soak-session")
    else:
        item = session_items[0]
        for key in ("path", "sha256", "capturedAt", "reviewer"):
            if not item.get(key):
                errors.append(f"soak-session-index.{key} is required")
        bindings = {"sessionId": record.get("recordId"), "durationSeconds": duration,
                    "installedTreeSha256": record.get("appIdentity", {}).get("installedTreeSha256") if isinstance(record.get("appIdentity"), dict) else None,
                    "workloadId": record.get("workloadId"), "workloadSha256": record.get("workloadSha256"),
                    "machineProfileId": record.get("machineProfileId"),
                    "machineProfileSha256": record.get("machineProfileSha256"), "platform": platform,
                    "architecture": record.get("architecture"),
                    **{key: record.get(key) for key in ("appIdentity", "bankIdentity", "projectIdentity")}}
        for key, value in (expected_session_bindings or {}).items():
            if key in bindings and bindings[key] != value:
                errors.append(f"outer product soak binding {key} differs")
            if key in ("buildId", "version") and (not isinstance(record.get("appIdentity"), dict)
                    or record["appIdentity"].get(key) != value):
                errors.append(f"outer product soak appIdentity.{key} differs")
            bindings[key] = value
        session = validate_soak_session_reference({"locator": item.get("path"), "sha256": item.get("sha256")},
            evidence_root=root, expected_bindings=bindings, expected_samples=samples, replay_context=context)
        errors.extend(session.errors)
        # Engineering authority cannot be overridden by status or physical wrapper labels.
        errors.append(ENGINEERING_ONLY)
        blocked.append("soak-session")
    if not isinstance(evidence, list) or not evidence:
        errors.append("soak evidence must be non-empty")
    else:
        for index, item in enumerate(evidence):
            if not isinstance(item, dict) or item.get("kind") != "soak-session-index":
                try:
                    reused = reuse_reference({"locator": item.get("path"), "sha256": item.get("sha256")},
                        base=root, maximum_bytes=PRODUCT_LIMIT, replay_context=context) if isinstance(item, dict) else None
                    if reused is None:
                        _evidence(root, item, f"evidence[{index}]", errors)
                    else:
                        for key in ("kind", "path", "sha256", "capturedAt", "reviewer"):
                            if not item.get(key):
                                errors.append(f"evidence[{index}].{key} is required")
                except (OSError, ValueError) as exc:
                    errors.append(f"evidence[{index}]: {exc}")
    if record.get("status") != "PASS":
        errors.append("record.status must be PASS")
        blocked.append("record")
    return ProductSoakResult(not errors and not blocked, tuple(errors), tuple(sorted(set(blocked))), session)


def validate_product_soak_reference(reference: dict, root: Path | None, *,
                                    expected_session_bindings: dict | None = None,
                                    soak_replay_context: SoakReplayContext | None = None) -> ProductSoakResult:
    context = soak_replay_context if soak_replay_context is not None else SoakReplayContext()
    try:
        contents = read_reference(reference, evidence_root=root, maximum_bytes=PRODUCT_LIMIT, replay_context=context)
        record = parse_json(contents)
        if not isinstance(record, dict) or record.get("recordType") != "external-beta-product-soak":
            return ProductSoakResult(False, ("product soak reference must contain a typed external-beta-product-soak record",), ("soak-session",))
        return validate_product_soak(record, root, expected_session_bindings=expected_session_bindings,
                                     soak_replay_context=context)
    except (OSError, ValueError, TypeError) as exc:
        return ProductSoakResult(False, (f"product soak reference: {exc}",), ("soak-session",))


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return value
