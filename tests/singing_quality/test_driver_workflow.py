from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.singing_quality.runner import RunSettings, run_corpus

ROOT = Path(__file__).resolve().parents[2]
CORPUS = ROOT / "tests/singing_quality/corpus/corpus.json"
DRIVER = os.environ.get("SEAM_SINGING_QUALITY_DRIVER", "")
ANALYZER = os.environ.get("SEAM_SINGING_QUALITY_ANALYZER", "")
BUILD_EVIDENCE = os.environ.get("SEAM_SINGING_QUALITY_BUILD_EVIDENCE", "")


@unittest.skipUnless(DRIVER and ANALYZER and BUILD_EVIDENCE,
                     "Native driver, analyzer and current build evidence were not supplied")
class DriverWorkflowTests(unittest.TestCase):
    def test_actual_workflow_saves_dry_audio_projects_and_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            packet = run_corpus(RunSettings(ROOT, CORPUS, Path(directory), Path(DRIVER),
                Path(ANALYZER), Path(BUILD_EVIDENCE), ROOT / "CMakeLists.txt"))
            inputs = json.loads((packet / "input-provenance.json").read_text())
            outputs = json.loads((packet / "output-provenance.json").read_text())
            self.assertEqual(hashlib.sha256(Path(DRIVER).read_bytes()).hexdigest(),
                             inputs["driver"]["sha256"])
            self.assertEqual(8, len(list((packet / "commands").glob("*.json"))))
            for artifact in outputs["artifacts"]:
                self.assertEqual(artifact["sha256"], hashlib.sha256(
                    (packet / artifact["path"]).read_bytes()).hexdigest())
            for case, expected_seconds in (("original-melody", 41.0), ("unequal-rests", 9.125)):
                for mode in ("bank", "raw"):
                    result = packet / (case + "-" + mode)
                    diagnostics = json.loads((result / "diagnostics.json").read_text())
                    measured = json.loads((result / "analysis/analysis.json").read_text())
                    saved = json.loads((result / "saved-project.seam").read_text())
                    self.assertEqual("com.project-seam.project", saved["formatId"])
                    self.assertAlmostEqual(expected_seconds, diagnostics["duration_seconds"], places=3)
                    self.assertGreater(measured["rms"], 0.0)
                    self.assertTrue(math.isfinite(measured["rms"]))
                    self.assertEqual(measured["frames"], diagnostics["frames"])
                    self.assertEqual(mode, diagnostics["renderer_policy"])
                    self.assertTrue(diagnostics["build"]["render_abi"])
                    self.assertTrue(diagnostics["build"]["compiler_id"])
                    self.assertTrue(diagnostics["phrases"])
                    for phrase in diagnostics["phrases"]:
                        self.assertTrue(phrase["resources"])
                        self.assertTrue(phrase["target_timing"])
                        self.assertTrue(phrase["rendered_placements"])
                        for placement in phrase["rendered_placements"]:
                            self.assertTrue(placement["requested_renderer"])
                            self.assertTrue(placement["actual_renderer"])
                            self.assertIsInstance(placement["used_fallback"], bool)
                            self.assertIn("diagnostic", placement)

    def test_driver_rejects_wrong_audio_hash_before_output(self) -> None:
        spec = json.loads(CORPUS.read_text())
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lock = root / "audio-lock.json"
            lock.write_text(json.dumps({"audio/human-vowel-demo.wav": "0" * 64}))
            output = root / "output"
            result = subprocess.run((DRIVER, str(ROOT / spec["cases"][0]["project"]),
                str(ROOT / spec["manifest"]), str(lock), str(output), "bank"),
                capture_output=True, text=True, timeout=30, check=False)
            self.assertEqual(3, result.returncode, result.stderr)
            self.assertFalse(output.exists())

    def test_driver_help_and_malformed_arguments_have_clear_exits(self) -> None:
        help_result = subprocess.run((DRIVER, "--help"), capture_output=True, text=True,
                                      timeout=30, check=False)
        self.assertEqual(0, help_result.returncode, help_result.stderr)
        for arguments in ((), ("project", "manifest", "lock", "output", "invalid")):
            result = subprocess.run((DRIVER, *arguments), capture_output=True, text=True,
                                     timeout=30, check=False)
            self.assertEqual(2, result.returncode)
            self.assertTrue(result.stderr.strip())

    def test_driver_rejects_malformed_project_before_output(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            project = root / "invalid.seam"
            project.write_text("{}", encoding="utf-8")
            output = root / "output"
            result = subprocess.run((DRIVER, str(project), "unused-manifest", "unused-lock",
                                     str(output), "bank"), capture_output=True, text=True,
                                     timeout=30, check=False)
            self.assertEqual(3, result.returncode)
            self.assertTrue(result.stderr.strip())
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
