from __future__ import annotations

import hashlib
import tempfile
import unittest
from pathlib import Path

from tools.external_beta.candidate_root import (
    build_candidate_root,
    create_cohort_envelope,
    validate_candidate_root,
)
from tools.external_beta.freeze_candidate import freeze_candidate


def _digest(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


class CandidateRootContractTests(unittest.TestCase):
    def test_candidate_root_binds_platform_and_sidecar_digests(self) -> None:
        commit = "a" * 40
        manifests = [
            {
                "platform": platform,
                "status": "PASS",
                "signed": False,
                "sourceCommit": commit,
                "manifestSha256": _digest(platform + "manifest"),
                "treeSha256": _digest(platform + "tree"),
                "architecture": "arm64" if platform == "macos" else "x64",
            }
            for platform in ("macos", "windows")
            for _ in range(2)
        ]
        authorization = {
            "schemaVersion": 1,
            "status": "GO",
            "candidateSeed": "seed-001",
            "sourceCommit": commit,
            "bankSha256": _digest("bank"),
            "trustPolicySha256": _digest("trust"),
            "documentationSha256": _digest("docs"),
            "sbomSha256": _digest("sbom"),
            "predecessorSha256": _digest("predecessor"),
            "archiveSha256": _digest("archive"),
            "acceptanceContractSha256": _digest("external-beta-acceptance"),
            "archiveRestored": True,
            "signingCredentialsExcluded": True,
            "approvals": [{"role": role, "status": "APPROVED"} for role in ("A3", "A4", "A5", "A6")],
            "buildManifests": manifests,
            "releaseIdentity": {"version": "0.14.0", "buildId": "beta-001"},
        }
        freeze = freeze_candidate(authorization, manifests, set())
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = {}
            for name in ("bank", "macos", "windows", "trust", "docs", "archive"):
                path = root / name
                path.write_bytes(name.encode("utf-8"))
                files[name] = path
            candidate = build_candidate_root(
                freeze["candidateId"], freeze, files["bank"],
                {"macos": files["macos"], "windows": files["windows"]},
                files["trust"], files["docs"], files["archive"]
            )
            self.assertEqual(
                freeze["acceptanceContractSha256"],
                candidate["acceptanceContractSha256"],
            )
            self.assertEqual([], validate_candidate_root(candidate))
            envelope = create_cohort_envelope(
                candidate, "macos", {"ProjectSEAM.zip": files["macos"]}
            )
            self.assertEqual(candidate["candidateRoot"]["id"], envelope["candidateRootId"])
            candidate["candidateRoot"]["nodes"][0]["sha256"] = _digest("tampered")
            self.assertTrue(validate_candidate_root(candidate))


if __name__ == "__main__":
    unittest.main()
