"""The status page may only claim a state its evidence supports.

SEAM-BETA-P2-03 records the defect this file closes: several documents mix contract validity,
source implementation, target-machine pass and beta readiness, so a string like
`*_CONTRACT=PASS` reads as product acceptance when it is not. The page is only worth having if
it cannot make the same mistake in a new place, so most of what follows checks that it understates
rather than overstates. A test that only confirmed today's values would pass just as happily if
every value were raised.
"""
from __future__ import annotations

import json
import unittest
from pathlib import Path

from tools.external_beta.release_status import (
    STATES,
    Row,
    build_rows,
    state_rank,
    status_report,
)


ROOT = Path(__file__).resolve().parents[2]


class ReleaseStatusTests(unittest.TestCase):
    def test_states_are_ordered_weakest_first(self) -> None:
        self.assertEqual(
            STATES,
            ("CONTRACT_VALID", "IMPLEMENTED", "TARGET_PASS", "BETA_READY"),
        )
        self.assertEqual(state_rank("CONTRACT_VALID"), 0)
        self.assertEqual(state_rank("BETA_READY"), 3)
        # An unrecognised state has no rank at all, so it can never be compared as if valid.
        self.assertEqual(state_rank("SHIPPED"), -1)

    def test_every_row_names_evidence_and_what_blocks_it(self) -> None:
        for row in build_rows():
            with self.subTest(claim=row.claim):
                self.assertTrue(row.evidence, "a bare claim with no evidence is the defect")
                self.assertLessEqual(state_rank(row.state), state_rank("BETA_READY"))
                # CONTRACT_VALID is a ceiling: it claims only that a requirement is written
                # down, so a row there has nothing further to be blocked from.
                if row.state not in ("CONTRACT_VALID", "BETA_READY"):
                    self.assertTrue(
                        row.blocked_on,
                        "a row below BETA_READY must say what is missing",
                    )

    def test_a_row_cannot_claim_an_unknown_state(self) -> None:
        with self.assertRaises(ValueError):
            Row(claim="x", state="SHIPPED", evidence=("y",), blocked_on="z")
        # A row with no evidence at all is refused even though its state name is valid.
        with self.assertRaises(ValueError):
            Row(claim="x", state="IMPLEMENTED", evidence=(), blocked_on="z")

    def test_a_row_below_beta_ready_cannot_omit_the_blocker(self) -> None:
        # This is the check that stops a row quietly reading as further along than it is.
        with self.assertRaises(ValueError):
            Row(claim="x", state="TARGET_PASS", evidence=("y",))
        # A CONTRACT_VALID row is at its ceiling: claiming only that a requirement is written
        # down is the whole of that state, so it is not required to name a blocker.
        Row(claim="x", state="CONTRACT_VALID", evidence=("y",))

    def test_the_page_reports_no_beta_readiness(self) -> None:
        report = status_report()
        self.assertIs(report["betaReady"], False)
        self.assertIn("No row is BETA_READY", str(report["betaReadyWhy"]))
        # Nothing may claim BETA_READY while the page says the product is not ready.
        for row in report["rows"]:
            self.assertNotEqual(row["state"], "BETA_READY", row["claim"])

    def test_the_journey_row_is_read_from_the_acceptance_matrix_not_asserted(self) -> None:
        # P0-04's number must be derived, so it cannot drift away from the matrix it summarises.
        # The expectations below are pinned to the matrix's own recorded state rather than to
        # another run of the same reader: comparing the page against a second implementation of
        # the same rule would let a hardcoded number satisfy both sides. An earlier version of
        # this case did exactly that and passed a page claiming 20/20 while the matrix says 0/20.
        document = json.loads(
            (ROOT / "docs/product/usable-alpha-acceptance.json").read_text(encoding="utf-8")
        )
        requirements = document["requirements"]
        # As recorded today: every canonical row is NOT_RUN and none carries evidence, and the
        # document's own gate reads BLOCKED. If a physical run is ever accepted, these change
        # together with the matrix, which is the point.
        self.assertEqual(len(requirements), 20)
        self.assertEqual(document["gate"]["status"], "BLOCKED")
        self.assertEqual(
            [r["status"] for r in requirements],
            ["NOT_RUN"] * 20,
            "a canonical row changed state; update this case with the evidence it now carries",
        )
        self.assertEqual([r["evidence"] for r in requirements], [[]] * 20)
        row = next(r for r in status_report()["rows"] if "musician journey" in r["claim"])
        # Matched as a phrase, not a substring: "20 of 20" contains "0 of 20", so a substring
        # check passed a page claiming every canonical row was complete while the matrix records
        # none. A hardcoded 20/20 was tried against this case and passed it.
        self.assertRegex(row["blockedOn"], r"(?<!\d)0 of 20(?!\d)")
        self.assertNotRegex(row["blockedOn"], r"(?<!\d)20 of 20(?!\d)")

    def test_every_named_evidence_path_exists(self) -> None:
        # A citation that names a file which is not there is worse than no citation.
        for row in status_report()["rows"]:
            for evidence in row["evidence"]:
                path = evidence.split(" ", 1)[0]
                with self.subTest(evidence=evidence):
                    self.assertTrue(
                        (ROOT / path).exists(), f"{row['claim']}: {path} does not exist"
                    )

    def test_absent_rows_are_not_claimed_to_pass(self) -> None:
        self.assertIn("not claimed to pass", str(status_report()["absentMeans"]))


if __name__ == "__main__":
    unittest.main()
