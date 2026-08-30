from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import unittest

from tools.public_release.contracts import PUBLIC_REQUIREMENT_IDS, PUBLIC_STATES


ROOT = Path(__file__).resolve().parents[2]


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class PublicReleaseSourceContractTests(unittest.TestCase):
    def test_u53_artifacts_exist_and_checked_in_snapshots_are_blocked(self) -> None:
        required = (
            "docs/product/PUBLIC_RELEASE_ACCEPTANCE.md",
            "docs/product/public-release-acceptance.json",
            "docs/product/PUBLIC_RELEASE_RUNBOOK.md",
            "docs/product/PUBLIC_WINDOWS_STANDALONE_ACCEPTANCE.md",
            "docs/product/public-windows-standalone-acceptance.json",
            "docs/product/public-windows-standalone-evidence.schema.json",
            "docs/public/EULA.md",
            "docs/public/PRIVACY.md",
            "docs/public/SUPPORT.md",
            "docs/public/SECURITY_RESPONSE.md",
            "tools/public_release/__init__.py",
            "tools/public_release/release_gate.py",
            "scripts/run_public_release_audit.py",
            "scripts/verify_public_windows_standalone_contract.py",
        )
        for relative in required:
            self.assertTrue((ROOT / relative).is_file(), relative)
        public = json.loads(
            (ROOT / "docs/product/public-release-acceptance.json").read_text(
                encoding="utf-8"
            )
        )
        windows = json.loads(
            (
                ROOT
                / "docs/product/public-windows-standalone-acceptance.json"
            ).read_text(encoding="utf-8")
        )
        self.assertEqual("BLOCKED", public["status"])
        self.assertEqual([], public["evidence"])
        self.assertEqual([], public["approvalPolicy"]["trustedKeys"])
        self.assertEqual([], public["operationPolicy"]["trustedKeys"])
        self.assertEqual(list(PUBLIC_STATES), public["states"])
        self.assertEqual(
            list(PUBLIC_REQUIREMENT_IDS),
            [item["id"] for item in public["requirements"]],
        )
        self.assertEqual("BLOCKED", windows["gate"]["status"])
        self.assertIsNone(windows["candidateLineageId"])
        self.assertIsNone(windows["artifactRootSha256"])
        self.assertIsNone(windows["installedTreeSha256"])
        self.assertEqual(
            ["NOT_RUN"] * 20,
            [item["status"] for item in windows["requirements"]],
        )

    def test_public_document_versions_and_digests_are_separate_from_beta(self) -> None:
        contract_path = ROOT / "docs/product/public-release-acceptance.json"
        self.assertTrue(contract_path.is_file(), str(contract_path))
        contract = json.loads(contract_path.read_text(encoding="utf-8"))
        documents = contract["publicDocuments"]
        self.assertEqual(
            {
                "project-seam.public.eula",
                "project-seam.public.privacy",
                "project-seam.public.support",
                "project-seam.public.security-response",
            },
            {item["id"] for item in documents},
        )
        for item in documents:
            path = ROOT / item["path"]
            self.assertTrue(path.is_file(), item["path"])
            self.assertEqual(_sha256(path), item["sha256"])
            self.assertTrue(item["version"].startswith("public-"))
            self.assertEqual("DRAFT", item["approvalStatus"])
            self.assertIn(
                "DRAFT / NOT APPROVED FOR DISTRIBUTION",
                path.read_text(encoding="utf-8"),
            )
        self.assertIn(
            "qualified legal counsel",
            (ROOT / "docs/public/EULA.md").read_text(encoding="utf-8"),
        )
        beta = json.loads(
            (ROOT / "docs/product/external-beta-documentation.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(
            [
                ("eula", "external-beta-eula-1.0"),
                ("privacy", "external-beta-privacy-1.0"),
                ("manual", "external-beta-manual-1.0"),
                ("limitations", "external-beta-limitations-1.0"),
                ("update", "external-beta-update-rollback-1.0"),
                ("checklist", "external-beta-checklist-1.0"),
                ("support", "external-beta-support-1.0"),
                ("security", "external-beta-security-response-1.0"),
            ],
            [(item["id"], item["version"]) for item in beta["requiredDocuments"]],
        )

    def test_runbook_links_and_status_tokens_are_repository_relative(self) -> None:
        runbook = (
            ROOT / "docs/product/EXTERNAL_BETA_RUNBOOK.md"
        ).read_text(encoding="utf-8")
        self.assertNotIn("/Users/", runbook)
        links = re.findall(r"\[[^]]+\]\(([^)]+)\)", runbook)
        self.assertTrue(links)
        self.assertTrue(all(link.startswith("../../scripts/") for link in links), links)
        public_runbook = (
            ROOT / "docs/product/PUBLIC_RELEASE_RUNBOOK.md"
        ).read_text(encoding="utf-8")
        for token in (
            "EXTERNAL_BETA_CLOSED",
            "PUBLIC_ACTIVE",
            "DISTRIBUTION_PAUSED",
            "REVOKED",
            "EvidenceRoot",
        ):
            self.assertIn(token, public_runbook)
        for relative in (
            "docs/STATUS.md",
            "docs/RELEASE_READINESS.md",
            "docs/RELEASE_READINESS_KO.md",
        ):
            text = (ROOT / relative).read_text(encoding="utf-8")
            self.assertIn("PUBLIC_ACTIVE", text, relative)
            self.assertIn("BLOCKED", text, relative)
            self.assertIn("PW-001", text, relative)


if __name__ == "__main__":
    unittest.main()
