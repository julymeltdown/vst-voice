from __future__ import annotations

import copy
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from tools.external_beta.product_soak import DEFAULT_THRESHOLDS, REQUIRED_FAULT_IDS, validate_product_soak


def _sample(elapsed: int, rss: int = 100_000_000, handles: int = 100, threads: int = 10) -> dict:
    return {
        "elapsedSeconds": elapsed,
        "rssBytes": rss,
        "handles": handles,
        "threads": threads,
        "cpuPercent": 40.0,
        "renderLatencyMs": 120.0,
        "callbackLatencyUs": 800.0,
        "queueDepth": 3,
        "queueAgeMs": 10.0,
        "cacheEvictionStallMs": 5.0,
        "mediaBudgetHighWaterBytes": 10_000_000,
        "underflows": 0,
        "xruns": 0,
        "controlQueueOverflow": 0,
    }


def _record(root: Path, duration: int = 1800, platform: str = "macos") -> dict:
    evidence = []
    for name in ("metrics", "faults", "export"):
        path = root / "evidence" / f"{name}.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        content = json.dumps({"kind": name}, sort_keys=True).encode()
        path.write_bytes(content)
        evidence.append({"kind": name, "path": str(path.relative_to(root)), "sha256": hashlib.sha256(content).hexdigest(), "capturedAt": "2026-08-21T12:00:00Z", "reviewer": "soak-reviewer"})
    samples = [_sample(0), _sample(duration)]
    summary = {
        "rssGrowthBytes": 0,
        "handleGrowth": 0,
        "threadGrowth": 0,
        "maxRssBytes": 100_000_000,
        "maxCpuPercent": 40.0,
        "maxCallbackLatencyUs": 800.0,
        "maxRenderLatencyMs": 120.0,
        "maxQueueDepth": 3,
        "maxQueueAgeMs": 10.0,
        "maxMediaBudgetHighWaterBytes": 10_000_000,
        "maxCacheEvictionStallMs": 5.0,
        "underflowCount": 0,
        "xrunCount": 0,
        "controlQueueOverflowCount": 0,
        "restartCount": 0,
        "dataLoss": False,
    }
    faults = [{"id": fault_id, "result": "RECOVERED", "userDecision": "operator-recovered", "evidenceRecordId": "faults", "dataLoss": False} for fault_id in REQUIRED_FAULT_IDS]
    return {
        "schemaVersion": 1,
        "recordType": "external-beta-product-soak",
        "status": "PASS",
        "recordId": f"{platform}-{duration}",
        "phase": "usable-alpha-30m" if duration == 1800 else "external-beta-120m",
        "durationSeconds": duration,
        "platform": platform,
        "architecture": "arm64" if platform == "macos" else "x86_64",
        "osBuild": "test-os-build",
        "appIdentity": {"version": "0.13.1", "buildId": "build-001", "installedTreeSha256": "a" * 64},
        "bankIdentity": {"id": "beta.voice.01", "version": "0.1.0", "contentSha256": "b" * 64, "installedProvenanceTreeSha256": "c" * 64},
        "projectIdentity": {"projectSha256": "d" * 64, "mediaSha256": "e" * 64},
        "workloadId": "eb.standalone.soak.v1",
        "workloadSha256": "f" * 64,
        "machineProfileId": f"eb.{platform}.reference.v1",
        "machineProfileSha256": "1" * 64,
        "clockAuthority": "physical-device-clock",
        "deviceAuthority": "physical",
        "thresholds": dict(DEFAULT_THRESHOLDS),
        "samples": samples,
        "summary": summary,
        "faults": faults,
        "evidence": evidence,
        "startedAt": "2026-08-21T10:00:00Z",
        "endedAt": "2026-08-21T12:00:00Z",
    }


class ProductSoakTests(unittest.TestCase):
    def test_30_minute_and_120_minute_records_pass_on_each_target_os(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for duration, platform in ((1800, "macos"), (7200, "macos"), (7200, "windows")):
                result = validate_product_soak(_record(root, duration, platform), root)
                self.assertTrue(result.passed, (duration, platform, result.errors))

    def test_threshold_violation_and_nonzero_realtime_counter_fail(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            record["summary"]["rssGrowthBytes"] = DEFAULT_THRESHOLDS["maxRssGrowthBytes"] + 1
            record["samples"][-1]["rssBytes"] += DEFAULT_THRESHOLDS["maxRssGrowthBytes"] + 1
            record["summary"]["xrunCount"] = 1
            result = validate_product_soak(record, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("threshold" in error or "xrun" in error for error in result.errors))

    def test_cpu_threshold_is_bound_to_sample_series(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            record["samples"][-1]["cpuPercent"] = DEFAULT_THRESHOLDS["maxCpuPercent"] + 1.0
            record["summary"]["maxCpuPercent"] = DEFAULT_THRESHOLDS["maxCpuPercent"] + 1.0
            result = validate_product_soak(record, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("maxCpuPercent" in error for error in result.errors))

    def test_restart_cannot_hide_growth(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            record["summary"]["restartCount"] = 1
            result = validate_product_soak(record, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("restart" in error for error in result.errors))

    def test_missing_fault_and_data_loss_are_blocking(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            record["faults"] = record["faults"][:-1]
            record["faults"][0]["dataLoss"] = True
            result = validate_product_soak(record, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("fault matrix row is missing" in error for error in result.errors))
            self.assertTrue(any("dataLoss" in error for error in result.errors))

    def test_sample_series_must_be_monotonic_and_cover_duration(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            record["samples"] = [_sample(0), _sample(100), _sample(90)]
            result = validate_product_soak(record, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("strictly increasing" in error or "cover" in error for error in result.errors))

    def test_evidence_tamper_and_nonphysical_authority_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            original = copy.deepcopy(record)
            record["deviceAuthority"] = "simulated"
            artifact = root / record["evidence"][0]["path"]
            artifact.write_text("tampered", encoding="utf-8")
            result = validate_product_soak(record, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("physical" in error for error in result.errors))
            self.assertTrue(any("does not match" in error for error in result.errors))
            self.assertEqual("macos", original["platform"])


if __name__ == "__main__":
    unittest.main()
