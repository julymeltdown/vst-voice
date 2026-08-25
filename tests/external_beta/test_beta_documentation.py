from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class BetaDocumentationTests(unittest.TestCase):
    def test_release_source_entry_points_resolve_to_canonical_documents(self) -> None:
        entries = {
            "EULA.md": "docs/manual/EULA.md",
            "PRIVACY.md": "docs/manual/PRIVACY.md",
            "QUICK_START.md": "docs/manual/QUICK_START.md",
            "USER_MANUAL.md": "docs/manual/USER_MANUAL.md",
            "KNOWN_LIMITATIONS.md": "docs/manual/KNOWN_LIMITATIONS.md",
            "UPDATE_AND_ROLLBACK.md": "docs/manual/UPDATE_AND_ROLLBACK.md",
            "SUPPORT.md": "docs/support/SUPPORT.md",
            "SECURITY_RESPONSE.md": "docs/support/SECURITY_RESPONSE.md",
            "BETA_TESTER_CHECKLIST.md": "docs/manual/BETA_TESTER_CHECKLIST.md",
        }
        for name, canonical in entries.items():
            entry = (ROOT / "docs/beta" / name).read_text(encoding="utf-8")
            target = (ROOT / canonical).read_text(encoding="utf-8")
            relative = Path(canonical).relative_to("docs").as_posix()
            self.assertIn(f"../{relative}", entry)
            self.assertTrue(target)


if __name__ == "__main__":
    unittest.main()
