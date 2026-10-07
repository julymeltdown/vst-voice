"""Temporary signed public/Beta audit fixtures with no production authorization.

Runs the real archive, signature, registry, report and cohort validators. The
test's trusted policy copy and archive anchor are explicit configuration, not a
mock evaluator or a production fixture-admission switch.

The legacy synthetic soak payloads deliberately lack supervised session records.
They must be refused; they cannot establish physical release qualification.
"""
from __future__ import annotations

from contextlib import contextmanager
import copy
import hashlib
import json
import os
from pathlib import Path

from tests.external_beta.full_product_report_fixture import complete_report
from tests.external_beta.release_gate_fixtures import candidate as beta_candidate
from tests.external_beta.test_cohort_gate import _cohort
from tests.production.public_release_fixtures import candidate, acceptance_contract
from tests.production.public_release_archive_fixtures import archived_candidate
from tools.external_beta.evidence_archive import create_archive_manifest
from tools.external_beta.release_gate import candidate_root_sha256, sha256_json

ROOT = Path(__file__).resolve().parents[2]


LEGACY_SOAK_GATE_ERRORS = tuple("External Beta replay: gate: " + error for error in (
    "record-EB-005-standalone-soak-1: product soak reference: product soak reference must contain a typed external-beta-product-soak record",
    "record-EB-005-standalone-soak-2: product soak reference: product soak reference must contain a typed external-beta-product-soak record",
    "full-product observation R17.soak-hosts: product soak reference must contain a typed external-beta-product-soak record",
    "full-product observation R17.soak-hosts: distinct 1800/7200 supervised sessions are required",
))
LEGACY_SOAK_AUDIT_ERRORS = tuple("gate: " + error for error in LEGACY_SOAK_GATE_ERRORS)
LEGACY_SOAK_OPERATION_ERROR = "reproduced public release audit failed: " + "; ".join(LEGACY_SOAK_AUDIT_ERRORS)


def assert_legacy_soak_refusal(test, result, *, audit=False):
    test.assertFalse(result.passed)
    test.assertEqual(("PR-003-external-beta-closed",), result.blocked if audit else result.blocked_ids)
    test.assertEqual(LEGACY_SOAK_AUDIT_ERRORS if audit else LEGACY_SOAK_GATE_ERRORS, result.errors)


def write_reference(root: Path, relative: str, value):
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    data = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()
    path.write_bytes(data)
    return {"locator": relative, "sha256": hashlib.sha256(data).hexdigest()}


