from __future__ import annotations

import base64
import copy
from functools import lru_cache
import hashlib
import json

from tools.phase13a.update_contract import (
    canonical_json as signing_json,
    ed25519_public_key,
    ed25519_sign,
)


JsonValue = (
    str | int | float | bool | None | list["JsonValue"] | dict[str, "JsonValue"]
)
JsonObject = dict[str, JsonValue]

REQUIREMENT_IDS = (
    "PR-001-contract",
    "PR-002-root-chain",
    "PR-003-external-beta-closed",
    "PR-004-public-documents",
    "PR-005-signed-artifacts",
    "PR-006-clean-installed",
    "PR-007-bank-ready",
    "PR-008-target-matrices",
    "PR-009-update-channel",
    "PR-010-support-intake",
    "PR-011-incident-drill",
    "PR-012-archive-restore",
    "PR-013-approvals",
    "PR-014-rollback-revoke",
)

APPROVAL_ROLES = (
    "independent-release-verifier",
    "content-rights",
    "security-privacy",
    "macos-reviewer",
    "windows-reviewer",
    "musician-reviewer",
    "accessibility-reviewer",
    "archive-reviewer",
)

OPERATION_KEY_ID = "public-operation-release-manager-01"
OPERATION_POLICY_VERSION = "public-release-operation-1.0"
OPERATION_SEED = bytes.fromhex("f0" * 32)


def canonical_json(value: JsonValue) -> str:
    return json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    )


def sha256_json(value: JsonValue) -> str:
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def sha256_root(value: JsonObject) -> str:
    return sha256_json({key: item for key, item in value.items() if key != "sha256"})


def approval_seed(role: str) -> bytes:
    return hashlib.sha256(f"approval-seed:{role}".encode()).digest()


def approval_key_id(role: str) -> str:
    return f"public-approval-{role}"


def signing_payload(value: JsonObject, digest_field: str) -> bytes:
    unsigned = {
        key: item
        for key, item in value.items()
        if key not in {"signature", digest_field}
    }
    return signing_json(unsigned)


def sign_operation(value: JsonObject, digest_field: str) -> None:
    value.pop("signature", None)
    value.pop(digest_field, None)
    value["keyId"] = OPERATION_KEY_ID
    value["policyVersion"] = OPERATION_POLICY_VERSION
    value["algorithm"] = "Ed25519"
    value["signature"] = base64.b64encode(
        ed25519_sign(signing_payload(value, digest_field), OPERATION_SEED)
    ).decode("ascii")
    value[digest_field] = sha256_json(value)


def approval(
    role: str,
    index: int,
    evidence_root_sha256: str,
    trusted_time: str = "2026-08-31T01:00:00Z",
) -> JsonObject:
    value: JsonObject = {
        "schemaVersion": 1,
        "envelopeId": f"approval-{index:02d}",
        "signerId": f"reviewer-{index:02d}",
        "role": role,
        "decision": "GO",
        "policyVersion": "public-release-approval-1.0",
        "evidenceRootSha256": evidence_root_sha256,
        "trustedTime": trusted_time,
        "keyId": approval_key_id(role),
        "algorithm": "Ed25519",
    }
    value["signature"] = base64.b64encode(
        ed25519_sign(signing_payload(value, "envelopeSha256"), approval_seed(role))
    ).decode("ascii")
    value["envelopeSha256"] = sha256_json(value)
    return value


@lru_cache(maxsize=1)
def _acceptance_contract_template() -> JsonObject:
    documents = [
        {
            "id": "project-seam.public.eula",
            "version": "public-eula-1.0.0",
            "path": "docs/public/EULA.md",
            "sha256": "1" * 64,
            "approvalStatus": "APPROVED",
            "acceptanceMode": "EXPLICIT",
            "reacceptOnDigestChange": True,
        },
        {
            "id": "project-seam.public.privacy",
            "version": "public-privacy-1.1.0",
            "path": "docs/public/PRIVACY.md",
            "sha256": "2" * 64,
            "approvalStatus": "APPROVED",
            "acceptanceMode": "EXPLICIT",
            "reacceptOnDigestChange": True,
        },
        {
            "id": "project-seam.public.support",
            "version": "public-support-1.1.0",
            "path": "docs/public/SUPPORT.md",
            "sha256": "3" * 64,
            "approvalStatus": "APPROVED",
            "acceptanceMode": "PUBLISHED_VERSION",
            "reacceptOnDigestChange": False,
        },
        {
            "id": "project-seam.public.security-response",
            "version": "public-security-response-1.1.0",
            "path": "docs/public/SECURITY_RESPONSE.md",
            "sha256": "4" * 64,
            "approvalStatus": "APPROVED",
            "acceptanceMode": "PUBLISHED_VERSION",
            "reacceptOnDigestChange": False,
        },
    ]
    return {
        "schemaVersion": 1,
        "contractId": "project-seam.public-release.acceptance",
        "contractVersion": "1.0.0",
        "states": [
            "DRAFT",
            "AUTHORIZED_FROZEN",
            "SIGNED",
            "CLEAN_INSTALLED",
            "BANK_READY",
            "EVIDENCE_PASSED",
            "EXTERNAL_BETA_READY",
            "EXTERNAL_BETA_CLOSED",
            "PUBLIC_ACTIVE",
            "DISTRIBUTION_PAUSED",
            "SUPERSEDED",
            "REVOKED",
        ],
        "requirements": [
            {"id": requirement_id, "mandatoryFor": ["PUBLIC_ACTIVE"]}
            for requirement_id in REQUIREMENT_IDS
        ],
        "publicDocuments": documents,
        "approvalPolicy": {
            "policyVersion": "public-release-approval-1.0",
            "requiredRoles": list(APPROVAL_ROLES),
            "algorithm": "Ed25519",
            "distinctSigners": True,
            "trustedKeyFields": ["keyId", "role", "signerId", "publicKey"],
            "trustedKeys": [
                {
                    "keyId": approval_key_id(role),
                    "role": role,
                    "signerId": f"reviewer-{index:02d}",
                    "publicKey": base64.b64encode(
                        ed25519_public_key(approval_seed(role))
                    ).decode("ascii"),
                }
                for index, role in enumerate(APPROVAL_ROLES, start=1)
            ],
        },
        "operationPolicy": {
            "policyVersion": OPERATION_POLICY_VERSION,
            "publisherRole": "release-manager",
            "algorithm": "Ed25519",
            "trustedKeyFields": ["keyId", "role", "signerId", "publicKey"],
            "trustedKeys": [
                {
                    "keyId": OPERATION_KEY_ID,
                    "role": "release-manager",
                    "signerId": "release-operator-001",
                    "publicKey": base64.b64encode(
                        ed25519_public_key(OPERATION_SEED)
                    ).decode("ascii"),
                }
            ],
        },
        "status": "PASSED",
        "evidence": [],
    }


def acceptance_contract() -> JsonObject:
    return copy.deepcopy(_acceptance_contract_template())
