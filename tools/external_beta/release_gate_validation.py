from __future__ import annotations

import hashlib
import re

JsonValue = str | int | float | bool | None | list["JsonValue"] | dict[str, "JsonValue"]
JsonObject = dict[str, JsonValue]

HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")
HEX40 = re.compile(r"^[0-9a-fA-F]{40}$")


def stable_workload_sha256(identifier: str) -> str:
    return hashlib.sha256(f"workload:{identifier}:v1".encode("utf-8")).hexdigest()


def stable_machine_sha256(identifier: str) -> str:
    return hashlib.sha256(f"machine:{identifier}:v1".encode("utf-8")).hexdigest()


def _digest(value: JsonValue, label: str, errors: list[str]) -> None:
    if not isinstance(value, str) or HEX64.fullmatch(value) is None:
        errors.append(f"{label} must be a 64-character hexadecimal digest")


def _identity_errors(value: JsonValue, label: str = "releaseIdentity") -> list[str]:
    if not isinstance(value, dict):
        return [f"{label} must be an object"]
    errors: list[str] = []
    for key in ("product", "version", "buildId", "sourceCommit"):
        if not isinstance(value.get(key), str) or not value[key]:
            errors.append(f"{label}.{key} is required")
    if isinstance(value.get("sourceCommit"), str) and HEX40.fullmatch(value["sourceCommit"]) is None:
        errors.append(f"{label}.sourceCommit must be a 40-character hexadecimal commit")
    if not isinstance(value.get("buildEpoch"), int) or isinstance(value.get("buildEpoch"), bool) or value["buildEpoch"] < 0:
        errors.append(f"{label}.buildEpoch must be a non-negative integer")
    return errors


def _lineage_errors(candidate: JsonObject) -> list[str]:
    errors: list[str] = []
    root = candidate.get("candidateRoot")
    if not isinstance(root, dict) or not isinstance(root.get("id"), str) or not root["id"]:
        return ["candidateRoot.id is required"]
    if root.get("status") != "SEALED":
        errors.append("candidateRoot.status must be SEALED")
    _digest(root.get("sha256"), "candidateRoot.sha256", errors)
    nodes_value = candidate.get("stageNodes")
    edges_value = candidate.get("stageEdges")
    if not isinstance(nodes_value, list) or not nodes_value:
        return errors + ["stageNodes must be a non-empty array"]
    if not isinstance(edges_value, list) or not edges_value:
        return errors + ["stageEdges must be a non-empty array"]
    nodes: dict[str, JsonObject] = {}
    for node in nodes_value:
        if not isinstance(node, dict) or not isinstance(node.get("id"), str) or not node["id"]:
            errors.append("every stage node requires a non-empty id")
            continue
        if node["id"] in nodes:
            errors.append(f"duplicate stage node: {node['id']}")
        nodes[node["id"]] = node
        if not isinstance(node.get("kind"), str) or not node["kind"]:
            errors.append(f"{node['id']}: stage kind is required")
        _digest(node.get("sha256"), f"{node['id']}.sha256", errors)
    edges: dict[str, JsonObject] = {}
    for edge in edges_value:
        if not isinstance(edge, dict) or not isinstance(edge.get("id"), str) or not edge["id"]:
            errors.append("every stage edge requires a non-empty id")
            continue
        edge_id = edge["id"]
        if edge_id in edges:
            errors.append(f"duplicate stage edge: {edge_id}")
        edges[edge_id] = edge
        if edge.get("parent") not in nodes or edge.get("child") not in nodes:
            errors.append(f"{edge_id}: stage edge references an unknown node")
        if not isinstance(edge.get("transformation"), str) or not edge["transformation"]:
            errors.append(f"{edge_id}: transformation is required")
    evidence = candidate.get("evidence")
    if not isinstance(evidence, list) or not evidence:
        return errors + ["evidence must be a non-empty array"]
    for record in evidence:
        if not isinstance(record, dict):
            errors.append("each evidence record must be an object")
            continue
        record_id = record.get("recordId", "evidence")
        stage = record.get("stageNodeId")
        edge = record.get("parentEdgeId")
        if stage not in nodes:
            errors.append(f"{record_id}: stageNodeId is not in the candidate graph")
        if edge not in edges:
            errors.append(f"{record_id}: parentEdgeId is not in the candidate graph")
        elif isinstance(edges[edge], dict) and edges[edge].get("child") != stage:
            errors.append(f"{record_id}: parentEdgeId does not terminate at stageNodeId")
        if record.get("candidateRootId") != root["id"]:
            errors.append(f"{record_id}: candidateRootId does not match candidateRoot")
    return errors


