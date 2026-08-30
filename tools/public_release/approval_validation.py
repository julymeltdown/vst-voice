from __future__ import annotations

from datetime import datetime

from .contracts import (
    REQUIRED_APPROVAL_ROLES,
    JsonObject,
    JsonValue,
    ValidationFinding,
    is_sha256,
    parse_time,
)
from .crypto_validation import (
    approval_policy_errors,
    operation_policy_errors,
    signed_record_errors,
)


def approval_errors(
    approvals: JsonValue,
    evidence_root_sha256: JsonValue,
    policy: JsonValue,
    *,
    fresh_after: datetime | None = None,
) -> tuple[str, ...]:
    errors = list(approval_policy_errors(policy, REQUIRED_APPROVAL_ROLES))
    if not isinstance(approvals, list):
        return tuple([*errors, "approvals must be an array"])
    roles: list[str] = []
    signers: list[str] = []
    for index, approval in enumerate(approvals):
        label = f"approvals[{index}]"
        if not isinstance(approval, dict):
            errors.append(f"{label} must be an object")
            continue
        role = approval.get("role")
        signer = approval.get("signerId")
        if role not in REQUIRED_APPROVAL_ROLES:
            errors.append(f"{label} role is invalid")
            continue
        assert isinstance(role, str)
        roles.append(role)
        if not isinstance(signer, str) or not signer:
            errors.append(f"{label} signerId is required")
        else:
            signers.append(signer)
        if approval.get("decision") != "GO":
            errors.append(f"{label} decision must be GO")
        if approval.get("evidenceRootSha256") != evidence_root_sha256:
            errors.append(f"{label} evidence root differs")
        trusted_time = parse_time(approval.get("trustedTime"))
        if trusted_time is None:
            errors.append(f"{label} trustedTime is invalid")
        elif fresh_after is not None and trusted_time <= fresh_after:
            errors.append(f"{label} is not a fresh approval")
        errors.extend(
            f"{label}: {error}"
            for error in signed_record_errors(
                approval,
                policy,
                role,
                "envelopeSha256",
                "signerId",
            )
        )
    if tuple(sorted(roles)) != tuple(sorted(REQUIRED_APPROVAL_ROLES)):
        errors.append("approval quorum roles are incomplete")
    if len(signers) != len(set(signers)):
        errors.append("approval roles require distinct signers")
    return tuple(errors)


def approval_findings(
    candidate: JsonObject,
    contract: JsonObject,
) -> tuple[ValidationFinding, ...]:
    requirement_id = "PR-013-approvals"
    findings: list[ValidationFinding] = []
    chain = candidate.get("rootChain")
    evidence_root = chain.get("evidenceRoot") if isinstance(chain, dict) else None
    evidence_root_sha256 = (
        evidence_root.get("sha256") if isinstance(evidence_root, dict) else None
    )
    if isinstance(evidence_root, dict) and any(
        "approval" in key.lower() for key in evidence_root
    ):
        findings.append(
            ValidationFinding(
                requirement_id,
                "approval digests must stay outside EvidenceRoot",
            )
        )
    approval_policy = contract.get("approvalPolicy")
    findings.extend(
        ValidationFinding(requirement_id, error)
        for error in approval_errors(
            candidate.get("approvals"),
            evidence_root_sha256,
            approval_policy,
        )
    )
    operation_policy = contract.get("operationPolicy")
    findings.extend(
        ValidationFinding(requirement_id, error)
        for error in operation_policy_errors(operation_policy)
    )
    operation = candidate.get("operationEnvelope")
    if not isinstance(operation, dict):
        findings.append(
            ValidationFinding(requirement_id, "operationEnvelope is required")
        )
        return tuple(findings)
    if operation.get("action") != "ACTIVATE" or operation.get("state") != "PUBLIC_ACTIVE":
        findings.append(
            ValidationFinding(requirement_id, "activation operation differs")
        )
    if operation.get("candidateLineageId") != candidate.get("candidateLineageId"):
        findings.append(
            ValidationFinding(requirement_id, "operation candidate lineage differs")
        )
    if operation.get("evidenceRootSha256") != evidence_root_sha256:
        findings.append(
            ValidationFinding(requirement_id, "operation evidence root differs")
        )
    approvals = candidate.get("approvals")
    expected_hashes = (
        [item.get("envelopeSha256") for item in approvals if isinstance(item, dict)]
        if isinstance(approvals, list)
        else []
    )
    if operation.get("approvalEnvelopeSha256s") != expected_hashes:
        findings.append(
            ValidationFinding(
                requirement_id,
                "operation approval envelope hashes differ",
            )
        )
    if operation.get("actorRole") != "release-manager":
        findings.append(
            ValidationFinding(
                requirement_id,
                "operation actor must be release-manager",
            )
        )
    if not isinstance(operation.get("actorId"), str) or not operation["actorId"]:
        findings.append(
            ValidationFinding(requirement_id, "operation actorId is required")
        )
    if parse_time(operation.get("createdAt")) is None:
        findings.append(
            ValidationFinding(requirement_id, "operation createdAt is invalid")
        )
    findings.extend(
        ValidationFinding(requirement_id, error)
        for error in signed_record_errors(
            operation,
            operation_policy,
            "release-manager",
            "envelopeSha256",
            "actorId",
        )
    )
    if not is_sha256(evidence_root_sha256):
        findings.append(
            ValidationFinding(requirement_id, "terminal evidence root is invalid")
        )
    return tuple(findings)
