from __future__ import annotations

import copy
import hashlib
from pathlib import Path

from tests.production.public_release_fixtures import (
    APPROVAL_ROLES,
    JsonObject,
    approval,
    sign_operation,
    sha256_json,
    sha256_root,
)
from tests.production.public_release_contract_fixtures import canonical_json


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def archived_candidate(
    root: Path,
    source_candidate: JsonObject,
) -> tuple[JsonObject, JsonObject]:
    candidate = copy.deepcopy(source_candidate)
    records = candidate["evidence"]
    root_chain = candidate["rootChain"]
    assert isinstance(records, list)
    assert isinstance(root_chain, dict)
    artifact_root = root_chain["artifactRoot"]
    evidence_root = root_chain["evidenceRoot"]
    assert isinstance(artifact_root, dict)
    assert isinstance(evidence_root, dict)
    entries: list[JsonObject] = []
    for record in records:
        assert isinstance(record, dict)
        archived: JsonObject = {
            key: value for key, value in record.items() if key != "rawArchive"
        }
        path_value = f"evidence/{record['recordId']}.json"
        payload = (canonical_json(archived) + "\n").encode("utf-8")
        path = root / path_value
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
        digest = _sha256_bytes(payload)
        record["rawArchive"] = {"path": path_value, "sha256": digest}
        entries.append(
            {
                "path": path_value,
                "kind": "evidence-record",
                "privacyClass": "PUBLIC_TECHNICAL",
                "sha256": digest,
                "size": len(payload),
            }
        )
    anchor_sha256 = sha256_json(
        {
            "archiveId": "public-archive-001",
            "artifactRootSha256": artifact_root["sha256"],
            "entries": entries,
        }
    )
    manifest: JsonObject = {
        "schemaVersion": 1,
        "recordType": "project-seam.public-release.archive",
        "archiveId": "public-archive-001",
        "candidateLineageId": candidate["candidateLineageId"],
        "artifactRootSha256": artifact_root["sha256"],
        "status": "SEALED",
        "anchored": True,
        "immutable": True,
        "anchor": {
            "kind": "immutable-object",
            "locator": f"urn:sha256:{anchor_sha256}",
            "sha256": anchor_sha256,
        },
        "createdAt": "2026-08-31T00:45:00Z",
        "entries": entries,
    }
    manifest["manifestSha256"] = sha256_json(manifest)
    evidence_root["archiveManifestSha256"] = manifest["manifestSha256"]
    evidence_root["evidenceIndexSha256"] = sha256_json(records)
    evidence_root["sha256"] = sha256_root(evidence_root)
    archive = candidate["archive"]
    assert isinstance(archive, dict)
    archive["manifestSha256"] = manifest["manifestSha256"]
    approvals = [
        approval(role, index, str(evidence_root["sha256"]))
        for index, role in enumerate(APPROVAL_ROLES, start=1)
    ]
    candidate["approvals"] = approvals
    operation = candidate["operationEnvelope"]
    assert isinstance(operation, dict)
    operation["evidenceRootSha256"] = evidence_root["sha256"]
    operation["approvalEnvelopeSha256s"] = [
        item["envelopeSha256"] for item in approvals
    ]
    sign_operation(operation, "envelopeSha256")
    return candidate, manifest