def _evidence_errors(candidate: JsonObject) -> list[str]:
    errors: list[str] = []
    identity = candidate.get("releaseIdentity")
    records = candidate.get("evidence")
    workloads = candidate.get("workloadCatalog")
    machines = candidate.get("machineProfiles")
    nodes_value = candidate.get("stageNodes")
    stage_nodes: dict[str, JsonObject] = {}
    if isinstance(nodes_value, list):
        for node in nodes_value:
            if isinstance(node, dict) and isinstance(node.get("id"), str):
                stage_nodes[node["id"]] = node
    if not isinstance(records, list):
        return ["evidence must be an array"]
    record_ids: set[str] = set()
    for record in records:
        if not isinstance(record, dict):
            continue
        record_id = record.get("recordId")
        if not isinstance(record_id, str) or not record_id:
            errors.append("evidence recordId is required")
            continue
        if record_id in record_ids:
            errors.append(f"duplicate evidence record: {record_id}")
        record_ids.add(record_id)
        required = ("requirementId", "sourceCommit", "platform", "architecture", "surface", "workloadId", "machineProfileId", "privacyClass", "trustedTime", "status")
        for key in required:
            if not isinstance(record.get(key), str) or not record[key]:
                errors.append(f"{record_id}: {key} is required")
        if record.get("status") == "PASS":
            for key in ("finalDeliverableSha256", "installedTreeSha256", "artifactSha256", "workloadSha256", "machineProfileSha256"):
                _digest(record.get(key), f"{record_id}.{key}", errors)
            roles = record.get("roles")
            if not isinstance(roles, dict) or not all(isinstance(roles.get(key), str) and roles[key] for key in ("producer", "reviewer", "approver")):
                errors.append(f"{record_id}: producer, reviewer, and approver roles are required")
            archive = record.get("rawArchive")
            if not isinstance(archive, dict) or not isinstance(archive.get("locator"), str) or not archive["locator"]:
                errors.append(f"{record_id}: rawArchive.locator is required")
            else:
                _digest(archive.get("sha256"), f"{record_id}.rawArchive.sha256", errors)
            if isinstance(identity, dict) and record.get("sourceCommit") != identity.get("sourceCommit"):
                errors.append(f"{record_id}: sourceCommit differs from release identity")
            stage = stage_nodes.get(record.get("stageNodeId"))
            if (
                isinstance(stage, dict)
                and stage.get("kind") == "INSTALLED_TREE"
                and record.get("installedTreeSha256") != stage.get("sha256")
            ):
                errors.append(f"{record_id}: installed tree digest differs from declared stage node")
            if isinstance(workloads, dict) and isinstance(record.get("workloadId"), str):
                workload = workloads.get(record["workloadId"])
                if not isinstance(workload, dict) or workload.get("sha256") != record.get("workloadSha256"):
                    errors.append(f"{record_id}: workload identity differs from declared workload")
                elif workload.get("identityMode") == "stable-id-v1" and record.get("workloadSha256") != stable_workload_sha256(record["workloadId"]):
                    errors.append(f"{record_id}: workload digest does not match stable workload identity")
            if isinstance(machines, dict) and isinstance(record.get("machineProfileId"), str):
                machine = machines.get(record["machineProfileId"])
                if not isinstance(machine, dict) or machine.get("sha256") != record.get("machineProfileSha256"):
                    errors.append(f"{record_id}: machine identity differs from declared profile")
                elif machine.get("identityMode") == "stable-id-v1" and record.get("machineProfileSha256") != stable_machine_sha256(record["machineProfileId"]):
                    errors.append(f"{record_id}: machine digest does not match stable machine identity")
        elif record.get("status") not in {"NOT_RUN", "BLOCKED", "FAIL"}:
            errors.append(f"{record_id}: invalid status")
    return errors


def _requirement_errors(candidate: JsonObject, required_ids: tuple[str, ...]) -> tuple[list[str], tuple[str, ...]]:
    errors: list[str] = []
    blocked: list[str] = []
    requirements = candidate.get("requirements")
    records = candidate.get("evidence")
    records_by_id: dict[str, JsonObject] = {}
    if isinstance(records, list):
        for record in records:
            if not isinstance(record, dict):
                continue
            record_id = record.get("recordId")
            if isinstance(record_id, str):
                records_by_id[record_id] = record
    if not isinstance(requirements, dict):
        return ["requirements must be an object"], required_ids
    for requirement_id in required_ids:
        item = requirements.get(requirement_id)
        if not isinstance(item, dict):
            errors.append(f"missing requirement: {requirement_id}")
            blocked.append(requirement_id)
            continue
        if item.get("status") != "PASS":
            errors.append(f"requirement {requirement_id} is not PASS")
            blocked.append(requirement_id)
        refs = item.get("evidenceRecordIds")
        valid_refs = [reference for reference in refs if isinstance(reference, str) and reference in records_by_id] if isinstance(refs, list) else []
        if not isinstance(refs, list) or not refs or len(valid_refs) != len(refs):
            errors.append(f"requirement {requirement_id} must reference existing evidence")
        elif any(records_by_id[reference].get("status") != "PASS" or records_by_id[reference].get("requirementId") != requirement_id for reference in valid_refs):
            errors.append(f"requirement {requirement_id} must reference PASS evidence for itself")
    return errors, tuple(sorted(blocked))


def _base_errors(candidate: JsonObject) -> list[str]:
    errors: list[str] = []
    if candidate.get("schemaVersion") != 1:
        errors.append("schemaVersion must equal 1")
    errors.extend(_identity_errors(candidate.get("releaseIdentity")))
    errors.extend(_lineage_errors(candidate))
    errors.extend(_evidence_errors(candidate))
    archive = candidate.get("archive")
    if not isinstance(archive, dict) or archive.get("anchored") is not True or archive.get("immutable") is not True:
        errors.append("archive must be anchored and immutable")
    elif not isinstance(archive.get("locator"), str) or not archive["locator"]:
        errors.append("archive.locator is required")
    else:
        _digest(archive.get("sha256"), "archive.sha256", errors)
    issues = candidate.get("issues", [])
    if not isinstance(issues, list):
        errors.append("issues must be an array")
    else:
        for issue in issues:
            if isinstance(issue, dict) and issue.get("severity") in {"Blocker", "Critical"} and issue.get("status") not in {"RESOLVED", "CLOSED"}:
                errors.append(f"unresolved {issue.get('severity')} issue: {issue.get('id', 'unknown')}")
    return errors
