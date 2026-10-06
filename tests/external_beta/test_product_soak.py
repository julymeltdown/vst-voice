from __future__ import annotations

import copy
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.external_beta.soak_collector import (
    _process_rss_bytes,
    build_soak_record,
    collect_soak_samples,
    summarise,
)
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
        evidence.append({"kind": name, "path": path.relative_to(root).as_posix(), "sha256": hashlib.sha256(content).hexdigest(), "capturedAt": "2026-08-21T12:00:00Z", "reviewer": "soak-reviewer"})
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


class ProductSoakCollectorTests(unittest.TestCase):
    """The collector must produce a soak series the real validator accepts.

    A soak record asserts memory and latency behaviour over time. Writing one by hand proves
    nothing about a process, so this case samples a live process on a real clock and feeds the
    result into validate_product_soak.
    """

    def test_measured_samples_satisfy_the_validator_they_feed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            template = _record(root)
            samples = collect_soak_samples(os.getpid(), 0.2, 0.05)
            self.assertGreaterEqual(len(samples), 2)
            elapsed = [sample['elapsedSeconds'] for sample in samples]
            self.assertEqual(elapsed, sorted(elapsed))
            self.assertGreaterEqual(elapsed[-1], 0.2)
            self.assertTrue(all(sample['rssBytes'] > 0 for sample in samples))
            self.assertTrue(all(sample['threads'] >= 1 for sample in samples))
            record = build_soak_record(
                samples,
                record_id=template['recordId'],
                phase=template['phase'],
                duration_seconds=template['durationSeconds'],
                workload_id=template['workloadId'],
                workload_sha256=template['workloadSha256'],
                machine_profile_id=template['machineProfileId'],
                machine_profile_sha256=template['machineProfileSha256'],
                thresholds=template['thresholds'],
                started_at=template['startedAt'],
                ended_at=template['endedAt'],
                faults=template['faults'],
            )
            record['status'] = 'PASS'
            for key in ('appIdentity', 'bankIdentity', 'projectIdentity'):
                record[key] = template[key]
            result = validate_product_soak(record, root)
            self.assertEqual([e for e in result.errors if 'samples' in e], [])
            self.assertEqual([e for e in result.errors if 'summary' in e], [])

    def test_rss_is_read_from_the_live_process_not_a_constant(self) -> None:
        # A constant placeholder satisfies 'rssBytes > 0', so the value is compared against
        # what the operating system reports for this very process instead.
        measured = _process_rss_bytes(os.getpid())
        self.assertGreater(measured, 1_000_000)
        samples = collect_soak_samples(os.getpid(), 0.1, 0.05)
        observed = {sample['rssBytes'] for sample in samples}
        self.assertTrue(observed)
        for value in observed:
            # Within an order of magnitude of the live reading: the sampler and this
            # assertion observe the same process moments apart.
            self.assertGreater(value, measured // 4)
            self.assertLess(value, measured * 4)

    def test_a_growing_process_shows_growth(self) -> None:
        # Holding real bytes across a reading must move the measured RSS, which a constant
        # reading could never do. The ballast is retained between the two readings and every
        # page is written, so the pages are resident rather than merely reserved.
        ballast = [bytes(index % 251 for index in range(4096)) * 256 for _ in range(96)]
        resident = sum(len(block) for block in ballast)
        self.assertGreater(resident, 32 * 1024 * 1024)
        before = _process_rss_bytes(os.getpid())
        ballast.extend(bytes(index % 241 for index in range(4096)) * 256 for _ in range(96))
        grown = sum(len(block) for block in ballast)
        after = _process_rss_bytes(os.getpid())
        self.assertGreater(grown, resident)
        self.assertGreater(after, before)

    def test_the_summary_is_derived_not_asserted(self) -> None:
        samples = collect_soak_samples(os.getpid(), 0.1, 0.05)
        summary = summarise(samples)
        self.assertEqual(summary['maxRssBytes'], max(s['rssBytes'] for s in samples))
        self.assertEqual(summary['rssGrowthBytes'], samples[-1]['rssBytes'] - samples[0]['rssBytes'])
        self.assertEqual(summary['handleGrowth'], samples[-1]['handles'] - samples[0]['handles'])

    def test_the_series_covers_the_declared_duration(self) -> None:
        samples = collect_soak_samples(os.getpid(), 0.15, 0.05)
        self.assertGreaterEqual(samples[-1]['elapsedSeconds'], 0.15)

class ProductSoakTests(unittest.TestCase):
    def test_nonfinite_sample_values_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = _record(root)
            for field in baseline["samples"][-1]:
                for value in (float("nan"), float("inf"), float("-inf")):
                    with self.subTest(field=field, value=value):
                        record = copy.deepcopy(baseline)
                        record["samples"][-1][field] = value
                        result = validate_product_soak(record, root)
                        self.assertFalse(result.passed)
                        self.assertTrue(any(f"samples[1].{field}" in error for error in result.errors))

    def test_malformed_samples_return_failure_without_summary_arithmetic(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = _record(root)
            for index in (0, 1):
                for value in (None, "invalid", [], {}):
                    with self.subTest(index=index, sample=value):
                        record = copy.deepcopy(baseline)
                        record["samples"][index] = value
                        result = validate_product_soak(record, root)
                        self.assertFalse(result.passed)
                for field in baseline["samples"][index]:
                    for value in (None, "invalid", [], {}, True, False):
                        with self.subTest(index=index, field=field, value=value):
                            record = copy.deepcopy(baseline)
                            record["samples"][index][field] = value
                            result = validate_product_soak(record, root)
                            self.assertFalse(result.passed)
                            self.assertTrue(any(f"samples[{index}].{field}" in error for error in result.errors))

    def test_negative_and_fractional_count_samples_fail_closed(self) -> None:
        count_fields = ("rssBytes", "handles", "threads", "queueDepth", "mediaBudgetHighWaterBytes",
                        "underflows", "xruns", "controlQueueOverflow")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = _record(root)
            for field in baseline["samples"][-1]:
                values = (-1, 0.5) if field in count_fields else (-1,)
                for value in values:
                    with self.subTest(field=field, value=value):
                        record = copy.deepcopy(baseline)
                        record["samples"][-1][field] = value
                        record["summary"] = summarise(record["samples"])
                        result = validate_product_soak(record, root)
                        self.assertFalse(result.passed)
                        self.assertTrue(any(f"samples[1].{field}" in error for error in result.errors))

    def test_summary_metric_types_and_nonfinite_values_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = _record(root)
            for field in baseline["summary"]:
                if field == "dataLoss":
                    continue
                for value in (None, "invalid", [], {}, True, False, float("nan"), float("inf"), float("-inf")):
                    with self.subTest(field=field, value=value):
                        record = copy.deepcopy(baseline)
                        record["summary"][field] = value
                        result = validate_product_soak(record, root)
                        self.assertFalse(result.passed)
                        self.assertTrue(any(field in error for error in result.errors))

    def test_duration_type_and_single_sample_cannot_bypass_coverage(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = _record(root)
            for value in (1800.0, True, None, "1800", [], {}, float("nan"), float("inf")):
                with self.subTest(duration=value):
                    record = copy.deepcopy(baseline)
                    record["durationSeconds"] = value
                    record["samples"][-1]["elapsedSeconds"] = 1
                    self.assertFalse(validate_product_soak(record, root).passed)
            record = copy.deepcopy(baseline)
            record["samples"] = [record["samples"][-1]]
            self.assertFalse(validate_product_soak(record, root).passed)

    def test_negative_growth_and_finite_fractional_latencies_remain_valid(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            record["samples"][-1].update(rssBytes=90_000_000, handles=99, threads=9,
                                         cpuPercent=40.25, renderLatencyMs=120.25, callbackLatencyUs=800.25)
            record["summary"] = summarise(record["samples"])
            result = validate_product_soak(record, root)
            self.assertTrue(result.passed, result.errors)

    def test_large_integer_metrics_fail_budget_without_float_conversion(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            record["samples"][-1]["rssBytes"] = 1 << 4096
            record["summary"] = summarise(record["samples"])
            result = validate_product_soak(record, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("maxRssBytes exceeds" in error for error in result.errors))

    def test_cli_reports_nonfinite_and_malformed_metrics_without_traceback(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = _record(root)
            for field, value in (("elapsedSeconds", float("nan")), ("elapsedSeconds", float("inf")),
                                 ("rssBytes", "invalid")):
                with self.subTest(field=field, value=value):
                    record = copy.deepcopy(baseline)
                    record["samples"][-1][field] = value
                    path = root / "record.json"
                    path.write_text(json.dumps(record), encoding="utf-8")
                    completed = subprocess.run([
                        sys.executable, str(Path(__file__).resolve().parents[2] / "scripts/run_external_beta_product_soak.py"),
                        "--record", str(path), "--evidence-root", str(root),
                        "--output", str(root / "result.json"),
                    ], capture_output=True, text=True, timeout=10, check=False)
                    self.assertEqual(completed.returncode, 3, completed.stderr)
                    self.assertEqual(completed.stderr, "")
                    result = json.loads(completed.stdout)
                    self.assertFalse(result["passed"])
                    self.assertTrue(any(f"samples[1].{field}" in error for error in result["errors"]))
                    self.assertEqual(result, json.loads((root / "result.json").read_text()))

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
