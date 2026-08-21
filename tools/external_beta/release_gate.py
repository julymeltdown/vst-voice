from __future__ import annotations

import argparse
import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path

JsonValue = str | int | float | bool | None | list["JsonValue"] | dict[str, "JsonValue"]
JsonObject = dict[str, JsonValue]

READY_REQUIREMENT_IDS = (
    "EB-001-contract",
    "EB-002-identity",
    "EB-003-beta-bank",
    "EB-004-signed-install",
    "EB-005-standalone-soak",
    "EB-006-host-matrix",
    "EB-007-provenance-archive",
    "EB-008-defect-review",
)
HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")
HEX40 = re.compile(r"^[0-9a-fA-F]{40}$")


@dataclass(frozen=True, slots=True)
class ReleaseIdentity:
    product: str
    version: str
    build_id: str
    source_commit: str
    build_epoch: int

    def as_dict(self) -> JsonObject:
        return {
            "product": self.product,
            "version": self.version,
            "buildId": self.build_id,
            "sourceCommit": self.source_commit,
            "buildEpoch": self.build_epoch,
        }


@dataclass(frozen=True, slots=True)
class GateResult:
    state: str
    passed: bool
    errors: tuple[str, ...] = ()
    blocked_ids: tuple[str, ...] = ()

    def as_dict(self) -> JsonObject:
        return {"state": self.state, "passed": self.passed, "errors": list(self.errors), "blockedIds": list(self.blocked_ids)}


def canonical_json(value: JsonValue) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def sha256_json(value: JsonValue) -> str:
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


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
    ids = {record.get("recordId") for record in records if isinstance(record, dict)} if isinstance(records, list) else set()
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
        if not isinstance(refs, list) or not refs or any(reference not in ids for reference in refs):
            errors.append(f"requirement {requirement_id} must reference existing evidence")
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


def evaluate_ready(candidate: JsonObject) -> GateResult:
    errors = _base_errors(candidate)
    requirement_errors, blocked = _requirement_errors(candidate, READY_REQUIREMENT_IDS)
    errors.extend(requirement_errors)
    return GateResult("EXTERNAL_BETA_READY", not errors and not blocked, tuple(errors), blocked)


def _cohort_errors(candidate: JsonObject) -> list[str]:
    cohort = candidate.get("cohort")
    if not isinstance(cohort, dict):
        return ["cohort evidence is required for EXTERNAL_BETA_CLOSED"]
    errors: list[str] = []
    window = cohort.get("evaluationWindow")
    if not isinstance(window, dict) or window.get("status") != "ENDED":
        errors.append("cohort evaluation window must be ENDED")
    sessions = cohort.get("externalSessions")
    if not isinstance(sessions, list):
        errors.append("cohort externalSessions are required")
        sessions = []
    completed_platforms = {item.get("platform") for item in sessions if isinstance(item, dict) and item.get("status") == "COMPLETED" and {"F1", "F2", "F5"}.issubset(set(item.get("flows", [])))}
    for platform in ("macos", "windows"):
        if platform not in completed_platforms:
            errors.append(f"cohort requires an external completed session on {platform}")
    claimed = cohort.get("claimedHostTuples")
    hosts = cohort.get("hostSessions")
    completed_hosts = {item.get("tuple") for item in hosts if isinstance(item, dict) and item.get("status") == "COMPLETED"} if isinstance(hosts, list) else set()
    if not isinstance(claimed, list) or any(item not in completed_hosts for item in claimed):
        errors.append("cohort requires an external completed session for every claimed host tuple")
    assignments = cohort.get("assignments")
    if not isinstance(assignments, list) or any(not isinstance(item, dict) or item.get("status") not in {"COMPLETED", "WITHDRAWN", "DISQUALIFIED"} or not item.get("reason") for item in assignments):
        errors.append("every cohort assignment must have a terminal status and reason")
    for key, allowed in (("checkpoints", {"RESOLVED", "COMPLETED"}), ("incidents", {"RESOLVED", "CLOSED"})):
        values = cohort.get(key)
        if not isinstance(values, list) or any(not isinstance(item, dict) or item.get("status") not in allowed for item in values):
            errors.append(f"cohort {key} must be terminal")
    approvals = cohort.get("approvals")
    roles = {item.get("role") for item in approvals if isinstance(item, dict) and item.get("status") == "APPROVED"} if isinstance(approvals, list) else set()
    if "A3" not in roles or not ({"A4", "A6"} & roles):
        errors.append("cohort requires A3 and A4-or-A6 approvals")
    return errors


