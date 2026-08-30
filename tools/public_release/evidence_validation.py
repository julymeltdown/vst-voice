from __future__ import annotations

from pathlib import PurePosixPath

from .contracts import (
    INCIDENT_ACTIONS,
    PUBLIC_REQUIREMENT_IDS,
    SUPPORT_LIFECYCLE_STAGES,
    JsonObject,
    ValidationFinding,
    is_sha256,
    parse_time,
)


def _finding(requirement_id: str, message: str) -> ValidationFinding:
    return ValidationFinding(requirement_id, message)


def _safe_relative(value: str) -> bool:
    path = PurePosixPath(value)
    return (
        bool(value)
        and "\\" not in value
        and not path.is_absolute()
        and all(part not in {"", ".", ".."} for part in path.parts)
    )


def requirement_findings(candidate: JsonObject) -> tuple[ValidationFinding, ...]:
    findings: list[ValidationFinding] = []
    requirements = candidate.get("requirements")
    records_value = candidate.get("evidence")
    records: dict[str, JsonObject] = {}
    if isinstance(records_value, list):
        for record in records_value:
            if isinstance(record, dict) and isinstance(record.get("recordId"), str):
                records[record["recordId"]] = record
    else:
        findings.extend(
            _finding(requirement_id, "evidence must be an array")
            for requirement_id in PUBLIC_REQUIREMENT_IDS
        )
    if not isinstance(requirements, dict):
        return tuple(
            [
                *findings,
                *(
                    _finding(requirement_id, f"requirement {requirement_id} is missing")
                    for requirement_id in PUBLIC_REQUIREMENT_IDS
                ),
            ]
        )
    for requirement_id in PUBLIC_REQUIREMENT_IDS:
        requirement = requirements.get(requirement_id)
        if not isinstance(requirement, dict):
            findings.append(_finding(requirement_id, f"requirement {requirement_id} is missing"))
            continue
        if requirement.get("status") != "PASS":
            findings.append(_finding(requirement_id, f"requirement {requirement_id} is not PASS"))
        references = requirement.get("evidenceRecordIds")
        if not isinstance(references, list) or not references:
            findings.append(_finding(requirement_id, f"requirement {requirement_id} needs evidence"))
            continue
        for reference in references:
            record = records.get(reference) if isinstance(reference, str) else None
            if record is None:
                findings.append(_finding(requirement_id, f"missing evidence record: {reference}"))
                continue
            if record.get("requirementId") != requirement_id or record.get("status") != "PASS":
                findings.append(_finding(requirement_id, f"evidence {reference} cannot satisfy {requirement_id}"))
    return tuple(findings)


def evidence_record_findings(candidate: JsonObject) -> tuple[ValidationFinding, ...]:
    records = candidate.get("evidence")
    if not isinstance(records, list):
        return ()
    findings: list[ValidationFinding] = []
    artifact_sha256 = None
    chain = candidate.get("rootChain")
    if isinstance(chain, dict):
        artifact = chain.get("artifactRoot")
        if isinstance(artifact, dict):
            artifact_sha256 = artifact.get("sha256")
    seen: set[str] = set()
    for index, record in enumerate(records):
        if not isinstance(record, dict):
            continue
        requirement_id = record.get("requirementId")
        if requirement_id not in PUBLIC_REQUIREMENT_IDS:
            continue
        assert isinstance(requirement_id, str)
        record_id = record.get("recordId")
        if not isinstance(record_id, str) or not record_id:
            findings.append(_finding(requirement_id, f"evidence[{index}] recordId is required"))
        elif record_id in seen:
            findings.append(_finding(requirement_id, f"duplicate evidence record: {record_id}"))
        else:
            seen.add(record_id)
        if record.get("candidateLineageId") != candidate.get("candidateLineageId"):
            findings.append(_finding(requirement_id, f"{record_id} candidate lineage differs"))
        if record.get("artifactRootSha256") != artifact_sha256:
            findings.append(_finding(requirement_id, f"{record_id} artifact root differs"))
        producer = record.get("producerId")
        reviewer = record.get("reviewerId")
        if not isinstance(producer, str) or not producer or not isinstance(reviewer, str) or not reviewer:
            findings.append(_finding(requirement_id, f"{record_id} producer and reviewer are required"))
        elif producer == reviewer:
            findings.append(_finding(requirement_id, f"{record_id} producer and reviewer must be distinct"))
        if parse_time(record.get("trustedTime")) is None:
            findings.append(_finding(requirement_id, f"{record_id} trustedTime is invalid"))
        raw = record.get("rawArchive")
        if not isinstance(raw, dict):
            findings.append(_finding(requirement_id, f"{record_id} raw archive is required"))
            continue
        path = raw.get("path")
        if not isinstance(path, str) or not _safe_relative(path):
            findings.append(_finding(requirement_id, f"{record_id} archive path is unsafe"))
        if not is_sha256(raw.get("sha256")):
            findings.append(_finding(requirement_id, f"{record_id} archive digest is invalid"))
    return tuple(findings)


