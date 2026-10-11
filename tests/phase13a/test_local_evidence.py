"""Construction fixtures prove labels; they are not executed release evidence."""
import copy
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.generate_phase13a_evidence import build_evidence, Phase13AEvidenceError
import release_gate


class LocalEvidenceTests(unittest.TestCase):
    def test_current_source_summary_and_matrix_keep_deferred_ci_and_blocked_gates(self):
        matrix = release_gate.load_matrix(ROOT / "docs/phase13a/mandatory-validation-matrix.json")
        gates = {gate: release_gate.evaluate_matrix(matrix, gate).as_dict()
                 for gate in ("G2", "G3", "G4", "G5")}
        local = dict.fromkeys(("contractTests", "sourceContract", "dependencyLock",
                              "linuxUnsignedDeveloperPackage", "linuxInstallUninstallSmoke"), "PASS")
        manifest = {"fixture": True}
        summary = build_evidence(package=Path("fixture.zip"), package_manifest=manifest,
                                 gate_results=gates, local_executed=local)
        self.assertEqual(summary["implementationState"], "SOURCE_READY")
        self.assertEqual(summary["acceptanceStatus"], "BLOCKED")
        self.assertEqual(summary["developerPackage"], {"path": "fixture.zip", "manifest": manifest})
        self.assertEqual(summary["localExecuted"], local)
        self.assertTrue(all(value == "NOT_RUN" for value in summary["externalMandatory"].values()))
        self.assertIn("CI_CONFIGURED", matrix["implementationStateValues"])
        self.assertFalse(any(row["implementationState"] == "CI_CONFIGURED" for row in matrix["targets"]))
        for result in gates.values():
            self.assertFalse(result["passed"])
            self.assertEqual(result["errors"], [])
        for key in local:
            for value in ("FAIL", "NOT_RUN", None):
                changed = copy.copy(local); changed[key] = value
                with self.subTest(key=key, value=value), self.assertRaises(Phase13AEvidenceError):
                    build_evidence(package=Path("fixture.zip"), package_manifest=manifest,
                                   gate_results=gates, local_executed=changed)
        with self.assertRaises(Phase13AEvidenceError):
            build_evidence(package=Path("fixture.zip"), package_manifest=manifest,
                           gate_results=gates, local_executed={})
