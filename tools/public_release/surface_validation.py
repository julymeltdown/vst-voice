from __future__ import annotations

from .contracts import JsonObject, ValidationFinding


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


def public_document_findings(
    candidate: JsonObject,
    contract: JsonObject,
) -> tuple[ValidationFinding, ...]:
    requirement_id = "PR-004-public-documents"
    expected = contract.get("publicDocuments")
    actual = candidate.get("publicDocuments")
    if not isinstance(expected, list) or not isinstance(actual, list):
        return (_finding(requirement_id, "public documents are required"),)
    findings: list[ValidationFinding] = []
    expected_by_id = {
        item["id"]: item
        for item in expected
        if isinstance(item, dict) and isinstance(item.get("id"), str)
    }
    actual_by_id = {
        item["id"]: item
        for item in actual
        if isinstance(item, dict) and isinstance(item.get("id"), str)
    }
    if set(actual_by_id) != set(expected_by_id):
        findings.append(_finding(requirement_id, "public document identities differ"))
    for document_id, expected_document in expected_by_id.items():
        document = actual_by_id.get(document_id)
        if document is None:
            continue
        for key in ("version", "path", "sha256", "approvalStatus"):
            if document.get(key) != expected_document.get(key):
                findings.append(_finding(requirement_id, f"{document_id} {key} differs"))
        approval_status = expected_document.get("approvalStatus")
        if approval_status not in {"DRAFT", "APPROVED"}:
            findings.append(
                _finding(requirement_id, f"{document_id} approvalStatus is invalid")
            )
        if candidate.get("state") == "PUBLIC_ACTIVE" and approval_status != "APPROVED":
            findings.append(
                _finding(requirement_id, f"{document_id} is not APPROVED")
            )
        if document.get("acceptedVersion") != expected_document.get("version"):
            findings.append(_finding(requirement_id, f"{document_id} accepted version differs"))
        if document.get("acceptedSha256") != expected_document.get("sha256"):
            findings.append(_finding(requirement_id, f"{document_id} accepted digest differs"))
    return tuple(findings)


def product_surface_findings(candidate: JsonObject) -> tuple[ValidationFinding, ...]:
    findings: list[ValidationFinding] = []
    chain = candidate.get("rootChain")
    if not isinstance(chain, dict):
        return (_finding("PR-002-root-chain", "rootChain must be an object"),)
    freeze = chain.get("freezeRoot")
    artifact = chain.get("artifactRoot")
    evidence = chain.get("evidenceRoot")
    if not all(isinstance(value, dict) for value in (freeze, artifact, evidence)):
        return (_finding("PR-002-root-chain", "all three release roots are required"),)
    assert isinstance(freeze, dict)
    assert isinstance(artifact, dict)
    assert isinstance(evidence, dict)
    signed, signed_errors = _required_object(candidate, "signedArtifacts", "PR-005-signed-artifacts")
    findings.extend(signed_errors)
    if signed is not None:
        for key in ("macosPackageSha256", "windowsInstallerSha256", "bankPackageSha256"):
            if signed.get(key) != artifact.get(key):
                findings.append(_finding("PR-005-signed-artifacts", f"signed {key} differs from artifact root"))
    installed, installed_errors = _required_object(candidate, "installations", "PR-006-clean-installed")
    findings.extend(installed_errors)
    if installed is not None:
        for key in ("macosInstalledTreeSha256", "windowsInstalledTreeSha256"):
            if installed.get(key) != evidence.get(key):
                findings.append(_finding("PR-006-clean-installed", f"installed {key} differs from evidence root"))
    bank, bank_errors = _required_object(candidate, "bank", "PR-007-bank-ready")
    findings.extend(bank_errors)
    if bank is not None:
        if bank.get("sourceSha256") != freeze.get("bankSourceSha256"):
            findings.append(_finding("PR-007-bank-ready", "bank source digest differs"))
        if bank.get("packageSha256") != artifact.get("bankPackageSha256"):
            findings.append(_finding("PR-007-bank-ready", "bank package digest differs"))
        if bank.get("installedTreeSha256") != evidence.get("bankInstalledTreeSha256"):
            findings.append(_finding("PR-007-bank-ready", "bank installed digest differs"))
        if bank.get("rightsPassed") is not True or bank.get("musicalReviewPassed") is not True:
            findings.append(_finding("PR-007-bank-ready", "bank rights and musical review must pass"))
    return tuple(findings)


def matrix_findings(candidate: JsonObject) -> tuple[ValidationFinding, ...]:
    requirement_id = "PR-008-target-matrices"
    matrices, findings = _required_object(candidate, "targetMatrices", requirement_id)
    if matrices is None:
        return findings
    errors = list(findings)
    expected = (
        ("macos", "UA", "macos", "arm64", [f"UA-{index:03d}" for index in range(1, 21)]),
        ("windows", "PW", "windows", "x86_64", [f"PW-{index:03d}" for index in range(1, 21)]),
    )
    for key, namespace, platform, architecture, row_ids in expected:
        value = matrices.get(key)
        if not isinstance(value, dict):
            errors.append(_finding(requirement_id, f"{key} target matrix is required"))
            continue
        if value.get("namespace") != namespace or value.get("rowIds") != row_ids:
            errors.append(_finding(requirement_id, f"{key} row namespace differs"))
        if value.get("platform") != platform or value.get("architecture") != architecture:
            errors.append(_finding(requirement_id, f"{key} target identity differs"))
        if value.get("status") != "PASS":
            errors.append(_finding(requirement_id, f"{key} target matrix is not PASS"))
    return tuple(errors)
