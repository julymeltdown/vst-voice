"""Restored, byte-bound public and External Beta audit inputs.

Policy is supplied by the trusted acceptance-contract caller, never by a
candidate's asserted PASS or by an injectable verifier. The default Beta policy
is the current canonical acceptance document; a public freeze may pin another
reviewed revision by its exact file SHA-256 in its acceptance contract.
"""
from __future__ import annotations

import hashlib
from pathlib import Path

from tools.external_beta.full_product_report import _parse_json, _read_regular_reference, _reference_errors
from tools.external_beta.release_audit import audit_release as audit_beta_release
from .contracts import JsonObject, ReleaseGateInputError, sha256_json

ROOT = Path(__file__).resolve().parents[2]


def read_reference(reference, root: Path, label: str) -> JsonObject:
    errors = _reference_errors(reference, base=root, label=label, verify=False)
    if errors:
        raise ReleaseGateInputError("; ".join(errors))
    value = _parse_json(_read_regular_reference(reference, base=root, label=label, maximum_bytes=64 * 1024 * 1024))
    if not isinstance(value, dict):
        raise ReleaseGateInputError(f"{label} must contain a JSON object")
    return value


def restored_directory(value, base: Path, label: str) -> Path:
    if not isinstance(value, str) or not value:
        raise ReleaseGateInputError(f"{label} is required")
    path = Path(value)
    if not path.is_absolute():
        path = base / path
    try:
        path.resolve(strict=True).relative_to(base.resolve(strict=True))
    except (OSError, ValueError) as error:
        raise ReleaseGateInputError(f"{label} escapes or is absent from the restored root") from error
    if path.is_symlink() or not path.is_dir():
        raise ReleaseGateInputError(f"{label} must be a real directory")
    return path


def predecessor_state(state: str) -> str | None:
    if state == "EXTERNAL_BETA_READY":
        return "READY"
    if state in {"EXTERNAL_BETA_CLOSED", "PUBLIC_ACTIVE", "DISTRIBUTION_PAUSED", "SUPERSEDED"}:
        return "CLOSED"
    return None


def replay_predecessor(candidate: JsonObject, contract: JsonObject, root: Path, required_state: str) -> tuple[str, ...]:
    try:
        value = candidate.get("externalBeta")
        if not isinstance(value, dict):
            raise ReleaseGateInputError("restored External Beta predecessor is required")
        declared = value.get("state")
        if declared not in {"EXTERNAL_BETA_READY", "EXTERNAL_BETA_CLOSED"} or (
            required_state == "CLOSED" and declared != "EXTERNAL_BETA_CLOSED"
        ):
            raise ReleaseGateInputError("External Beta READY cannot substitute for CLOSED")
        if value.get("candidateLineageId") != candidate.get("candidateLineageId"):
            raise ReleaseGateInputError("External Beta predecessor lineage differs")
        chain = candidate.get("rootChain", {})
        evidence_root = chain.get("evidenceRoot", {}) if isinstance(chain, dict) else {}
        if not isinstance(evidence_root, dict) or evidence_root.get("externalBetaSha256") != sha256_json(value):
            raise ReleaseGateInputError("EvidenceRoot does not bind the complete External Beta replay inputs")
        inputs = value.get("releaseAudit")
        if not isinstance(inputs, dict) or set(inputs) != {"candidate", "archiveManifest", "archiveRoot"}:
            raise ReleaseGateInputError("External Beta predecessor requires exact restored releaseAudit inputs")
        beta_root = restored_directory(inputs["archiveRoot"], root, "External Beta archiveRoot")
        beta = read_reference(inputs["candidate"], root, "External Beta candidate")
        manifest = read_reference(inputs["archiveManifest"], root, "External Beta archive manifest")
        policy_reference = value.get("acceptanceContract")
        policy = read_reference(policy_reference, root, "External Beta acceptance contract")
        expected = contract.get("externalBetaAcceptanceContractSha256")
        if expected is None:
            expected = hashlib.sha256((ROOT / "docs/product/external-beta-acceptance.json").read_bytes()).hexdigest()
        if policy_reference.get("sha256") != expected:
            raise ReleaseGateInputError("External Beta acceptance contract differs from the trusted public policy")
        beta_identity = beta.get("candidateRoot", {})
        if not isinstance(beta_identity, dict) or beta_identity.get("id") != value.get("candidateRootId") or beta_identity.get("sha256") != value.get("candidateRootSha256"):
            raise ReleaseGateInputError("External Beta predecessor root differs from its bound candidate")
        # The signed public EvidenceRoot owns the explicit same-lineage mapping;
        # also require the actual source base, rather than a matching label alone.
        beta_release = beta.get("releaseIdentity")
        public_release = candidate.get("releaseIdentity")
        if not isinstance(beta_release, dict) or not isinstance(public_release, dict) or beta_release.get("sourceCommit") != public_release.get("sourceCommit"):
            raise ReleaseGateInputError("External Beta predecessor source differs from public lineage")
        audit_state = "CLOSED" if declared == "EXTERNAL_BETA_CLOSED" else "READY"
        result = audit_beta_release(beta, manifest, beta_root, audit_state, acceptance_contract=policy)
        return tuple(f"External Beta replay: {error}" for error in result.errors) if not result.passed else ()
    except (OSError, ValueError, TypeError) as error:
        return (f"External Beta replay: {error}",)