def _status_object(
    candidate: JsonObject,
    key: str,
    requirement_id: str,
) -> tuple[JsonObject | None, list[ValidationFinding]]:
    value = candidate.get(key)
    if not isinstance(value, dict):
        return None, [_finding(requirement_id, f"{key} must be an object")]
    findings: list[ValidationFinding] = []
    if value.get("status") != "PASS":
        findings.append(_finding(requirement_id, f"{key} is not PASS"))
    if value.get("candidateLineageId") != candidate.get("candidateLineageId"):
        findings.append(_finding(requirement_id, f"{key} candidate lineage differs"))
    return value, findings


def operation_surface_findings(candidate: JsonObject) -> tuple[ValidationFinding, ...]:
    findings: list[ValidationFinding] = []
    chain = candidate.get("rootChain")
    artifact_sha256 = None
    evidence_manifest_sha256 = None
    if isinstance(chain, dict):
        artifact = chain.get("artifactRoot")
        evidence = chain.get("evidenceRoot")
        if isinstance(artifact, dict):
            artifact_sha256 = artifact.get("sha256")
        if isinstance(evidence, dict):
            evidence_manifest_sha256 = evidence.get("archiveManifestSha256")
    update, update_findings = _status_object(candidate, "updateChannel", "PR-009-update-channel")
    findings.extend(update_findings)
    if update is not None:
        if update.get("artifactRootSha256") != artifact_sha256:
            findings.append(_finding("PR-009-update-channel", "update channel artifact root differs"))
        if not is_sha256(update.get("metadataSha256")) or update.get("signatureVerified") is not True:
            findings.append(_finding("PR-009-update-channel", "signed update metadata is required"))
    support, support_findings = _status_object(candidate, "supportIntake", "PR-010-support-intake")
    findings.extend(support_findings)
    if support is not None:
        if support.get("destinationId") != "project-seam.public.support-intake":
            findings.append(_finding("PR-010-support-intake", "public support destination differs"))
        if support.get("securityContactId") != "project-seam.public.security-contact":
            findings.append(_finding("PR-010-support-intake", "public security contact differs"))
        if support.get("lifecycleStages") != list(SUPPORT_LIFECYCLE_STAGES):
            findings.append(_finding("PR-010-support-intake", "support lifecycle is incomplete"))
    incident, incident_findings = _status_object(candidate, "incidentDrill", "PR-011-incident-drill")
    findings.extend(incident_findings)
    if incident is not None and incident.get("actions") != list(INCIDENT_ACTIONS):
        findings.append(_finding("PR-011-incident-drill", "incident drill actions are incomplete"))
    archive = candidate.get("archive")
    if not isinstance(archive, dict):
        findings.append(_finding("PR-012-archive-restore", "archive must be an object"))
    else:
        if archive.get("status") != "PASS" or archive.get("restored") is not True:
            findings.append(_finding("PR-012-archive-restore", "archive restore is not PASS"))
        if archive.get("anchored") is not True or archive.get("immutable") is not True:
            findings.append(_finding("PR-012-archive-restore", "archive must be anchored and immutable"))
        if archive.get("manifestSha256") != evidence_manifest_sha256:
            findings.append(_finding("PR-012-archive-restore", "archive manifest digest differs"))
    rollback, rollback_findings = _status_object(candidate, "rollbackOrRevoke", "PR-014-rollback-revoke")
    findings.extend(rollback_findings)
    if rollback is not None:
        if not is_sha256(rollback.get("predecessorArtifactRootSha256")) or rollback.get("rollbackVerified") is not True:
            findings.append(_finding("PR-014-rollback-revoke", "verified rollback record is required"))
        rehearsal = rollback.get("revokeRehearsalLineageId")
        if not isinstance(rehearsal, str) or not rehearsal or rehearsal == candidate.get("candidateLineageId"):
            findings.append(_finding("PR-014-rollback-revoke", "revoke rehearsal needs a separate lineage"))
        if rollback.get("revokeIrreversibleVerified") is not True:
            findings.append(_finding("PR-014-rollback-revoke", "irreversible revoke rehearsal is required"))
    return tuple(findings)


def evidence_findings(candidate: JsonObject) -> tuple[ValidationFinding, ...]:
    return (
        *requirement_findings(candidate),
        *evidence_record_findings(candidate),
        *operation_surface_findings(candidate),
    )
