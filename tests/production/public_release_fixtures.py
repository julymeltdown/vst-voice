from __future__ import annotations

import copy
import json
from functools import lru_cache

from tests.production.public_release_contract_fixtures import (
    APPROVAL_ROLES,
    REQUIREMENT_IDS,
    JsonObject,
    acceptance_contract,
    approval,
    canonical_json,
    sign_operation,
    sha256_json,
    sha256_root,
)
from tests.production.public_support_fixtures import support_intake_fixture


@lru_cache(maxsize=8)
def _candidate_template(contract_json: str) -> JsonObject:
    selected_contract = json.loads(contract_json)
    assert isinstance(selected_contract, dict)
    contract_sha256 = sha256_json(selected_contract)
    documents = selected_contract["publicDocuments"]
    assert isinstance(documents, list)
    freeze_root: JsonObject = {
        "id": "freeze-root-001",
        "status": "FROZEN",
        "candidateLineageId": "public-lineage-001",
        "acceptanceContractSha256": contract_sha256,
        "sourceCommit": "a" * 40,
        "sourceTreeSha256": "1" * 64,
        "bankSourceSha256": "2" * 64,
        "publicDocumentsSha256": sha256_json(documents),
        "sbomSha256": "3" * 64,
        "trustPolicySha256": "4" * 64,
        "toolchainSha256": "5" * 64,
        "unsignedPayloadSha256": "6" * 64,
    }
    freeze_root["sha256"] = sha256_root(freeze_root)
    artifact_root: JsonObject = {
        "id": "artifact-root-001",
        "status": "SEALED",
        "candidateLineageId": "public-lineage-001",
        "freezeRootSha256": freeze_root["sha256"],
        "macosPackageSha256": "7" * 64,
        "windowsInstallerSha256": "8" * 64,
        "bankPackageSha256": "9" * 64,
    }
    artifact_root["sha256"] = sha256_root(artifact_root)
    records: list[JsonObject] = []
    for index, requirement_id in enumerate(REQUIREMENT_IDS, start=1):
        record_id = f"public-record-{index:02d}"
        archived: JsonObject = {
            "recordId": record_id,
            "requirementId": requirement_id,
            "candidateLineageId": "public-lineage-001",
            "artifactRootSha256": artifact_root["sha256"],
            "status": "PASS",
            "producerId": f"producer-{index:02d}",
            "reviewerId": f"reviewer-{index:02d}",
            "trustedTime": "2026-08-31T00:30:00Z",
        }
        if requirement_id == "PR-010-support-intake":
            archived["supportBundleSha256"] = "f" * 64
        record = dict(archived)
        record["rawArchive"] = {
            "path": f"evidence/{record_id}.json",
            "sha256": sha256_json(archived),
        }
        records.append(record)
    evidence_root: JsonObject = {
        "id": "evidence-root-001",
        "status": "SEALED",
        "candidateLineageId": "public-lineage-001",
        "artifactRootSha256": artifact_root["sha256"],
        "archiveManifestSha256": "d" * 64,
        "evidenceIndexSha256": sha256_json(records),
        "macosInstalledTreeSha256": "b" * 64,
        "windowsInstalledTreeSha256": "c" * 64,
        "bankInstalledTreeSha256": "e" * 64,
    }
    evidence_root["sha256"] = sha256_root(evidence_root)
    approvals = [
        approval(role, index, str(evidence_root["sha256"]))
        for index, role in enumerate(APPROVAL_ROLES, start=1)
    ]
    operation_envelope: JsonObject = {
        "schemaVersion": 1,
        "envelopeId": "public-activation-001",
        "action": "ACTIVATE",
        "state": "PUBLIC_ACTIVE",
        "candidateLineageId": "public-lineage-001",
        "evidenceRootSha256": evidence_root["sha256"],
        "approvalEnvelopeSha256s": [item["envelopeSha256"] for item in approvals],
        "actorId": "release-operator-001",
        "actorRole": "release-manager",
        "createdAt": "2026-08-31T01:15:00Z",
    }
    sign_operation(operation_envelope, "envelopeSha256")
    references = {
        requirement_id: [records[index]["recordId"]]
        for index, requirement_id in enumerate(REQUIREMENT_IDS)
    }
    return {
        "schemaVersion": 1,
        "contractId": "project-seam.public-release.acceptance",
        "contractVersion": "1.0.0",
        "state": "PUBLIC_ACTIVE",
        "candidateLineageId": "public-lineage-001",
        "acceptanceContractSha256": contract_sha256,
        "releaseIdentity": {
            "product": "Project SEAM",
            "version": "1.0.0",
            "buildId": "public-build-001",
            "sourceCommit": "a" * 40,
            "buildEpoch": 1_788_131_600,
        },
        "rootChain": {
            "freezeRoot": freeze_root,
            "artifactRoot": artifact_root,
            "evidenceRoot": evidence_root,
        },
        "requirements": {
            requirement_id: {
                "status": "PASS",
                "evidenceRecordIds": references[requirement_id],
            }
            for requirement_id in REQUIREMENT_IDS
        },
        "evidence": records,
        "externalBeta": {
            "state": "EXTERNAL_BETA_CLOSED",
            "candidateLineageId": "public-lineage-001",
            "candidateRootSha256": "f" * 64,
            "closedAt": "2026-08-30T23:00:00Z",
            "archiveVerified": True,
        },
        "publicDocuments": [
            {
                **document,
                "acceptedVersion": document["version"],
                "acceptedSha256": document["sha256"],
            }
            for document in documents
            if isinstance(document, dict)
        ],
        "signedArtifacts": {
            "status": "PASS",
            "macosPackageSha256": artifact_root["macosPackageSha256"],
            "windowsInstallerSha256": artifact_root["windowsInstallerSha256"],
            "bankPackageSha256": artifact_root["bankPackageSha256"],
            "evidenceRecordIds": references["PR-005-signed-artifacts"],
        },
        "installations": {
            "status": "PASS",
            "macosInstalledTreeSha256": evidence_root["macosInstalledTreeSha256"],
            "windowsInstalledTreeSha256": evidence_root["windowsInstalledTreeSha256"],
            "evidenceRecordIds": references["PR-006-clean-installed"],
        },
        "bank": {
            "status": "PASS",
            "sourceSha256": freeze_root["bankSourceSha256"],
            "packageSha256": artifact_root["bankPackageSha256"],
            "installedTreeSha256": evidence_root["bankInstalledTreeSha256"],
            "rightsPassed": True,
            "musicalReviewPassed": True,
            "evidenceRecordIds": references["PR-007-bank-ready"],
        },
        "targetMatrices": {
            "status": "PASS",
            "macos": {
                "namespace": "UA",
                "platform": "macos",
                "architecture": "arm64",
                "status": "PASS",
                "rowIds": [f"UA-{index:03d}" for index in range(1, 21)],
            },
            "windows": {
                "namespace": "PW",
                "platform": "windows",
                "architecture": "x86_64",
                "status": "PASS",
                "rowIds": [f"PW-{index:03d}" for index in range(1, 21)],
            },
            "evidenceRecordIds": references["PR-008-target-matrices"],
        },
        "updateChannel": {
            "status": "PASS",
            "channelId": "project-seam.public.direct-download",
            "candidateLineageId": "public-lineage-001",
            "artifactRootSha256": artifact_root["sha256"],
            "metadataSha256": "0" * 64,
            "signatureVerified": True,
            "evidenceRecordIds": references["PR-009-update-channel"],
        },
        "supportIntake": support_intake_fixture(
            references["PR-010-support-intake"]
        ),
        "incidentDrill": {
            "status": "PASS",
            "candidateLineageId": "public-lineage-001",
            "actions": [
                "PAUSE",
                "SUPPORT_INTAKE",
                "ACKNOWLEDGE",
                "TRIAGE",
                "REPAIR",
                "ROLLBACK",
                "REVOKE_REHEARSAL",
                "USER_COMMUNICATION",
            ],
            "evidenceRecordIds": references["PR-011-incident-drill"],
        },
        "archive": {
            "status": "PASS",
            "manifestSha256": evidence_root["archiveManifestSha256"],
            "restored": True,
            "anchored": True,
            "immutable": True,
            "evidenceRecordIds": references["PR-012-archive-restore"],
        },
        "approvals": approvals,
        "operationEnvelope": operation_envelope,
        "rollbackOrRevoke": {
            "status": "PASS",
            "candidateLineageId": "public-lineage-001",
            "predecessorArtifactRootSha256": "a" * 64,
            "rollbackVerified": True,
            "revokeRehearsalLineageId": "revoke-rehearsal-001",
            "revokeIrreversibleVerified": True,
            "evidenceRecordIds": references["PR-014-rollback-revoke"],
        },
        "issues": [],
    }


def candidate(contract: JsonObject | None = None) -> JsonObject:
    selected_contract = contract if contract is not None else acceptance_contract()
    return copy.deepcopy(_candidate_template(canonical_json(selected_contract)))
