from __future__ import annotations

import json
from pathlib import Path

from tools.external_beta import release_gate


def record(
    *,
    requirement_id: str,
    candidate_root_id: str = "candidate-root-001",
    stage_node_id: str = "installed-macos-001",
    parent_edge_id: str = "edge-signed-to-installed-001",
    status: str = "PASS",
) -> release_gate.JsonObject:
    return {
        "recordId": f"record-{requirement_id}",
        "requirementId": requirement_id,
        "candidateRootId": candidate_root_id,
        "stageNodeId": stage_node_id,
        "parentEdgeId": parent_edge_id,
        "sourceCommit": "a" * 40,
        "platform": "macos",
        "architecture": "arm64",
        "surface": "standalone",
        "host": None,
        "finalDeliverableSha256": "b" * 64,
        "installedTreeSha256": "c" * 64,
        "artifactSha256": "d" * 64,
        "toolIdentity": {"name": "validator", "version": "1.0"},
        "workloadId": "eb.render.preview.v1",
        "workloadSha256": release_gate.stable_workload_sha256(
            "eb.render.preview.v1"
        ),
        "machineProfileId": "eb.macos.arm64.reference.v1",
        "machineProfileSha256": release_gate.stable_machine_sha256(
            "eb.macos.arm64.reference.v1"
        ),
        "privacyClass": "PUBLIC_TECHNICAL",
        "roles": {"producer": "A3", "reviewer": "A6", "approver": "A3"},
        "trustedTime": "2026-08-21T12:00:00Z",
        "rawArchive": {
            "locator": f"archive/{requirement_id}.json",
            "sha256": "1" * 64,
        },
        "status": status,
        "blockingReason": None if status == "PASS" else "target not run",
    }


def candidate() -> release_gate.JsonObject:
    requirement_ids = tuple(release_gate.READY_REQUIREMENT_IDS)
    contract_path = (
        Path(__file__).resolve().parents[2]
        / "docs/product/external-beta-acceptance.json"
    )
    contract = json.loads(contract_path.read_text(encoding="utf-8"))
    assert isinstance(contract, dict)
    acceptance_contract_sha256 = release_gate.sha256_json(contract)
    requirement_values = contract["requirements"]
    assert isinstance(requirement_values, list)
    records: list[release_gate.JsonObject] = []
    references: dict[str, list[str]] = {}
    stage_locations = {
        "UNSIGNED_PAYLOAD": ("unsigned-001", "edge-source-to-unsigned-001"),
        "SIGNED_DELIVERABLE": ("signed-001", "edge-unsigned-to-signed-001"),
        "SEALED_ARCHIVE": ("archive-001", "edge-installed-to-archive-001"),
    }
    for requirement in requirement_values:
        assert isinstance(requirement, dict)
        requirement_id = requirement["id"]
        policy = requirement["evidencePolicy"]
        assert isinstance(requirement_id, str)
        assert isinstance(policy, dict)
        stage_kinds = policy["stageKinds"]
        target_values = policy["requiredTargets"]
        assert isinstance(stage_kinds, list) and len(stage_kinds) == 1
        assert isinstance(stage_kinds[0], str)
        assert isinstance(target_values, list)
        references[requirement_id] = []
        for index, target in enumerate(target_values):
            assert isinstance(target, dict)
            stage_kind = stage_kinds[0]
            stage_node_id, parent_edge_id = stage_locations.get(
                stage_kind,
                ("installed-macos-001", "edge-signed-to-installed-macos-001"),
            )
            if stage_kind == "INSTALLED_TREE" and target.get("platform") == "windows":
                stage_node_id = "installed-windows-001"
                parent_edge_id = "edge-signed-to-installed-windows-001"
            value = record(
                requirement_id=requirement_id,
                stage_node_id=stage_node_id,
                parent_edge_id=parent_edge_id,
            )
            record_id = f"record-{requirement_id}-{index + 1}"
            value.update(
                {
                    "recordId": record_id,
                    "platform": target["platform"],
                    "architecture": target["architecture"],
                    "surface": target["surface"],
                    "host": target.get("host"),
                    "rawArchive": {
                        "locator": f"archive/{record_id}.json",
                        "sha256": "1" * 64,
                    },
                }
            )
            records.append(value)
            references[requirement_id].append(record_id)
    return {
        "schemaVersion": 1,
        "gate": "EXTERNAL_BETA_READY",
        "acceptanceContractSha256": acceptance_contract_sha256,
        "releaseIdentity": {
            "product": "Project SEAM",
            "version": "0.13.1",
            "buildId": "candidate-build-001",
            "sourceCommit": "a" * 40,
            "buildEpoch": 1_755_768_000,
        },
        "candidateRoot": {
            "id": "candidate-root-001",
            "sha256": "2" * 64,
            "status": "SEALED",
            "acceptanceContractSha256": acceptance_contract_sha256,
        },
        "stageNodes": [
            {
                "id": "source-001",
                "kind": "SOURCE_TREE",
                "sha256": "0" * 64,
            },
            {
                "id": "unsigned-001",
                "kind": "UNSIGNED_PAYLOAD",
                "sha256": "3" * 64,
            },
            {
                "id": "signed-001",
                "kind": "SIGNED_DELIVERABLE",
                "sha256": "4" * 64,
            },
            {
                "id": "installed-macos-001",
                "kind": "INSTALLED_TREE",
                "sha256": "c" * 64,
            },
            {
                "id": "installed-windows-001",
                "kind": "INSTALLED_TREE",
                "sha256": "d" * 64,
            },
            {
                "id": "archive-001",
                "kind": "SEALED_ARCHIVE",
                "sha256": "e" * 64,
            },
        ],
        "stageEdges": [
            {
                "id": "edge-source-to-unsigned-001",
                "parent": "source-001",
                "child": "unsigned-001",
                "transformation": "BUILD",
            },
            {
                "id": "edge-unsigned-to-signed-001",
                "parent": "unsigned-001",
                "child": "signed-001",
                "transformation": "SIGN",
            },
            {
                "id": "edge-signed-to-installed-macos-001",
                "parent": "signed-001",
                "child": "installed-macos-001",
                "transformation": "INSTALL",
            },
            {
                "id": "edge-signed-to-installed-windows-001",
                "parent": "signed-001",
                "child": "installed-windows-001",
                "transformation": "INSTALL",
            },
            {
                "id": "edge-installed-to-archive-001",
                "parent": "installed-macos-001",
                "child": "archive-001",
                "transformation": "ARCHIVE",
            },
        ],
        "requirements": {
            item: {
                "status": "PASS",
                "evidenceRecordIds": references[item],
            }
            for item in requirement_ids
        },
        "evidence": records,
        "workloadCatalog": {
            "eb.render.preview.v1": {
                "sha256": release_gate.stable_workload_sha256(
                    "eb.render.preview.v1"
                ),
                "identityMode": "stable-id-v1",
            },
        },
        "machineProfiles": {
            "eb.macos.arm64.reference.v1": {
                "sha256": release_gate.stable_machine_sha256(
                    "eb.macos.arm64.reference.v1"
                ),
                "identityMode": "stable-id-v1",
            },
        },
        "archive": {
            "locator": "archive/candidate-root-001",
            "sha256": "5" * 64,
            "anchored": True,
            "immutable": True,
        },
        "issues": [],
    }
