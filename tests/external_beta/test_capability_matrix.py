"""The capability report may only state refusals the code actually raises.

Every entry cites a file and line. This checks the citation, so the report cannot drift away
from the code it describes: if a refusal is removed, renamed or moved, the citation stops
matching and this fails rather than leaving a stale capability claim in the repository.

It also checks the thing that matters most: that the report never claims positive coverage.
A capability matrix that asserted support with nothing behind it would be worse than none.
"""
from __future__ import annotations

import unittest
from pathlib import Path

from tools.external_beta.capability_matrix import (
    MODELLED_RESOURCE_KINDS,
    REFUSALS,
    REFUSED_CAPABILITIES,
    UNMODELLED_CONTRACT_KINDS,
    capability_report,
    refusals_for,
)


ROOT = Path(__file__).resolve().parents[2]


class CapabilityMatrixTests(unittest.TestCase):
    def test_every_refusal_is_raised_and_worded_where_the_report_says(self) -> None:
        for refusal in REFUSALS:
            with self.subTest(capability=refusal.capability, carrier=refusal.carrier):
                # The refusal site must raise an Unsupported failure for this code path.
                raised = self.window(refusal.refused_at)
                self.assertIn("ErrorCode::Unsupported", raised, refusal.refused_at)
                # The message site must actually contain the capability this report names.
                worded = self.window(refusal.worded_at)
                self.assertIn(refusal.refusal, worded, f"{refusal.worded_at}: {refusal.refusal!r}")

    @staticmethod
    def window(citation: str, radius: int = 4) -> str:
        relative, _, line_text = citation.rpartition(":")
        path = ROOT / relative
        if not path.is_file():
            raise AssertionError(f"{citation}: {relative} is not a file")
        lines = path.read_text(encoding="utf-8").splitlines()
        index = int(line_text) - 1
        if not 0 <= index < len(lines):
            raise AssertionError(f"{citation} is past the end of {relative}")
        return "\n".join(lines[max(0, index - radius): index + radius + 1])

    def test_the_report_never_claims_positive_coverage(self) -> None:
        report = capability_report()
        self.assertIs(report["positiveCoverageClaimed"], False)
        self.assertEqual(report["matrixStatus"], "REFUSALS_ONLY")
        # The contract's own gate must still read unresolved; this module does not resolve it.
        self.assertIn("releasedResources", report["why"])
        self.assertIn("unmodelledContractKinds", report)

    def test_absent_combinations_are_not_claimed_to_work(self) -> None:
        self.assertIn("not claimed to work", capability_report()["absentMeans"])

    def test_unmodelled_contract_kinds_stay_visible(self) -> None:
        # These have no C++ representation at all. Dropping them would quietly shrink the
        # contract's six kinds to the three the code happens to model.
        self.assertEqual(set(UNMODELLED_CONTRACT_KINDS), {"dictionary-original", "character-original"})
        self.assertEqual(set(MODELLED_RESOURCE_KINDS), {"sample", "procedural", "neural"})

    def test_refusal_lookups_are_stable(self) -> None:
        self.assertEqual(len(REFUSALS), len({(r.capability, r.carrier) for r in REFUSALS}))
        self.assertTrue(refusals_for("neural"))
        self.assertEqual(refusals_for("nonexistent-carrier"), ())
        self.assertEqual(refusals_for("all"), REFUSALS)
        self.assertIn("formant", REFUSED_CAPABILITIES)


if __name__ == "__main__":
    unittest.main()
