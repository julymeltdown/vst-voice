"""Tests for scripts/check_brand_terms.py and the shipped tree it guards (plan section 14.6)."""

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "check_brand_terms.py"
_spec = importlib.util.spec_from_file_location("check_brand_terms", SCRIPT)
assert _spec is not None and _spec.loader is not None
brand = importlib.util.module_from_spec(_spec)
sys.modules["check_brand_terms"] = brand
_spec.loader.exec_module(brand)


def write(root: Path, relative: str, text: str) -> Path:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


class TermMatching(unittest.TestCase):
    def test_word_boundaries_and_case(self) -> None:
        vans = brand.compile_term("Vans")
        self.assertIsNotNone(vans.search("plain VANS high-tops"))
        self.assertIsNotNone(vans.search("vans"))
        self.assertIsNone(vans.search("caravans and vanservice"))
        self.assertIsNone(brand.compile_term("UTAU").search("utaustyle"))

    def test_multi_word_terms_accept_separators_and_plurals(self) -> None:
        term = brand.compile_term("Fall Out Boy")
        for text in ("Fall Out Boy", "fall-out-boy", "FALL_OUT_BOY", "falloutboy"):
            self.assertIsNotNone(term.search(text), text)
        self.assertIsNotNone(brand.compile_term("Converse").search("two converses"))
        self.assertIsNone(brand.compile_term("Pro Tools").search("protoolsmith"))

    def test_punctuation_inside_terms(self) -> None:
        self.assertIsNotNone(brand.compile_term("Blink-182").search("blink 182 shirt"))
        self.assertIsNotNone(brand.compile_term("Dr. Martens").search("dr. martens boots"))


class Scanning(unittest.TestCase):
    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self.root = Path(self._dir.name)
        self.known = {term.text.lower() for term in brand.deny_terms()}

    def tearDown(self) -> None:
        self._dir.cleanup()

    def allowlist(self, entries: list[dict[str, object]]) -> list[object]:
        path = write(self.root, "allow.json", json.dumps({"schemaVersion": 1, "allow": entries}))
        return brand.load_allowlist(path, self.known)

    def test_string_table_asset_metadata_and_file_names_are_scanned(self) -> None:
        write(self.root, "libs/seam-native-ui/include/seam/native_ui/design/shell_strings.def",
              'SEAM_SHELL_STRING(Share, "Share to MySpace")\n')
        write(self.root, "assets/ui-design/manifest.json", '{"note": "Chuck Taylor style"}\n')
        write(self.root, "assets/ui-design/emo/nike-swoosh.txt", "ok\n")
        hits = brand.scan(self.root, [])
        found = {(hit.path.split("/")[-1], hit.term) for hit in hits}
        self.assertIn(("shell_strings.def", "Myspace"), found)
        self.assertIn(("manifest.json", "Chuck Taylor"), found)
        self.assertIn(("nike-swoosh.txt", "Nike"), found)
        name_hit = next(hit for hit in hits if hit.term == "Nike")
        self.assertEqual(name_hit.line, 0)

    def test_allowlist_is_scoped_to_its_paths(self) -> None:
        write(self.root, "docs/manual/USER_MANUAL.md", "Works as a VST3 plug-in.\n")
        write(self.root, "libs/seam-native-ui/include/seam/native_ui/design/shell_strings.def",
              'SEAM_SHELL_STRING(Format, "VST3 plug-in")\n')
        allowances = self.allowlist([{"term": "VST3", "paths": ["docs/manual/*.md"],
                                      "reason": "plug-in format name in the manual"}])
        hits = brand.scan(self.root, allowances)
        self.assertEqual([hit.path for hit in hits],
                         ["libs/seam-native-ui/include/seam/native_ui/design/shell_strings.def"])

    def test_allowlist_rejects_unknown_terms_and_missing_reasons(self) -> None:
        with self.assertRaises(ValueError):
            self.allowlist([{"term": "UnknownBrand", "paths": ["**"], "reason": "not on the deny-list"}])
        with self.assertRaises(ValueError):
            self.allowlist([{"term": "VST3", "paths": ["docs/*"], "reason": ""}])
        with self.assertRaises(ValueError):
            self.allowlist([{"term": "VST3", "paths": [], "reason": "a reason that is long"}])

    def test_unshipped_files_are_not_scanned(self) -> None:
        write(self.root, "README.md", "Tested in FL Studio.\n")
        write(self.root, "docs/design/PLAN.md", "FL Studio rows F02-F05\n")
        self.assertEqual(brand.scan(self.root, []), [])


class ShippedTree(unittest.TestCase):
    def test_repository_ships_no_brand_terms(self) -> None:
        self.assertEqual(brand.main(["--root", str(ROOT)]), 0)

    def test_the_gate_covers_strings_assets_manifests_installers_and_docs(self) -> None:
        files = {path.relative_to(ROOT).as_posix() for path in brand.shipped_files(ROOT)}
        for required in (
            "libs/seam-native-ui/include/seam/native_ui/design/shell_strings.def",
            "assets/ui-design/manifest.json",
            "assets/fonts/manifest.json",
            "assets/character-01/manifest.json",
            "packaging/macos/ProjectSEAM-App-Info.plist.in",
            "packaging/macos/Distribution.xml.in",
            "packaging/windows/ProjectSEAM.nsi",
            "packaging/release-resource-inventory.json",
            "docs/manual/USER_MANUAL.md",
            "THIRD_PARTY_NOTICES.md",
        ):
            self.assertIn(required, files)


if __name__ == "__main__":
    unittest.main()
