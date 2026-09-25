"""Regression cases for the specification guard, not native rendering tests."""

import copy
import importlib.util
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "ui_fidelity_contract", ROOT / "scripts/verify_ui_fidelity_contract.py"
)
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)


def region(data, name):
    return next(item for item in data["canonical"]["regions"] if item["id"] == name)


class FidelityContractTests(unittest.TestCase):
    def setUp(self):
        self.contract = json.loads(CHECKER.DEFAULT_CONTRACT.read_text(encoding="utf-8"))

    def test_current_specification_does_not_claim_native_acceptance(self):
        result = CHECKER.validate(self.contract)
        self.assertEqual(result["specification_check"], "PASS", result["errors"])
        self.assertEqual(result["native_visual_match"], "NOT_RUN")
        self.assertEqual(result["host_verification"], "NOT_RUN")

    def test_incomplete_or_false_claims_are_rejected(self):
        cases = [
            ("reference hash", lambda d: d["references"][0].update(sha256="0" * 64)),
            ("reference size", lambda d: d["references"][0].update(size=[1600, 900])),
            ("crop", lambda d: d["references"][0].update(approximateFrameCrop=[0, 0, 9999, 900])),
            ("parent bounds", lambda d: region(d, "editor").update(rect=[-10, 108, 1112, 576])),
            ("overlap", lambda d: region(d, "ruler").update(rect=[24, 116, 100, 24])),
            ("parent cycle", lambda d: region(d, "header").update(parent="wordmark")),
            ("missing region", lambda d: d["canonical"]["regions"].pop()),
            ("empty geometry", lambda d: d["canonical"].update(regions=[])),
            ("tiny type", lambda d: d["typography"]["body"].update(points=6)),
            ("missing typography", lambda d: d.update(typography={})),
            ("memory", lambda d: d["rasterBudget"].update(retainedLimitMiB=80)),
            ("invalid scale", lambda d: d["canonical"].update(deviceScales=[0])),
            ("empty scales", lambda d: d["canonical"].update(deviceScales=[])),
            ("missing overlap coverage", lambda d: (
                d["canonical"].update(nonOverlappingGroups=[]),
                region(d, "tools").update(rect=[80, 148, 100, 24]))),
            ("misaligned musical axis", lambda d: region(d, "laneTimePlot").update(rect=[81, 740, 1039, 96])),
            ("missing musical axis", lambda d: d["canonical"].update(sharedTimeAxis=[])),
            ("false native pass", lambda d: d.update(nativeVisualMatch="PASS")),
            ("false evidence", lambda d: d["captureRequirements"].update(runtimeEvidencePresent=True)),
        ]
        for name, mutate in cases:
            with self.subTest(name=name):
                data = copy.deepcopy(self.contract)
                mutate(data)
                result = CHECKER.validate(data)
                self.assertEqual(result["specification_check"], "FAIL", name)
                self.assertEqual(result["native_visual_match"], "NOT_RUN", name)


if __name__ == "__main__":
    unittest.main()
