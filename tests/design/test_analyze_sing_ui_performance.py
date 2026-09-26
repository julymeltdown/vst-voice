"""Guard against turning incomplete paint evidence into an apparent frame-time pass."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "analyze_sing_ui_performance", ROOT / "scripts/analyze_sing_ui_performance.py")
assert SPEC is not None and SPEC.loader is not None
ANALYZER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYZER)


def capture(p50: float, p95: float) -> dict:
    return {
        "schema": ANALYZER.CAPTURE_SCHEMA, "source": ANALYZER.PAINT_SOURCE,
        "frames": 101, "paintMillis": {"p50": p50, "p95": p95, "max": p95 + 4},
        "memory": {"physFootprintBytes": 104857600, "physFootprintPeakBytes": 115343360,
                   "scope": "whole standalone process (phys_footprint)"},
    }


class PaintEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.packet = Path(self.directory.name)
        self.performance = {
            "schema": ANALYZER.SCHEMA,
            "captures": {
                "emo-ready-1600x900": capture(18.0, 30.0),
                "scene-ready-720x480": capture(6.0, 8.0),
            },
        }
        self.manifest = {
            "candidate": "test-candidate", "deviceScale": 2,
            "captures": [
                {"id": "emo-ready-1600x900", "mode": "emo", "state": "ready", "viewport": [1600, 900]},
                {"id": "scene-ready-720x480", "mode": "scene", "state": "ready", "viewport": [720, 480]},
            ],
        }

    def write_packet(self):
        (self.packet / "performance.json").write_text(json.dumps(self.performance))
        (self.packet / "manifest.json").write_text(json.dumps(self.manifest))

    def test_reports_each_size_state_mode_without_claiming_end_to_end_performance(self):
        self.write_packet()
        report = ANALYZER.analyze(self.packet)
        self.assertEqual(report["captureCount"], 2)
        self.assertEqual(report["overBudgetCount"], 1)
        self.assertEqual(report["endToEndVerdict"], "NOT_MEASURED")
        self.assertEqual(report["phaseTimings"], "NOT_MEASURED")
        self.assertEqual(report["rows"][0]["viewport"], [720, 480])
        self.assertEqual(report["rows"][1]["paintBudget"], "OVER_BUDGET")
        self.assertEqual(report["rows"][1]["p95OverBudgetMs"], 13.3)
        self.assertEqual(report["rows"][1]["processPeakMiB"], 110.0)
        self.assertEqual(report["shortRunCount"], 0)
        rendered = ANALYZER.markdown(report)
        self.assertIn("1600×900 @2×", rendered)
        self.assertIn("30.0", rendered)
        self.assertIn("No per-phase timings or call graph are present", rendered)
        self.assertIn("AppKit presentation", rendered)

    def test_short_runs_are_identified(self):
        self.performance["captures"]["emo-ready-1600x900"]["frames"] = 16
        self.write_packet()
        report = ANALYZER.analyze(self.packet)
        self.assertEqual(report["shortRunCount"], 1)
        self.assertIn("16*", ANALYZER.markdown(report))

    def test_missing_capture_fails_closed(self):
        self.write_packet()
        del self.performance["captures"]["emo-ready-1600x900"]
        self.write_packet()
        with self.assertRaisesRegex(ANALYZER.EvidenceError, "missing or unsupported performance capture"):
            ANALYZER.analyze(self.packet)

    def test_extra_capture_fails_closed(self):
        self.performance["captures"]["scene-failed-1600x900"] = capture(1, 2)
        self.write_packet()
        with self.assertRaisesRegex(ANALYZER.EvidenceError, "absent from manifest"):
            ANALYZER.analyze(self.packet)

    def test_invalid_quantiles_and_source_are_rejected(self):
        item = self.performance["captures"]["emo-ready-1600x900"]
        for invalid in (float("nan"), float("inf"), -1.0, 50.0):
            with self.subTest(invalid=invalid):
                item["paintMillis"]["p95"] = invalid
                self.write_packet()
                with self.assertRaises(ANALYZER.EvidenceError):
                    ANALYZER.analyze(self.packet)
        item["paintMillis"]["p95"] = 30.0
        item["source"] = "unrelated benchmark"
        self.write_packet()
        with self.assertRaisesRegex(ANALYZER.EvidenceError, "unexpected timing source"):
            ANALYZER.analyze(self.packet)

    def test_unverified_scale_and_memory_scope_are_rejected(self):
        self.manifest.pop("deviceScale")
        self.write_packet()
        with self.assertRaisesRegex(ANALYZER.EvidenceError, "deviceScale"):
            ANALYZER.analyze(self.packet)
        self.manifest["deviceScale"] = 2
        self.performance["captures"]["scene-ready-720x480"]["memory"]["scope"] = "UI cache"
        self.write_packet()
        with self.assertRaisesRegex(ANALYZER.EvidenceError, "memory scope"):
            ANALYZER.analyze(self.packet)


if __name__ == "__main__":
    unittest.main()
