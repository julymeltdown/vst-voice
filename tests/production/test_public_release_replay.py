from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from tests.production.public_release_replay_fixtures import public_replay_fixture, write_reference
from tests.production.public_release_contract_fixtures import APPROVAL_ROLES, approval, sign_operation
from tests.production.public_release_fixtures import candidate, acceptance_contract
from tools.public_release import release_gate
from tools.public_release.release_audit import audit_release

ROOT = Path(__file__).resolve().parents[2]


def decision(snapshot, action, identifier, contract, **extra):
    value = {"schemaVersion": 1, "decisionId": identifier, "action": action,
        "candidateLineageId": snapshot["candidateLineageId"], "evidenceRootSha256": snapshot["evidenceRootSha256"],
        "actorId": "release-operator-001", "actorRole": "release-manager", "createdAt": "2026-08-31T03:00:00Z",
        "acceptanceContractSha256": release_gate.sha256_json(contract), **extra}
    sign_operation(value, "decisionSha256")
    return value


class PublicReplayTests(unittest.TestCase):
    def test_direct_entry_booleans_and_shallow_closed_summary_do_not_pass(self):
        contract = acceptance_contract()
        result = release_gate.evaluate_gate(candidate(contract), acceptance_contract=contract, archive_verified=True)
        self.assertFalse(result.passed)
        self.assertIn("PR-003-external-beta-closed", result.blocked_ids)
        self.assertIn("PR-012-archive-restore", result.blocked_ids)

    def test_actual_replay_survives_a_clean_restore_without_the_original_path(self):
        canonical = ROOT / "docs/product/full-product-beta-contract.json"
        before = hashlib.sha256(canonical.read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            with public_replay_fixture(base / "original") as (value, manifest, contract):
                shutil.copytree(base / "original", base / "restored")
                (base / "original").rename(base / "hidden-original")
                result = audit_release(value, manifest, base / "restored", acceptance_contract=contract)
                self.assertTrue(result.passed, result.errors)
                (base / "restored/beta/synthetic-tone.wav").write_bytes(b"substituted")
                result = audit_release(value, manifest, base / "restored", acceptance_contract=contract)
                self.assertFalse(result.passed)
                self.assertTrue(any("digest" in error for error in result.errors), result.errors)
        self.assertEqual(before, hashlib.sha256(canonical.read_bytes()).hexdigest())

    def test_ready_replay_passes_ready_but_cannot_claim_closed_or_public_active(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with public_replay_fixture(root, state="EXTERNAL_BETA_READY", closed=False) as (value, manifest, contract):
                ready = audit_release(value, manifest, root, "EXTERNAL_BETA_READY", acceptance_contract=contract)
                self.assertTrue(ready.passed, ready.errors)
                for target in ("EXTERNAL_BETA_CLOSED", "PUBLIC_ACTIVE"):
                    changed = copy.deepcopy(value)
                    changed["state"] = target
                    result = audit_release(changed, manifest, root, target, acceptance_contract=contract)
                    self.assertFalse(result.passed)
                    self.assertIn("PR-003-external-beta-closed", result.blocked)

    def test_signed_activation_and_resume_replay_exact_inputs_on_every_call(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with public_replay_fixture(root) as (value, manifest, contract):
                inputs = {"candidate": write_reference(root, "public-candidate.json", value),
                    "archiveManifest": write_reference(root, "public-archive.json", manifest), "archiveRoot": "."}
                snapshot = {"schemaVersion": 1, "candidateLineageId": value["candidateLineageId"],
                    "evidenceRootSha256": value["rootChain"]["evidenceRoot"]["sha256"],
                    "state": "EXTERNAL_BETA_CLOSED", "decisionLog": []}
                activate = decision(snapshot, "ACTIVATE", "activate", contract, releaseAudit=inputs, approvals=value["approvals"])
                from jsonschema import Draft202012Validator
                schema = json.loads((ROOT / "docs/product/public-release-replay.schema.json").read_text())
                validator = Draft202012Validator(schema)
                validator.validate(value["externalBeta"])
                validator.validate(activate)
                active = release_gate.transition(snapshot, activate, contract, base=root)
                self.assertEqual("PUBLIC_ACTIVE", active["state"])
                self.assertEqual("PUBLIC_ACTIVE", active["lastReproducedAudit"]["state"])
                pause = decision(active, "PAUSE", "pause", contract, reason="synthetic integrity exercise")
                paused = release_gate.transition(active, pause, contract, base=root)
                fresh = [approval(role, index, snapshot["evidenceRootSha256"], "2026-08-31T04:00:00Z")
                    for index, role in enumerate(APPROVAL_ROLES, 1)]
                resume = decision(paused, "RESUME", "resume", contract, releaseAudit=inputs, approvals=fresh)
                resumed = release_gate.transition(paused, resume, contract, base=root)
                self.assertEqual("PUBLIC_ACTIVE", resumed["state"])
                # A previously valid, signed decision cannot reuse a prior PASS
                # once the raw predecessor bytes change.
                (root / "beta/report.json").write_text("{}")
                with self.assertRaisesRegex(ValueError, "reproduced public release audit failed"):
                    release_gate.transition(paused, resume, contract, base=root)
                revoked = release_gate.transition(paused,
                    decision(paused, "REVOKE", "revoke", contract, reason="withdraw synthetic candidate"), contract, base=root)
                self.assertEqual("REVOKED", revoked["state"])

    def test_predecessor_contract_and_identity_substitution_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with public_replay_fixture(root) as (value, manifest, contract):
                from tools.external_beta.full_product_contract import full_product_contract_errors
                external_policy = ROOT / "docs/product/full-product-beta-contract.json"
                outside_errors = full_product_contract_errors({"fullProductContract": {
                    "locator": str(external_policy), "sha256": hashlib.sha256(external_policy.read_bytes()).hexdigest()}}, base=root / "beta")
                self.assertTrue(any("cannot be read" in error for error in outside_errors), outside_errors)
                for key in ("candidateRootSha256", "candidateLineageId"):
                    changed = copy.deepcopy(value)
                    changed["externalBeta"][key] = "wrong-predecessor"
                    result = audit_release(changed, manifest, root, acceptance_contract=contract)
                    self.assertFalse(result.passed)
                    self.assertIn("PR-003-external-beta-closed", result.blocked)
                (root / "beta/full-contract.json").write_text("{}")
                result = audit_release(value, manifest, root, acceptance_contract=contract)
                self.assertFalse(result.passed)
                self.assertTrue(any("contract" in error and "digest" in error for error in result.errors), result.errors)

    def test_cli_accepts_complete_inputs_and_rejects_signed_boolean_only_decision(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with public_replay_fixture(root) as (value, manifest, contract):
                write_reference(root, "public-candidate.json", value)
                write_reference(root, "public-archive.json", manifest)
                write_reference(root, "public-contract.json", contract)
                command = [sys.executable, str(ROOT / "scripts/run_public_release_audit.py"),
                    "--candidate", str(root / "public-candidate.json"), "--archive-manifest", str(root / "public-archive.json"),
                    "--archive-root", str(root), "--acceptance-contract", str(root / "public-contract.json")]
                result = subprocess.run(command, text=True, capture_output=True, check=False, timeout=45)
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                self.assertTrue(json.loads(result.stdout)["passed"])
                snapshot = {"schemaVersion": 1, "candidateLineageId": value["candidateLineageId"],
                    "evidenceRootSha256": value["rootChain"]["evidenceRoot"]["sha256"], "state": "EXTERNAL_BETA_CLOSED", "decisionLog": []}
                write_reference(root, "snapshot.json", snapshot)
                claimed = decision(snapshot, "ACTIVATE", "claim", contract, gatePassed=True, approvals=value["approvals"])
                write_reference(root, "decision.json", claimed)
                result = subprocess.run([sys.executable, str(ROOT / "scripts/run_public_release_operation.py"),
                    "--snapshot", str(root / "snapshot.json"), "--decision", str(root / "decision.json"),
                    "--acceptance-contract", str(root / "public-contract.json")], text=True, capture_output=True, check=False, timeout=45)
                self.assertNotEqual(0, result.returncode)
                self.assertIn("restored releaseAudit", result.stdout)
                accepted = decision(snapshot, "ACTIVATE", "actual-replay", contract,
                    approvals=value["approvals"], releaseAudit={
                        "candidate": write_reference(root, "public-candidate.json", value),
                        "archiveManifest": write_reference(root, "public-archive.json", manifest), "archiveRoot": "."})
                write_reference(root, "decision.json", accepted)
                result = subprocess.run([sys.executable, str(ROOT / "scripts/run_public_release_operation.py"),
                    "--snapshot", str(root / "snapshot.json"), "--decision", str(root / "decision.json"),
                    "--acceptance-contract", str(root / "public-contract.json")], text=True, capture_output=True, check=False, timeout=45)
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                self.assertEqual("PUBLIC_ACTIVE", json.loads(result.stdout)["state"])


if __name__ == "__main__":
    unittest.main()
