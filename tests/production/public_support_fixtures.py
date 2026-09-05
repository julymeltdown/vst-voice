from __future__ import annotations

from tests.production.public_release_contract_fixtures import JsonObject


def support_intake_fixture(evidence_record_ids: list[str]) -> JsonObject:
    return {
        "status": "PASS",
        "destinationId": "project-seam.public.support-intake",
        "securityContactId": "project-seam.public.security-contact",
        "candidateLineageId": "public-lineage-001",
        "bundleSchemaId": (
            "https://project-seam.invalid/schemas/public-support-bundle-2.json"
        ),
        "bundleSha256": "f" * 64,
        "acknowledgementId": "support-ack-001",
        "acknowledgedBundleSha256": "f" * 64,
        "retentionPolicyId": "project-seam.public.support-retention-1",
        "retentionWindows": {
            "publicTechnicalDays": 180,
            "restrictedAttachmentDays": 30,
        },
        "withdrawnBundleSha256": "f" * 64,
        "withdrawalVerified": True,
        "deletedBundleSha256": "f" * 64,
        "deletionVerified": True,
        "minimalAuditRecordSha256": "a" * 64,
        "stageOwnerIds": {
            "INTAKE": "support-owner-001",
            "ACKNOWLEDGED": "support-owner-001",
            "TRIAGED": "support-owner-002",
            "REPRODUCED": "support-owner-002",
            "RESOLVED_OR_ESCALATED": "support-owner-003",
            "USER_COMMUNICATED": "support-owner-001",
            "RETAINED_OR_DELETED": "privacy-owner-001",
        },
        "lifecycleStages": [
            "INTAKE",
            "ACKNOWLEDGED",
            "TRIAGED",
            "REPRODUCED",
            "RESOLVED_OR_ESCALATED",
            "USER_COMMUNICATED",
            "RETAINED_OR_DELETED",
        ],
        "evidenceRecordIds": evidence_record_ids,
    }
