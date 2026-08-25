from __future__ import annotations

import copy
import hashlib
import tempfile
import unittest
from pathlib import Path

from tools.external_beta.release_gate_validation import JsonObject
from tools.external_beta.candidate_root import build_candidate_root, create_cohort_envelope, validate_candidate_root
from tools.external_beta.freeze_candidate import freeze_candidate, issue_candidate_id


def _digest(label: str) -> str:
    return hashlib.sha256(label.encode("utf-8")).hexdigest()


def _manifest(platform: str, commit: str) -> JsonObject:
    return {
        "platform": platform,
        "status": "PASS",
        "signed": False,
        "sourceCommit": commit,
        "manifestSha256": _digest(platform + "-manifest"),
        "treeSha256": _digest(platform + "-tree"),
        "architecture": "arm64" if platform == "macos" else "x64",
    }


def _authorization() -> JsonObject:
    commit = "a" * 40
    builds = [_manifest("macos", commit), _manifest("macos", commit), _manifest("windows", commit), _manifest("windows", commit)]
    return {
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
        "buildManifests": builds,
        "releaseIdentity": {"version": "0.14.0", "buildId": "beta-001"},
    }


class CandidateFreezeTests(unittest.TestCase):
    def test_freeze_issues_stable_candidate_and_rejects_reuse(self) -> None:
        authorization = _authorization()
        record = freeze_candidate(authorization, authorization["buildManifests"], set())
        self.assertEqual("FROZEN_UNSIGNED", record["status"])
        self.assertEqual(record["candidateId"], issue_candidate_id(authorization))
        with self.assertRaises(ValueError):
            issue_candidate_id(authorization, {record["candidateId"]})

    def test_independent_build_mismatch_blocks_freeze(self) -> None:
        authorization = _authorization()
        changed = copy.deepcopy(authorization["buildManifests"])
        changed[1]["treeSha256"] = _digest("different")
        with self.assertRaises(ValueError):
            freeze_candidate(authorization, changed, set())

    def test_freeze_binds_the_acceptance_contract_digest(self) -> None:
        authorization = _authorization()
        record = freeze_candidate(
            authorization,
            authorization["buildManifests"],
            set(),
        )
        self.assertEqual(
            authorization["acceptanceContractSha256"],
            record["acceptanceContractSha256"],
        )

    def test_missing_acceptance_contract_digest_blocks_freeze(self) -> None:
        authorization = _authorization()
        del authorization["acceptanceContractSha256"]
        with self.assertRaises(ValueError):
            freeze_candidate(
                authorization,
                authorization["buildManifests"],
                set(),
            )


class CandidateRootTests(unittest.TestCase):
    def test_root_binds_all_platform_and_bank_bytes(self) -> None:
        authorization = _authorization()
        freeze = freeze_candidate(authorization, authorization["buildManifests"], set())
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = {}
            for name in ("bank", "macos", "windows", "trust", "docs", "archive"):
                path = root / name
                path.write_bytes(name.encode("utf-8"))
                files[name] = path
            candidate = build_candidate_root(
                freeze["candidateId"],
                freeze,
                files["bank"],
                {"macos": files["macos"], "windows": files["windows"]},
                files["trust"],
                files["docs"],
                files["archive"],
            )
            self.assertEqual([], validate_candidate_root(candidate))
            envelope = create_cohort_envelope(
                candidate, "macos", {"ProjectSEAM.zip": files["macos"], "bank.seambank": files["bank"]}
            )
            self.assertEqual(candidate["candidateRoot"]["id"], envelope["candidateRootId"])
            self.assertEqual(2, len(envelope["members"]))
            candidate["candidateRoot"]["nodes"][0]["sha256"] = _digest("tampered")
            self.assertTrue(validate_candidate_root(candidate))


if __name__ == "__main__":
    unittest.main()
