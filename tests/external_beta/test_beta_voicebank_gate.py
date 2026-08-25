from __future__ import annotations

import copy
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.external_beta.voicebank_gate import REQUIRED_GATE_IDS, evaluate_beta_voicebank_dossier  # noqa: E402


def _write_hashed(root: Path, relative: str, content: bytes) -> str:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)
    return hashlib.sha256(content).hexdigest()


def _evidence(root: Path, name: str) -> dict[str, str]:
    relative = f"evidence/{name}.json"
    digest = _write_hashed(root, relative, json.dumps({"evidence": name}, sort_keys=True).encode("utf-8"))
    return {
        "kind": name,
        "path": relative.removeprefix("evidence/"),
        "sha256": digest,
        "executedAt": "2026-08-21T12:00:00Z",
        "reviewer": "beta-reviewer",
        "result": "PASS",
    }


def _valid_dossier(root: Path) -> dict:
    package_digest = _write_hashed(root, "packages/beta-voicebank-01.seambank", b"beta-package-bytes")
    source_digest = _write_hashed(root, "assets/source/source.wav", b"immutable-source")
    derived_digest = _write_hashed(root, "assets/derived/unit.wav", b"derived-unit")
    gates = {gate: {"status": "PASS", "evidence": [_evidence(root, gate)]} for gate in REQUIRED_GATE_IDS}
    receipt = _evidence(root, "reference-song-render")
    return {
        "schemaVersion": 1,
        "component": "beta-voicebank",
        "status": "ACCEPTED",
        "voicebankId": "beta.voice.01",
        "version": "0.1.0",
        "displayName": "Project SEAM Beta Voicebank 01",
        "official": False,
        "characterAssociated": False,
        "characterId": None,
        "evidenceRoot": "evidence",
        "installedProvenanceTreeSha256": "f" * 64,
        "package": {
            "id": "beta.voice.01",
            "version": "0.1.0",
            "contentSha256": package_digest,
            "entryManifestSha256": "e" * 64,
            "file": {"path": "packages/beta-voicebank-01.seambank", "sha256": package_digest},
            "signature": {
                "purpose": "BANK_PACKAGE",
                "delegatedKeyId": "beta-bank-key-01",
                "delegatedKeyFingerprint": "a" * 64,
                "algorithm": "ed25519",
                "signedAt": "2026-08-02T12:00:00Z",
                "trustEpoch": 7,
                "signatureSha256": "b" * 64,
            },
        },
        "trust": {
            "rootPolicyVersion": "external-beta-1",
            "rootKeyId": "offline-root-01",
            "rootKeyFingerprint": "c" * 64,
            "epoch": 7,
            "validFrom": "2026-01-01T00:00:00Z",
            "validUntil": "2026-12-31T23:59:59Z",
            "compromiseCutoff": "2025-12-31T23:59:59Z",
            "delegatedKeyId": "beta-bank-key-01",
            "delegatedKeyFingerprint": "a" * 64,
            "delegatedKeyPurpose": "BANK_PACKAGE",
            "rootSignedDelegation": True,
            "rootSignedDelegationSha256": "d" * 64,
            "delegatedKeyRevoked": False,
            "revalidation": {
                "result": "PASS",
                "trustEpoch": 7,
                "signedEntryManifestSha256": "e" * 64,
                "installedProvenanceTreeSha256": "f" * 64,
                "installedTreeLinkCount": 0,
            },
        },
        "inventory": {
            "language": "ja",
            "profileId": "beta-ja-cvvc-v1",
            "supportedStyles": ["original"],
            "requiredUnits": ["a", "ka", "ak"],
            "pitchLayers": [60, 72],
            "supportedRange": {"minMidi": 60, "maxMidi": 72, "method": "comfortable-range-test-v1", "result": "PASS"},
            "coverageResult": "PASS",
        },
        "rights": {
            "providerIdentityDisclosure": "WITHHELD",
            "approval": {
                "reviewer": "rights-reviewer",
                "reviewedAt": "2026-08-03T12:00:00Z",
                "scope": ["recording", "transformation", "redistribution", "rendered-audio"],
                "territory": ["worldwide"],
                "status": "APPROVED",
                "redactedApprovalSha256": "1" * 64,
            },
            "permissions": {
                "recordingSourceUse": True,
                "transformation": True,
                "redistribution": True,
                "endUserRenderedAudio": True,
            },
            "publicLicenseSummary": {
                "status": "APPROVED",
                "spdx": "LicenseRef-SEAM-BETA-VOICE",
                "summary": "Rights-cleared local singing voicebank for invited External Beta users.",
            },
        },
        "sourceAssets": [{"path": "assets/source/source.wav", "sha256": source_digest}],
        "derivedAssets": [{"path": "assets/derived/unit.wav", "sha256": derived_digest}],
        "referenceSong": {
            "projectSha256": "2" * 64,
            "mediaSha256": "3" * 64,
            "renderReceipt": receipt,
        },
        "gates": gates,
    }


