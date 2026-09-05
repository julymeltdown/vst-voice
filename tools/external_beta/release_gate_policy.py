from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass

try:
    from .full_product_contract import full_product_contract_errors
except ImportError:
    from full_product_contract import full_product_contract_errors


JsonValue = (
    str | int | float | bool | None | list["JsonValue"] | dict[str, "JsonValue"]
)
JsonObject = dict[str, JsonValue]


@dataclass(frozen=True, slots=True)
class EvidenceTarget:
    platform: str
    architecture: str
    surface: str
    host: str | None

    def matches(self, record: JsonObject) -> bool:
        return (
            record.get("platform") == self.platform
            and record.get("architecture") == self.architecture
            and record.get("surface") == self.surface
            and record.get("host") == self.host
        )


@dataclass(frozen=True, slots=True)
class RequirementPolicy:
    requirement_id: str
    stage_kinds: frozenset[str]
    transformations: frozenset[str]
    minimum_records: int
    required_targets: tuple[EvidenceTarget, ...]


def contract_sha256(contract: JsonObject) -> str:
    encoded = json.dumps(
        contract,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    )
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest()


def _string_set(
    value: JsonValue,
    label: str,
    errors: list[str],
) -> frozenset[str]:
    if not isinstance(value, list) or not value:
        errors.append(f"{label} must be a non-empty array")
        return frozenset()
    strings = [item for item in value if isinstance(item, str) and item]
    if len(strings) != len(value) or len(set(strings)) != len(strings):
        errors.append(f"{label} must contain unique non-empty strings")
    return frozenset(strings)


def _target(
    value: JsonValue,
    label: str,
    errors: list[str],
) -> EvidenceTarget | None:
    if not isinstance(value, dict):
        errors.append(f"{label} must be an object")
        return None
    fields = {
        key: value.get(key) for key in ("platform", "architecture", "surface")
    }
    for key, field in fields.items():
        if not isinstance(field, str) or not field:
            errors.append(f"{label}.{key} is required")
    host = value.get("host")
    if host is not None and (not isinstance(host, str) or not host):
        errors.append(f"{label}.host must be null or a non-empty string")
    if errors and any(error.startswith(label) for error in errors):
        return None
    return EvidenceTarget(
        platform=fields["platform"],
        architecture=fields["architecture"],
        surface=fields["surface"],
        host=host,
    )


def _requirement_policy(
    value: JsonValue,
    errors: list[str],
) -> RequirementPolicy | None:
    if not isinstance(value, dict):
        errors.append("acceptance contract requirement must be an object")
        return None
    requirement_id = value.get("id")
    if not isinstance(requirement_id, str) or not requirement_id:
        errors.append("acceptance contract requirement id is required")
        return None
    label = f"acceptance contract {requirement_id}.evidencePolicy"
    policy = value.get("evidencePolicy")
    if not isinstance(policy, dict):
        errors.append(f"{label} is required")
        return None
    stage_kinds = _string_set(policy.get("stageKinds"), f"{label}.stageKinds", errors)
    transformations = _string_set(
        policy.get("transformations"),
        f"{label}.transformations",
        errors,
    )
    minimum_records = policy.get("minimumRecords")
    if (
        not isinstance(minimum_records, int)
        or isinstance(minimum_records, bool)
        or minimum_records < 1
    ):
        errors.append(f"{label}.minimumRecords must be a positive integer")
        minimum_records = 1
    target_values = policy.get("requiredTargets")
    targets: list[EvidenceTarget] = []
    if not isinstance(target_values, list) or not target_values:
        errors.append(f"{label}.requiredTargets must be a non-empty array")
    else:
        for index, target_value in enumerate(target_values):
            parsed = _target(target_value, f"{label}.requiredTargets[{index}]", errors)
            if parsed is not None:
                targets.append(parsed)
    return RequirementPolicy(
        requirement_id=requirement_id,
        stage_kinds=stage_kinds,
        transformations=transformations,
        minimum_records=minimum_records,
        required_targets=tuple(targets),
    )


