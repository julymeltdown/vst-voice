from __future__ import annotations

import importlib.util
import hashlib
from pathlib import Path
import tempfile
from types import ModuleType
import unittest

from tools.public_release.contracts import JsonObject


ROOT = Path(__file__).resolve().parents[2]
VERIFIER = ROOT / "scripts/verify_public_windows_standalone_contract.py"


def _blocked_contract() -> JsonObject:
    return {
        "schemaVersion": 1,
        "gate": {"name": "Public Windows Standalone", "status": "BLOCKED"},
        "canonicalDocument": "docs/product/PUBLIC_WINDOWS_STANDALONE_ACCEPTANCE.md",
        "platform": "windows",
        "architecture": "x86_64",
        "candidateLineageId": None,
        "artifactRootSha256": None,
        "installedTreeSha256": None,
        "requirements": [
            {
                "id": f"PW-{index:03d}",
                "mandatory": True,
                "status": "NOT_RUN",
                "evidence": [],
            }
            for index in range(1, 21)
        ],
    }


class PublicWindowsStandaloneContractTests(unittest.TestCase):
    def _verifier(self) -> ModuleType:
        self.assertTrue(
            VERIFIER.is_file(),
            "the public Windows standalone verifier must be implemented",
        )
        spec = importlib.util.spec_from_file_location(
            "verify_public_windows_standalone_contract",
            VERIFIER,
        )
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_exact_pw_namespace_is_accepted_while_blocked(self) -> None:
        verifier = self._verifier()

        errors = verifier.verify_contract(_blocked_contract(), ROOT)

        self.assertEqual([], errors)

    def test_ua_row_cannot_satisfy_a_windows_requirement(self) -> None:
        verifier = self._verifier()
        contract = _blocked_contract()
        requirements = contract["requirements"]
        assert isinstance(requirements, list)
        row = requirements[0]
        assert isinstance(row, dict)
        row["id"] = "UA-001"

        errors = verifier.verify_contract(contract, ROOT)

        self.assertTrue(any("PW-001" in error for error in errors), errors)

    def test_pass_row_requires_real_windows_evidence_bytes(self) -> None:
        verifier = self._verifier()
        contract = _blocked_contract()
        requirements = contract["requirements"]
        assert isinstance(requirements, list)
        row = requirements[0]
        assert isinstance(row, dict)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence_path = root / "evidence/pw-001.json"
            evidence_path.parent.mkdir(parents=True)
            evidence_path.write_text('{"status":"PASS"}\n', encoding="utf-8")
            row["status"] = "PASS"
            contract["candidateLineageId"] = "public-lineage-001"
            contract["artifactRootSha256"] = "a" * 64
            contract["installedTreeSha256"] = "b" * 64
            row["evidence"] = [
                {
                    "recordId": "pw-record-001",
                    "requirementId": "PW-001",
                    "platform": "windows",
                    "architecture": "x86_64",
                    "surface": "standalone",
                    "status": "PASS",
                    "candidateLineageId": "public-lineage-001",
                    "artifactRootSha256": "a" * 64,
                    "installedTreeSha256": "b" * 64,
                    "operatorId": "windows-reviewer-001",
                    "machineProfileId": "windows-x64-clean-001",
                    "trustedTime": "2026-08-31T01:00:00Z",
                    "path": "evidence/pw-001.json",
                    "sha256": hashlib.sha256(evidence_path.read_bytes()).hexdigest(),
                }
            ]

            errors = verifier.verify_contract(contract, root)

            self.assertEqual([], errors)
            evidence = row["evidence"]
            assert isinstance(evidence, list)
            entry = evidence[0]
            assert isinstance(entry, dict)
            entry["sha256"] = "0" * 64
            mismatch = verifier.verify_contract(contract, root)
            self.assertTrue(any("sha256 does not match" in error for error in mismatch))

    def test_macos_ua_evidence_cannot_be_reused_for_pw(self) -> None:
        verifier = self._verifier()
        contract = _blocked_contract()
        requirements = contract["requirements"]
        assert isinstance(requirements, list)
        row = requirements[0]
        assert isinstance(row, dict)
        row["status"] = "PASS"
        row["evidence"] = [
            {
                "recordId": "ua-record-001",
                "requirementId": "UA-001",
                "platform": "macos",
                "architecture": "arm64",
                "surface": "standalone",
                "status": "PASS",
                "candidateLineageId": "public-lineage-001",
                "artifactRootSha256": "a" * 64,
                "installedTreeSha256": "b" * 64,
                "operatorId": "mac-reviewer-001",
                "machineProfileId": "macos-arm64-clean-001",
                "trustedTime": "2026-08-31T01:00:00Z",
                "path": "docs/product/usable-alpha-acceptance.json",
                "sha256": "0" * 64,
            }
        ]

        errors = verifier.verify_contract(contract, ROOT)

        self.assertTrue(any("cannot satisfy PW-001" in error for error in errors))
        self.assertTrue(any("platform must be windows" in error for error in errors))

    def test_pass_row_rejects_incomplete_target_identity(self) -> None:
        verifier = self._verifier()
        contract = _blocked_contract()
        requirements = contract["requirements"]
        assert isinstance(requirements, list)
        row = requirements[0]
        assert isinstance(row, dict)
        row["status"] = "PASS"
        row["evidence"] = [
            {
                "requirementId": "PW-001",
                "platform": "windows",
                "architecture": "x86_64",
                "candidateLineageId": "public-lineage-001",
                "artifactRootSha256": "a" * 64,
                "installedTreeSha256": "b" * 64,
                "path": "docs/product/usable-alpha-acceptance.json",
                "sha256": "0" * 64,
            }
        ]

        errors = verifier.verify_contract(contract, ROOT)

        self.assertTrue(any("recordId is required" in error for error in errors), errors)
        self.assertTrue(any("operatorId is required" in error for error in errors), errors)

    def test_passed_matrix_rejects_mixed_candidate_and_installation_identity(self) -> None:
        verifier = self._verifier()
        contract = _blocked_contract()
        contract["gate"] = {"name": "Public Windows Standalone", "status": "PASSED"}
        contract["candidateLineageId"] = "public-lineage-001"
        contract["artifactRootSha256"] = "a" * 64
        contract["installedTreeSha256"] = "b" * 64
        requirements = contract["requirements"]
        assert isinstance(requirements, list)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for index, row in enumerate(requirements, start=1):
                assert isinstance(row, dict)
                row_id = f"PW-{index:03d}"
                path = root / f"evidence/{row_id}.json"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('{"status":"PASS"}\n', encoding="utf-8")
                row["status"] = "PASS"
                row["evidence"] = [
                    {
                        "recordId": f"pw-record-{index:03d}",
                        "requirementId": row_id,
                        "platform": "windows",
                        "architecture": "x86_64",
                        "surface": "standalone",
                        "status": "PASS",
                        "candidateLineageId": "other-lineage" if index == 2 else "public-lineage-001",
                        "artifactRootSha256": "a" * 64,
                        "installedTreeSha256": "c" * 64 if index == 3 else "b" * 64,
                        "operatorId": f"windows-reviewer-{index:03d}",
                        "machineProfileId": "windows-x64-clean-001",
                        "trustedTime": "2026-08-31T01:00:00Z",
                        "path": f"evidence/{row_id}.json",
                        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                    }
                ]

            errors = verifier.verify_contract(contract, root)

        self.assertTrue(any("candidate lineage differs" in error for error in errors), errors)
        self.assertTrue(any("installed tree differs" in error for error in errors), errors)


if __name__ == "__main__":
    unittest.main()