class BetaVoicebankGateTests(unittest.TestCase):
    def test_complete_rights_cleared_beta_dossier_passes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = evaluate_beta_voicebank_dossier(_valid_dossier(root), root)
            self.assertTrue(result.passed, result.errors)
            self.assertEqual("ACCEPTED", result.release_status)

    def test_missing_or_ambiguous_rights_is_blocked(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dossier = _valid_dossier(root)
            dossier["rights"]["permissions"]["redistribution"] = False
            dossier["rights"]["approval"]["status"] = "NOT_RUN"
            result = evaluate_beta_voicebank_dossier(dossier, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("redistribution" in error or "approval" in error for error in result.errors))

    def test_official_or_character_associated_bank_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dossier = _valid_dossier(root)
            dossier["official"] = True
            dossier["characterAssociated"] = True
            dossier["characterId"] = "character.01"
            result = evaluate_beta_voicebank_dossier(dossier, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("official" in error for error in result.errors))
            self.assertTrue(any("character" in error for error in result.errors))

    def test_placeholder_or_failed_evidence_cannot_pass(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dossier = _valid_dossier(root)
            dossier["gates"]["hostile-package-validation"]["status"] = "NOT_RUN"
            dossier["gates"]["hostile-package-validation"]["evidence"] = []
            result = evaluate_beta_voicebank_dossier(dossier, root)
            self.assertFalse(result.passed)
            self.assertIn("hostile-package-validation", result.blocked)

    def test_evidence_path_escape_and_hash_tamper_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dossier = _valid_dossier(root)
            dossier["gates"]["rights-approval"]["evidence"][0]["path"] = "../outside.json"
            result = evaluate_beta_voicebank_dossier(dossier, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("safe relative" in error or "escape" in error for error in result.errors))
            dossier = _valid_dossier(root)
            evidence_path = root / "evidence" / "signed-package.json"
            evidence_path.write_text("tampered", encoding="utf-8")
            result = evaluate_beta_voicebank_dossier(dossier, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("does not match" in error for error in result.errors))

    def test_private_contract_material_is_not_accepted_in_public_dossier(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dossier = _valid_dossier(root)
            dossier["rights"]["privateContractPath"] = "private/contract.pdf"
            result = evaluate_beta_voicebank_dossier(dossier, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("private" in error for error in result.errors))

    def test_cross_purpose_signature_and_replayed_epoch_fail(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dossier = _valid_dossier(root)
            dossier["package"]["signature"]["purpose"] = "UPDATE_METADATA"
            dossier["package"]["signature"]["trustEpoch"] = 6
            result = evaluate_beta_voicebank_dossier(dossier, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("purpose" in error for error in result.errors))
            self.assertTrue(any("epoch" in error for error in result.errors))

    def test_receipt_key_without_root_attestation_is_not_trust(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dossier = _valid_dossier(root)
            dossier["trust"]["rootSignedDelegation"] = False
            result = evaluate_beta_voicebank_dossier(dossier, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("offline root" in error for error in result.errors))

    def test_phase13b_official_fixture_is_not_substitutable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dossier = _valid_dossier(root)
            dossier["voicebankId"] = "official.voice.01"
            dossier["package"]["id"] = "official.voice.01"
            result = evaluate_beta_voicebank_dossier(dossier, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("non-official" in error for error in result.errors))

    def test_valid_dossier_is_not_mutated_by_gate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dossier = _valid_dossier(root)
            original = copy.deepcopy(dossier)
            evaluate_beta_voicebank_dossier(dossier, root)
            self.assertEqual(original, dossier)


if __name__ == "__main__":
    unittest.main()