def _policies(
    contract: JsonObject,
    required_ids: tuple[str, ...],
    errors: list[str],
) -> dict[str, RequirementPolicy]:
    values = contract.get("requirements")
    if not isinstance(values, list):
        errors.append("acceptance contract requirements must be an array")
        return {}
    policies: dict[str, RequirementPolicy] = {}
    for value in values:
        policy = _requirement_policy(value, errors)
        if policy is None:
            continue
        if policy.requirement_id in policies:
            errors.append(
                f"acceptance contract requirement is duplicated: {policy.requirement_id}"
            )
        policies[policy.requirement_id] = policy
    missing = sorted(set(required_ids) - set(policies))
    if missing:
        errors.append(
            "acceptance contract is missing evidence policy for: "
            + ", ".join(missing)
        )
    return policies


def requirement_policy_errors(
    candidate: JsonObject,
    required_ids: tuple[str, ...],
    contract: JsonObject,
) -> list[str]:
    errors = full_product_contract_errors(contract)
    expected_contract_sha256 = contract_sha256(contract)
    if candidate.get("acceptanceContractSha256") != expected_contract_sha256:
        errors.append("candidate acceptance contract digest does not match")
    candidate_root = candidate.get("candidateRoot")
    if (
        not isinstance(candidate_root, dict)
        or candidate_root.get("acceptanceContractSha256")
        != expected_contract_sha256
    ):
        errors.append("candidate root acceptance contract digest does not match")
    errors.extend(evidence_policy_errors(candidate, required_ids, contract))
    return errors


def evidence_policy_errors(
    candidate: JsonObject,
    required_ids: tuple[str, ...],
    contract: JsonObject,
) -> list[str]:
    errors: list[str] = []
    policies = _policies(contract, required_ids, errors)
    records_value = candidate.get("evidence")
    requirements = candidate.get("requirements")
    nodes_value = candidate.get("stageNodes")
    edges_value = candidate.get("stageEdges")
    if not all(
        isinstance(value, expected)
        for value, expected in (
            (records_value, list),
            (requirements, dict),
            (nodes_value, list),
            (edges_value, list),
        )
    ):
        return errors
    records = {
        value["recordId"]: value
        for value in records_value
        if isinstance(value, dict) and isinstance(value.get("recordId"), str)
    }
    nodes = {
        value["id"]: value
        for value in nodes_value
        if isinstance(value, dict) and isinstance(value.get("id"), str)
    }
    edges = {
        value["id"]: value
        for value in edges_value
        if isinstance(value, dict) and isinstance(value.get("id"), str)
    }
    for requirement_id in required_ids:
        policy = policies.get(requirement_id)
        item = requirements.get(requirement_id)
        if policy is None or not isinstance(item, dict):
            continue
        references = item.get("evidenceRecordIds")
        if not isinstance(references, list):
            continue
        matched = [records[ref] for ref in references if isinstance(ref, str) and ref in records]
        if len(matched) < policy.minimum_records:
            errors.append(
                f"requirement {requirement_id} needs at least "
                f"{policy.minimum_records} policy-matching records"
            )
        for record in matched:
            stage = nodes.get(record.get("stageNodeId"))
            edge = edges.get(record.get("parentEdgeId"))
            if not isinstance(stage, dict) or stage.get("kind") not in policy.stage_kinds:
                errors.append(f"requirement {requirement_id} has wrong stage kind")
            if (
                not isinstance(edge, dict)
                or edge.get("transformation") not in policy.transformations
            ):
                errors.append(f"requirement {requirement_id} has wrong transformation")
        for target in policy.required_targets:
            if not any(target.matches(record) for record in matched):
                errors.append(
                    f"requirement {requirement_id} platform/surface target coverage "
                    f"is missing: {target.platform}/{target.architecture}/"
                    f"{target.surface}/{target.host or '-'}"
                )
    return errors
