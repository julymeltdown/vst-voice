import hashlib
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "phase13b"))

from common import GateResult  # noqa: E402
from product_gate import evaluate_product  # noqa: E402


def evidence(root: Path, name: str):
    path = root / "evidence" / f"{name}.txt"
    path.parent.mkdir(exist_ok=True)
    path.write_text(name, encoding="utf-8")
    return {
        "kind": name,
        "path": str(path.relative_to(root)),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "executedAt": "2026-08-18T00:00:00Z",
        "reviewer": "release-owner",
    }


class ProductGateTests(unittest.TestCase):
    def test_component_pass_is_insufficient_when_external_matrix_is_unresolved(self):
        matrix = {"targets": [{"id": "windows-runtime", "mandatory": True, "runtimeResult": "NOT_RUN", "evidence": []}]}
        result = evaluate_product(GateResult(True, []), GateResult(True, []), matrix)
        self.assertFalse(result["passed"])
        self.assertIn("windows-runtime", result["blockedTargets"])

    def test_product_passes_only_when_all_components_and_mandatory_targets_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            matrix = {"targets": [{"id": "windows-runtime", "mandatory": True, "runtimeResult": "PASS", "evidence": [evidence(root, "runtime")]}]}
            result = evaluate_product(GateResult(True, []), GateResult(True, []), matrix, root)
            self.assertTrue(result["passed"], result)

    def test_pass_result_without_evidence_root_fails_closed(self):
        matrix = {"targets": [{"id": "windows-runtime", "mandatory": True, "runtimeResult": "PASS", "evidence": [{"path": "evidence/x"}]}]}
        result = evaluate_product(GateResult(True, []), GateResult(True, []), matrix)
        self.assertFalse(result["passed"])
        self.assertIn("windows-runtime", result["blockedTargets"])


if __name__ == "__main__":
    unittest.main()
