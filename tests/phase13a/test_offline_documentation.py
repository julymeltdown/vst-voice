import json
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))

import documentation_contract  # noqa: E402


class OfflineDocumentationTests(unittest.TestCase):
    def test_required_documents_are_offline_and_hashable(self):
        manifest = json.loads((ROOT / "docs/product/external-beta-documentation.json").read_text(encoding="utf-8"))
        errors, result = documentation_contract.validate_documentation(ROOT, manifest)
        self.assertEqual([], errors)
        self.assertEqual(8, len(result["documents"]))
        self.assertTrue(all(len(value) == 64 for value in result["documents"].values()))

    def test_positive_commercial_claim_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for entry in json.loads((ROOT / "docs/product/external-beta-documentation.json").read_text(encoding="utf-8"))["requiredDocuments"]:
                source = ROOT / entry["path"]
                destination = root / entry["path"]
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_text(source.read_text(encoding="utf-8"), encoding="utf-8")
            manifest = json.loads((ROOT / "docs/product/external-beta-documentation.json").read_text(encoding="utf-8"))
            (root / "docs/manual/EULA.md").write_text((root / "docs/manual/EULA.md").read_text(encoding="utf-8") + "\nGeneral Availability is available now.\n", encoding="utf-8")
            errors, _ = documentation_contract.validate_documentation(root, manifest)
            self.assertTrue(any("unsupported commercial/GA claim" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
