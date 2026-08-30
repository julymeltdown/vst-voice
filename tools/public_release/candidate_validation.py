from __future__ import annotations

from .contracts import (
    HEX40,
    JsonObject,
    ValidationFinding,
    is_sha256,
    root_sha256,
    sha256_json,
)
from .surface_validation import (
    matrix_findings,
    product_surface_findings,
    public_document_findings,
)


def _finding(requirement_id: str, message: str) -> ValidationFinding:
    return ValidationFinding(requirement_id, message)


def _required_object(
    candidate: JsonObject,
    key: str,
    requirement_id: str,
) -> tuple[JsonObject | None, tuple[ValidationFinding, ...]]:
    value = candidate.get(key)
    if not isinstance(value, dict):
        return None, (_finding(requirement_id, f"{key} must be an object"),)
    return value, ()


def _digest_fields(
    value: JsonObject,
    keys: tuple[str, ...],
    requirement_id: str,
    label: str,
) -> list[ValidationFinding]:
    return [
        _finding(requirement_id, f"{label}.{key} must be a lowercase SHA-256")
        for key in keys
        if not is_sha256(value.get(key))
    ]


def contract_findings(
    candidate: JsonObject,
    contract: JsonObject,
) -> tuple[ValidationFinding, ...]:
    requirement_id = "PR-001-contract"
    findings: list[ValidationFinding] = []
    if candidate.get("schemaVersion") != 1:
        findings.append(_finding(requirement_id, "schemaVersion must equal 1"))
    if candidate.get("contractId") != contract.get("contractId"):
        findings.append(_finding(requirement_id, "candidate contractId differs"))
    if candidate.get("contractVersion") != contract.get("contractVersion"):
        findings.append(_finding(requirement_id, "candidate contractVersion differs"))
    contract_status = contract.get("status")
    if contract_status not in {"BLOCKED", "PASSED"}:
        findings.append(_finding(requirement_id, "acceptance contract status is invalid"))
    if candidate.get("state") == "PUBLIC_ACTIVE" and contract_status != "PASSED":
        findings.append(
            _finding(requirement_id, "PUBLIC_ACTIVE requires a PASSED acceptance contract")
        )
    contract_sha256 = sha256_json(contract)
    if candidate.get("acceptanceContractSha256") != contract_sha256:
        findings.append(_finding(requirement_id, "acceptance contract digest differs"))
    lineage = candidate.get("candidateLineageId")
    if not isinstance(lineage, str) or not lineage:
        findings.append(_finding(requirement_id, "candidateLineageId is required"))
    identity = candidate.get("releaseIdentity")
    if not isinstance(identity, dict):
        findings.append(_finding(requirement_id, "releaseIdentity must be an object"))
        return tuple(findings)
    for key in ("product", "version", "buildId", "sourceCommit"):
        if not isinstance(identity.get(key), str) or not identity[key]:
            findings.append(_finding(requirement_id, f"releaseIdentity.{key} is required"))
    source_commit = identity.get("sourceCommit")
    if not isinstance(source_commit, str) or HEX40.fullmatch(source_commit) is None:
        findings.append(_finding(requirement_id, "source commit must be lowercase hexadecimal"))
    build_epoch = identity.get("buildEpoch")
    if not isinstance(build_epoch, int) or isinstance(build_epoch, bool) or build_epoch < 0:
        findings.append(_finding(requirement_id, "releaseIdentity.buildEpoch is invalid"))
    return tuple(findings)