def evaluate_closed(candidate: JsonObject) -> GateResult:
    ready = evaluate_ready(candidate)
    errors = list(ready.errors)
    errors.extend(_cohort_errors(candidate))
    return GateResult("EXTERNAL_BETA_CLOSED", not errors, tuple(errors), ready.blocked_ids)


def evaluate_gate(candidate: JsonObject, state: str = "EXTERNAL_BETA_READY") -> GateResult:
    normalized = state.upper().replace(" ", "_")
    if normalized == "EXTERNAL_BETA_CLOSED":
        return evaluate_closed(candidate)
    if normalized == "EXTERNAL_BETA_READY":
        return evaluate_ready(candidate)
    return GateResult(normalized, False, (f"unsupported External Beta state: {state}",))


def read_generated_identity(path: Path) -> ReleaseIdentity:
    text = path.read_text(encoding="utf-8")
    values = {
        "version": re.search(r'kApplicationVersion\{"([^"\n]+)"\}', text),
        "build_id": re.search(r'kBuildId\{"([^"\n]+)"\}', text),
        "source_commit": re.search(r'kSourceCommit\{"([^"\n]+)"\}', text),
        "build_epoch": re.search(r"kBuildEpoch\{(\d+)", text),
    }
    if any(match is None for match in values.values()):
        raise ValueError(f"generated build identity is incomplete: {path}")
    return ReleaseIdentity("Project SEAM", values["version"].group(1), values["build_id"].group(1), values["source_commit"].group(1), int(values["build_epoch"].group(1)))


def read_source_identity(root: Path) -> ReleaseIdentity:
    text = (Path(root) / "CMakeLists.txt").read_text(encoding="utf-8")
    project = re.search(r"project\(ProjectSEAM VERSION ([0-9]+(?:\.[0-9]+)+)", text)
    if project is None:
        raise ValueError("ProjectSEAM CMake project version is missing")
    build = re.search(r'set\(SEAM_BUILD_ID "([^"]+)"', text)
    commit = re.search(r'set\(SEAM_SOURCE_COMMIT "([^"]+)"', text)
    epoch = re.search(r'set\(SEAM_BUILD_EPOCH (\d+)', text)
    return ReleaseIdentity("Project SEAM", project.group(1), build.group(1) if build else f"{project.group(1)}-source", commit.group(1) if commit else "0" * 40, int(epoch.group(1)) if epoch else 0)


def compare_identity(expected: ReleaseIdentity, actual: ReleaseIdentity) -> list[str]:
    return [f"{key} differs: {getattr(expected, key)} != {getattr(actual, key)}" for key in ("product", "version", "build_id", "source_commit", "build_epoch") if getattr(expected, key) != getattr(actual, key)]


def load_candidate(path: Path) -> JsonObject:
    raw = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise ValueError("candidate JSON root must be an object")
    return raw


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Project SEAM External Beta fail-closed release gate")
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--state", default="EXTERNAL_BETA_READY", choices=("EXTERNAL_BETA_READY", "EXTERNAL_BETA_CLOSED"))
    parser.add_argument("--json-output", type=Path)
    parser.add_argument("--expect-blocked", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = evaluate_gate(load_candidate(args.candidate), args.state)
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
        print(json.dumps({"state": args.state, "passed": False, "errors": [str(exc)]}, sort_keys=True))
        return 2
    payload = canonical_json(result.as_dict())
    print(payload)
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(payload + "\n", encoding="utf-8")
    if args.expect_blocked:
        return 0 if not result.passed else 4
    return 0 if result.passed else 3


if __name__ == "__main__":
    raise SystemExit(main())
