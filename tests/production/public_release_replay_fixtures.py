"""Temporary signed public/Beta audit fixtures with no production authorization.

The External Beta predecessor is the complete ENGINEERING_FIXTURE candidate
from tests/external_beta/full_product_report_fixture.py.  It is labelled with
the reserved synthetic contract identity, so the real Beta audit rejects it
with exactly two synthetic-authority errors and nothing else.  Public-state
tests that need a passing predecessor therefore run the real archive,
signature, registry, typed report and cohort validators and discard only that
residue through admit_synthetic_predecessor, a test double that exists only
here.  Tests that pass admit_synthetic=False observe the production refusal.
"""
from __future__ import annotations

from contextlib import contextmanager, nullcontext
import os
from pathlib import Path
from unittest import mock

from tests.external_beta.full_product_report_fixture import synthetic_beta_archive, write_reference
from tests.production.public_release_fixtures import candidate, acceptance_contract
from tests.production.public_release_archive_fixtures import archived_candidate
from tools.external_beta.full_product_contract import SYNTHETIC_CONTRACT_AUTHORITY_ERROR
from tools.external_beta.full_product_gate import FIXTURE_AUTHORITY_ERROR
from tools.external_beta.release_audit import ReleaseAuditResult, audit_release as real_beta_audit
from tools.external_beta.release_gate import sha256_json

ROOT = Path(__file__).resolve().parents[2]
SYNTHETIC_AUTHORITY_RESIDUE = frozenset(("gate: " + SYNTHETIC_CONTRACT_AUTHORITY_ERROR, "gate: " + FIXTURE_AUTHORITY_ERROR))

__all__ = ("admit_synthetic_predecessor", "make_beta_archive", "public_replay_fixture", "write_reference")


def admit_synthetic_predecessor(candidate_value, manifest, root, state="READY", *, acceptance_contract=None):
    """Test double: the real Beta audit minus only the synthetic-authority residue."""

    result = real_beta_audit(candidate_value, manifest, root, state, acceptance_contract=acceptance_contract)
    errors = set(result.errors)
    if errors - SYNTHETIC_AUTHORITY_RESIDUE or set(result.blocked) - {"EB-009-full-product"} or not SYNTHETIC_AUTHORITY_RESIDUE <= errors:
        return result
    return ReleaseAuditResult(True, result.state)


def make_beta_archive(root: Path, *, closed: bool = True):
    built = synthetic_beta_archive(root, closed=closed)
    return (built["candidate"], built["candidateReference"], built["manifestReference"],
        built["policyReference"], built["trustedAnchor"])


@contextmanager
def public_replay_fixture(root: Path, *, state: str = "PUBLIC_ACTIVE", closed: bool = True, admit_synthetic: bool = True):
    beta, beta_ref, manifest_ref, policy_ref, trusted_anchor = make_beta_archive(root / "beta", closed=closed)
    contract = acceptance_contract()
    contract["externalBetaAcceptanceContractSha256"] = policy_ref["sha256"]
    value = candidate(contract)
    value["state"] = state
    prefixed = lambda reference: {"locator": "beta/" + reference["locator"], "sha256": reference["sha256"]}  # noqa: E731
    value["externalBeta"] = {"state": beta["gate"], "candidateLineageId": value["candidateLineageId"],
        "candidateRootId": beta["candidateRoot"]["id"], "candidateRootSha256": beta["candidateRoot"]["sha256"],
        "acceptanceContract": prefixed(policy_ref),
        "releaseAudit": {"candidate": prefixed(beta_ref), "archiveManifest": prefixed(manifest_ref), "archiveRoot": "beta"}}
    predecessor_hash = sha256_json(value["externalBeta"])
    value["rootChain"]["evidenceRoot"]["externalBetaSha256"] = predecessor_hash
    next(record for record in value["evidence"] if record["requirementId"] == "PR-003-external-beta-closed")["externalBetaSha256"] = predecessor_hash
    extra_paths = [path.relative_to(root).as_posix() for path in (root / "beta").rglob("*") if path.is_file()]
    value, manifest = archived_candidate(root, value, extra_paths=extra_paths)
    key = "SEAM_EXTERNAL_BETA_TRUSTED_ANCHOR_SHA256"
    previous = os.environ.get(key)
    os.environ[key] = trusted_anchor
    double = mock.patch("tools.public_release.replay.audit_beta_release", admit_synthetic_predecessor)
    try:
        with double if admit_synthetic else nullcontext():
            yield value, manifest, contract
    finally:
        if previous is None:
            os.environ.pop(key, None)
        else:
            os.environ[key] = previous