def root_chain_findings(
    candidate: JsonObject,
    contract: JsonObject,
) -> tuple[ValidationFinding, ...]:
    requirement_id = "PR-002-root-chain"
    findings: list[ValidationFinding] = []
    chain = candidate.get("rootChain")
    if not isinstance(chain, dict):
        return (_finding(requirement_id, "rootChain must be an object"),)
    roots: dict[str, JsonObject] = {}
    for key in ("freezeRoot", "artifactRoot", "evidenceRoot"):
        value = chain.get(key)
        if not isinstance(value, dict):
            findings.append(_finding(requirement_id, f"rootChain.{key} is required"))
        else:
            roots[key] = value
    if len(roots) != 3:
        return tuple(findings)
    freeze = roots["freezeRoot"]
    artifact = roots["artifactRoot"]
    evidence = roots["evidenceRoot"]
    lineage = candidate.get("candidateLineageId")
    for label, root in roots.items():
        if root.get("candidateLineageId") != lineage:
            findings.append(_finding(requirement_id, f"{label} candidate lineage differs"))
        if root.get("sha256") != root_sha256(root):
            findings.append(_finding(requirement_id, f"{label} canonical digest differs"))
    if freeze.get("status") != "FROZEN":
        findings.append(_finding(requirement_id, "freezeRoot status must be FROZEN"))
    if artifact.get("status") != "SEALED" or evidence.get("status") != "SEALED":
        findings.append(_finding(requirement_id, "artifact and evidence roots must be SEALED"))
    findings.extend(
        _digest_fields(
            freeze,
            (
                "sourceTreeSha256",
                "bankSourceSha256",
                "publicDocumentsSha256",
                "sbomSha256",
                "trustPolicySha256",
                "toolchainSha256",
                "unsignedPayloadSha256",
            ),
            requirement_id,
            "freezeRoot",
        )
    )
    findings.extend(
        _digest_fields(
            artifact,
            ("macosPackageSha256", "windowsInstallerSha256", "bankPackageSha256"),
            requirement_id,
            "artifactRoot",
        )
    )
    findings.extend(
        _digest_fields(
            evidence,
            (
                "archiveManifestSha256",
                "evidenceIndexSha256",
                "macosInstalledTreeSha256",
                "windowsInstalledTreeSha256",
                "bankInstalledTreeSha256",
            ),
            requirement_id,
            "evidenceRoot",
        )
    )
    if freeze.get("acceptanceContractSha256") != sha256_json(contract):
        findings.append(_finding(requirement_id, "freeze root contract digest differs"))
    documents = contract.get("publicDocuments")
    if freeze.get("publicDocumentsSha256") != sha256_json(documents):
        findings.append(_finding(requirement_id, "freeze root public document digest differs"))
    identity = candidate.get("releaseIdentity")
    if not isinstance(identity, dict) or freeze.get("sourceCommit") != identity.get("sourceCommit"):
        findings.append(_finding(requirement_id, "source commit differs across root chain"))
    if artifact.get("freezeRootSha256") != freeze.get("sha256"):
        findings.append(_finding(requirement_id, "artifact root does not bind freeze root"))
    if evidence.get("artifactRootSha256") != artifact.get("sha256"):
        findings.append(_finding(requirement_id, "evidence root does not bind artifact root"))
    records = candidate.get("evidence")
    if evidence.get("evidenceIndexSha256") != sha256_json(records):
        findings.append(_finding(requirement_id, "evidence index digest differs"))
    return tuple(findings)


def external_beta_findings(candidate: JsonObject) -> tuple[ValidationFinding, ...]:
    requirement_id = "PR-003-external-beta-closed"
    value, findings = _required_object(candidate, "externalBeta", requirement_id)
    if value is None:
        return findings
    errors = list(findings)
    if value.get("state") != "EXTERNAL_BETA_CLOSED":
        errors.append(_finding(requirement_id, "External Beta must be CLOSED"))
    if value.get("candidateLineageId") != candidate.get("candidateLineageId"):
        errors.append(_finding(requirement_id, "External Beta candidate lineage differs"))
    if not is_sha256(value.get("candidateRootSha256")):
        errors.append(_finding(requirement_id, "External Beta candidate root is required"))
    if value.get("archiveVerified") is not True:
        errors.append(_finding(requirement_id, "External Beta archive must be verified"))
    return tuple(errors)


def candidate_findings(
    candidate: JsonObject,
    contract: JsonObject,
) -> tuple[ValidationFinding, ...]:
    return (
        *contract_findings(candidate, contract),
        *root_chain_findings(candidate, contract),
        *external_beta_findings(candidate),
        *public_document_findings(candidate, contract),
        *product_surface_findings(candidate),
        *matrix_findings(candidate),
    )