def make_beta_archive(root: Path, *, closed: bool = True):
    root.mkdir(parents=True, exist_ok=True)
    report, full = complete_report(root)
    # Preserve the exact mandatory registry identifier. All numerical values
    # remain solely in this private synthetic copy, never the canonical file.
    full["contractId"] = "project-seam.full-product-beta"
    full_ref = write_reference(root, "full-contract.json", full)
    acceptance = json.loads((ROOT / "docs/product/external-beta-acceptance.json").read_text())
    acceptance["fullProductContract"] = full_ref
    policy_ref = write_reference(root, "acceptance.json", acceptance)
    beta = beta_candidate()
    beta["gate"] = "EXTERNAL_BETA_CLOSED" if closed else "EXTERNAL_BETA_READY"
    beta["acceptanceContractSha256"] = sha256_json(acceptance)
    beta["candidateRoot"]["acceptanceContractSha256"] = beta["acceptanceContractSha256"]
    beta["candidateRoot"]["sha256"] = candidate_root_sha256(beta["candidateRoot"])
    report.update(candidateRootId=beta["candidateRoot"]["id"], candidateRootSha256=beta["candidateRoot"]["sha256"],
        acceptanceContractSha256=beta["acceptanceContractSha256"], fullProductContractSha256=full_ref["sha256"])
    for case in report["cases"]:
        for observation in case["observations"]:
            observation["sourceCommit"] = beta["releaseIdentity"]["sourceCommit"]
            observation["buildId"] = beta["releaseIdentity"]["buildId"]
    report_ref = write_reference(root, "report.json", report)
    references = []
    for platform in ("macos", "windows"):
        value = copy.deepcopy(next(record for record in beta["evidence"] if record["platform"] == platform and record["stageNodeId"].startswith("installed")))
        value.update(recordId="synthetic-full-product-" + platform, requirementId="EB-009-full-product",
            surface="standalone", host=None, fullProductReport=report_ref)
        references.append(value["recordId"])
        beta["evidence"].append(value)
    beta["requirements"]["EB-009-full-product"] = {"status": "PASS", "evidenceRecordIds": references}
    if closed:
        beta["cohort"] = _cohort()
    for value in beta["evidence"]:
        payload = {key: child for key, child in value.items() if key != "rawArchive"}
        value["rawArchive"] = write_reference(root, "archive/" + value["recordId"] + ".json", payload)
    paths = [path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()]
    manifest = create_archive_manifest(beta["candidateRoot"]["id"], root, paths,
        "synthetic-test-only-beta-archive", anchor_locator="https://8.8.8.8/synthetic-test-only")
    beta["archive"] = {"locator": "synthetic-test-only-beta-archive", "sha256": manifest["manifestSha256"], "anchored": True, "immutable": True}
    beta_ref = write_reference(root, "candidate.json", beta)
    manifest_ref = write_reference(root, "archive-manifest.json", manifest)
    return beta, beta_ref, manifest_ref, policy_ref, manifest["anchor"]["sha256"]


@contextmanager
def public_replay_fixture(root: Path, *, state: str = "PUBLIC_ACTIVE", closed: bool = True):
    beta, beta_ref, manifest_ref, policy_ref, trusted_anchor = make_beta_archive(root / "beta", closed=closed)
    contract = acceptance_contract()
    contract["externalBetaAcceptanceContractSha256"] = policy_ref["sha256"]
    value = candidate(contract)
    value["state"] = state
    prefixed = lambda reference: {"locator": "beta/" + reference["locator"], "sha256": reference["sha256"]}
    value["externalBeta"] = {"state": beta["gate"], "candidateLineageId": value["candidateLineageId"],
        "candidateRootId": beta["candidateRoot"]["id"], "candidateRootSha256": beta["candidateRoot"]["sha256"],
        "acceptanceContract": prefixed(policy_ref),
        "releaseAudit": {"candidate": prefixed(beta_ref), "archiveManifest": prefixed(manifest_ref), "archiveRoot": "beta"},
        # A closed External Beta asserts real testers ran this candidate, so the records naming
        # the collectors that measured it are part of the object being hashed.
        "measuredEvidence": {
            "install": {"status": "PASS", "collectorTool": "tools/external_beta/install_collector.py"},
            "soak": {"status": "PASS", "collectorTool": "tools/external_beta/soak_collector.py"},
            "host": {"status": "PASS", "collectorTool": "tools/external_beta/host_collector.py"},
        }}
    predecessor_hash = sha256_json(value["externalBeta"])
    value["rootChain"]["evidenceRoot"]["externalBetaSha256"] = predecessor_hash
    next(record for record in value["evidence"] if record["requirementId"] == "PR-003-external-beta-closed")["externalBetaSha256"] = predecessor_hash
    extra_paths = [path.relative_to(root).as_posix() for path in (root / "beta").rglob("*") if path.is_file()]
    value, manifest = archived_candidate(root, value, extra_paths=extra_paths)
    key = "SEAM_EXTERNAL_BETA_TRUSTED_ANCHOR_SHA256"
    previous = os.environ.get(key)
    os.environ[key] = trusted_anchor
    try:
        yield value, manifest, contract
    finally:
        if previous is None:
            os.environ.pop(key, None)
        else:
            os.environ[key] = previous
