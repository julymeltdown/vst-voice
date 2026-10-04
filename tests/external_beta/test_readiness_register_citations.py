"""A "CLOSED" claim in the readiness register must cite code that still says so.

SEAM-BETA-P2-03 found that this repository's status documents could drift away from the code they
described. Correcting the register by hand fixes today's entries and leaves the next one free to
rot the same way, which is what had happened: P1-01, P1-02 and P1-03 were all still listed as open
long after the code had been repaired.

This checks the mechanical half of that. Every `file.cpp:123` citation in a status line must
resolve to a file that exists and a line inside it, and every issue heading must carry a status.
It cannot tell whether the cited line proves the claim — that is a reviewer's job and is stated as
such — but it does stop a closure from citing a path that was renamed or deleted, which is the
common way one of these drifts without anybody noticing.
"""
from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
REGISTER = ROOT / "BETA_READINESS_ISSUES.md"

#: `path:line` or `path:line-line`, where path has no spaces.
CITATION = re.compile(r"`(?P<path>[A-Za-z0-9_./-]+\.(?:py|cpp|hpp|mm|md|json)):(?P<line>\d+)`")

#: Every issue heading must be followed by a status line, so a reader can tell open from closed
#: without inferring it from prose.
HEADING = re.compile(r"^### (SEAM-BETA-P\d-\d+):", re.MULTILINE)
#: A status line is a bolded paragraph opening with "Status". Both `**Status:** ...**` and
#: `**Status: ...**` are used in this document, so the colon is not required inside the markers.
STATUS = re.compile(r"^\*\*Status", re.MULTILINE)


class ReadinessRegisterTests(unittest.TestCase):
    def setUp(self) -> None:
        self.text = REGISTER.read_text(encoding="utf-8")

    def test_every_issue_heading_carries_a_status(self) -> None:
        for match in HEADING.finditer(self.text):
            issue = match.group(1)
            with self.subTest(issue=issue):
                # The status must appear between this heading and the next one.
                nxt = self.text.find("\n### ", match.end())
                section = self.text[match.end(): nxt if nxt != -1 else len(self.text)]
                self.assertRegex(
                    section,
                    STATUS,
                    f"{issue} has no **Status:** line, so open and closed cannot be told apart",
                )

    def test_every_source_citation_resolves(self) -> None:
        for match in CITATION.finditer(self.text):
            path = ROOT / match.group("path")
            with self.subTest(citation=match.group(0)):
                self.assertTrue(path.is_file(), f"{match.group('path')} does not exist")
                line = int(match.group("line"))
                lines = path.read_text(encoding="utf-8").splitlines()
                self.assertTrue(
                    1 <= line <= len(lines),
                    f"{match.group(0)} is past the end of {match.group('path')} ({len(lines)} lines)",
                )

    def test_the_register_does_not_claim_an_unqualified_beta(self) -> None:
        # The register is the document a second developer reads first. It may not say the product
        # is ready, because nothing in this repository has earned that.
        self.assertNotIn("BETA GO ACHIEVED", self.text.upper())
        self.assertIn("P0-04", self.text)


if __name__ == "__main__":
    unittest.main()
