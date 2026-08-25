from __future__ import annotations

import hashlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .freeze_candidate import canonical_json, sha256_json


@dataclass(frozen=True, slots=True)
class CandidateRootInputError(ValueError):
    message: str

    def __str__(self) -> str:
        return self.message


def file_digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _node(node_id: str, kind: str, digest: str, status: str = "SEALED") -> dict[str, Any]:
    if not node_id or not kind or len(digest) != 64:
        raise CandidateRootInputError(
            "candidate stage nodes require an id, kind, and SHA-256 digest"
        )
    return {"id": node_id, "kind": kind, "sha256": digest, "status": status}


def build_candidate_root(
    candidate_id: str,
    freeze_record: dict[str, Any],
    bank_package: Path,
    platform_deliverables: dict[str, Path],
    trust_policy: Path,
    documentation: Path,
    archive_anchor: Path,
) -> dict[str, Any]:
    if freeze_record.get("candidateId") != candidate_id or freeze_record.get("status") != "FROZEN_UNSIGNED":
        raise CandidateRootInputError(
            "candidate root requires the matching frozen unsigned record"
        )
    if set(platform_deliverables) != {"macos", "windows"}:
        raise CandidateRootInputError(
            "candidate root requires macos and windows deliverables"
        )
    paths = {"bank": bank_package, "macos": platform_deliverables["macos"], "windows": platform_deliverables["windows"], "trust": trust_policy, "docs": documentation, "archive": archive_anchor}
    if any(not path.is_file() for path in paths.values()):
        missing = next(label for label, path in paths.items() if not path.is_file())
        raise CandidateRootInputError(f"candidate root input is missing: {missing}")
    nodes = [
        _node(f"{candidate_id}:bank", "signed-bank", file_digest(bank_package)),
        _node(f"{candidate_id}:macos", "macos-deliverable", file_digest(platform_deliverables["macos"])),
        _node(f"{candidate_id}:windows", "windows-deliverable", file_digest(platform_deliverables["windows"])),
        _node(f"{candidate_id}:trust", "trust-policy", file_digest(trust_policy)),
        _node(f"{candidate_id}:docs", "documentation", file_digest(documentation)),
        _node(f"{candidate_id}:archive", "evidence-archive", file_digest(archive_anchor)),
    ]
    root_id = f"{candidate_id}:root"
    edges = [
        {"id": f"{root_id}:bank", "parent": root_id, "child": f"{candidate_id}:bank", "transformation": "candidate-envelope"},
        {"id": f"{root_id}:macos", "parent": root_id, "child": f"{candidate_id}:macos", "transformation": "signed-notarized-deliverable"},
        {"id": f"{root_id}:windows", "parent": root_id, "child": f"{candidate_id}:windows", "transformation": "signed-installer-deliverable"},
        {"id": f"{root_id}:trust", "parent": root_id, "child": f"{candidate_id}:trust", "transformation": "public-trust-policy"},
        {"id": f"{root_id}:docs", "parent": root_id, "child": f"{candidate_id}:docs", "transformation": "offline-manual-set"},
        {"id": f"{root_id}:archive", "parent": root_id, "child": f"{candidate_id}:archive", "transformation": "archive-anchor"},
    ]
    root = {
        "id": root_id,
        "candidateId": candidate_id,
        "status": "SEALED",
        "freezeSha256": freeze_record.get("freezeSha256"),
        "acceptanceContractSha256": freeze_record.get(
            "acceptanceContractSha256"
        ),
        "nodes": nodes,
        "edges": edges,
    }
    root["sha256"] = sha256_json(root)
    return {
        "schemaVersion": 1,
        "acceptanceContractSha256": freeze_record.get(
            "acceptanceContractSha256"
        ),
        "candidateRoot": root,
    }


def validate_candidate_root(candidate: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if candidate.get("schemaVersion") != 1:
        errors.append("schemaVersion must equal 1")
    root = candidate.get("candidateRoot")
    if not isinstance(root, dict):
        return ["candidateRoot must be an object"]
    if root.get("status") != "SEALED":
        errors.append("candidateRoot.status must be SEALED")
    acceptance_contract_sha256 = candidate.get("acceptanceContractSha256")
    if (
        not isinstance(acceptance_contract_sha256, str)
        or len(acceptance_contract_sha256) != 64
        or root.get("acceptanceContractSha256") != acceptance_contract_sha256
    ):
        errors.append("candidate acceptance contract digest is invalid")
    stored = root.get("sha256")
    unsigned = {key: value for key, value in root.items() if key != "sha256"}
    if stored != sha256_json(unsigned):
        errors.append("candidateRoot.sha256 does not match canonical root bytes")
    nodes = root.get("nodes")
    if not isinstance(nodes, list) or not nodes:
        errors.append("candidateRoot.nodes must be non-empty")
        nodes = []
    node_ids = set()
    for node in nodes:
        if not isinstance(node, dict) or not isinstance(node.get("id"), str):
            errors.append("every candidate node requires an id")
            continue
        if node["id"] in node_ids:
            errors.append(f"duplicate candidate node: {node['id']}")
        node_ids.add(node["id"])
        if node.get("status") != "SEALED":
            errors.append(f"candidate node is not sealed: {node['id']}")
        if not isinstance(node.get("sha256"), str) or len(node["sha256"]) != 64:
            errors.append(f"candidate node digest is invalid: {node['id']}")
    edges = root.get("edges")
    if not isinstance(edges, list) or not edges:
        errors.append("candidateRoot.edges must be non-empty")
        edges = []
    edge_ids = set()
    for edge in edges:
        if not isinstance(edge, dict) or not isinstance(edge.get("id"), str):
            errors.append("every candidate edge requires an id")
            continue
        if edge["id"] in edge_ids:
            errors.append(f"duplicate candidate edge: {edge['id']}")
        edge_ids.add(edge["id"])
        if edge.get("parent") != root.get("id") or edge.get("child") not in node_ids:
            errors.append(f"candidate edge is not rooted: {edge['id']}")
        if not isinstance(edge.get("transformation"), str) or not edge["transformation"]:
            errors.append(f"candidate edge transformation is required: {edge['id']}")
    return errors


def create_cohort_envelope(
    candidate: dict[str, Any], platform: str, members: dict[str, Path]
) -> dict[str, Any]:
    errors = validate_candidate_root(candidate)
    if errors:
        raise CandidateRootInputError("; ".join(errors))
    if platform not in {"macos", "windows"}:
        raise CandidateRootInputError("cohort envelope platform is unsupported")
    if not members:
        raise CandidateRootInputError(
            "cohort envelope requires at least one member"
        )
    entries = []
    for name, path in sorted(members.items()):
        if not name or Path(name).is_absolute() or ".." in Path(name).parts:
            raise CandidateRootInputError(f"cohort member path is unsafe: {name}")
        if not path.is_file():
            raise CandidateRootInputError(f"cohort member is missing: {name}")
        entries.append({"name": name, "sha256": file_digest(path), "size": path.stat().st_size})
    manifest = {
        "schemaVersion": 1,
        "candidateRootId": candidate["candidateRoot"]["id"],
        "platform": platform,
        "members": entries,
    }
    manifest["manifestSha256"] = sha256_json(manifest)
    return manifest


def write_candidate_root(candidate: dict[str, Any], path: Path) -> None:
    errors = validate_candidate_root(candidate)
    if errors:
        raise CandidateRootInputError("; ".join(errors))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(canonical_json(candidate) + "\n", encoding="utf-8")
