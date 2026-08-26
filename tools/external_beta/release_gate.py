from __future__ import annotations

import argparse
import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path

try:
    from .cohort_gate import validate_cohort
    from .release_gate_validation import (
        HEX40 as HEX40,
        HEX64 as HEX64,
        JsonObject as JsonObject,
        JsonValue as JsonValue,
        _base_errors as _base_errors,
        _digest as _digest,
        _evidence_errors as _evidence_errors,
        _identity_errors as _identity_errors,
        _lineage_errors as _lineage_errors,
        _requirement_errors as _requirement_errors,
        candidate_root_sha256 as candidate_root_sha256,
        stage_graph_sha256 as stage_graph_sha256,
        stable_machine_sha256 as stable_machine_sha256,
        stable_workload_sha256 as stable_workload_sha256,
    )
    from .release_gate_policy import requirement_policy_errors
except ImportError:
    from cohort_gate import validate_cohort
    from release_gate_validation import (
        HEX40 as HEX40,
        HEX64 as HEX64,
        JsonObject as JsonObject,
        JsonValue as JsonValue,
        _base_errors as _base_errors,
        _digest as _digest,
        _evidence_errors as _evidence_errors,
        _identity_errors as _identity_errors,
        _lineage_errors as _lineage_errors,
        _requirement_errors as _requirement_errors,
        candidate_root_sha256 as candidate_root_sha256,
        stage_graph_sha256 as stage_graph_sha256,
        stable_machine_sha256 as stable_machine_sha256,
        stable_workload_sha256 as stable_workload_sha256,
    )
    from release_gate_policy import requirement_policy_errors

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


class ReleaseGateInputError(ValueError):
    pass


def canonical_json(value: JsonValue) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def sha256_json(value: JsonValue) -> str:
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def evaluate_ready(
    candidate: JsonObject,
    acceptance_contract: JsonObject | None = None,
    *,
    archive_verified: bool = False,
) -> GateResult:
    contract = (
        acceptance_contract
        if acceptance_contract is not None
        else load_candidate(
            Path(__file__).resolve().parents[2]
            / "docs/product/external-beta-acceptance.json"
        )
    )
    errors = _base_errors(candidate)
    if not archive_verified:
        errors.append("verified restored archive audit is required for READY")
    errors.extend(requirement_policy_errors(candidate, READY_REQUIREMENT_IDS, contract))
    requirement_errors, blocked = _requirement_errors(candidate, READY_REQUIREMENT_IDS)
    errors.extend(requirement_errors)
    return GateResult("EXTERNAL_BETA_READY", not errors and not blocked, tuple(errors), blocked)


def _cohort_errors(candidate: JsonObject) -> list[str]:
    cohort = candidate.get("cohort")
    if not isinstance(cohort, dict):
        return ["cohort evidence is required for EXTERNAL_BETA_CLOSED"]
    return [f"cohort: {error}" for error in validate_cohort(cohort, "CLOSED").errors]


def evaluate_closed(
    candidate: JsonObject,
    acceptance_contract: JsonObject | None = None,
    *,
    archive_verified: bool = False,
) -> GateResult:
    ready = evaluate_ready(
        candidate, acceptance_contract, archive_verified=archive_verified
    )
    errors = list(ready.errors)
    errors.extend(_cohort_errors(candidate))
    return GateResult("EXTERNAL_BETA_CLOSED", not errors, tuple(errors), ready.blocked_ids)


def evaluate_gate(
    candidate: JsonObject,
    state: str = "EXTERNAL_BETA_READY",
    acceptance_contract: JsonObject | None = None,
    *,
    archive_verified: bool = False,
) -> GateResult:
    normalized = state.upper().replace(" ", "_")
    if normalized == "EXTERNAL_BETA_CLOSED":
        return evaluate_closed(
            candidate, acceptance_contract, archive_verified=archive_verified
        )
    if normalized == "EXTERNAL_BETA_READY":
        return evaluate_ready(
            candidate, acceptance_contract, archive_verified=archive_verified
        )
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
        raise ReleaseGateInputError(f"generated build identity is incomplete: {path}")
    return ReleaseIdentity("Project SEAM", values["version"].group(1), values["build_id"].group(1), values["source_commit"].group(1), int(values["build_epoch"].group(1)))


def read_source_identity(root: Path) -> ReleaseIdentity:
    text = (Path(root) / "CMakeLists.txt").read_text(encoding="utf-8")
    project = re.search(r"project\(ProjectSEAM VERSION ([0-9]+(?:\.[0-9]+)+)", text)
    if project is None:
        raise ReleaseGateInputError("ProjectSEAM CMake project version is missing")
    build = re.search(r'set\(SEAM_BUILD_ID "([^"]+)"', text)
    commit = re.search(r'set\(SEAM_SOURCE_COMMIT "([^"]+)"', text)
    epoch = re.search(r'set\(SEAM_BUILD_EPOCH (\d+)', text)
    return ReleaseIdentity("Project SEAM", project.group(1), build.group(1) if build else f"{project.group(1)}-source", commit.group(1) if commit else "0" * 40, int(epoch.group(1)) if epoch else 0)


def compare_identity(expected: ReleaseIdentity, actual: ReleaseIdentity) -> list[str]:
    return [f"{key} differs: {getattr(expected, key)} != {getattr(actual, key)}" for key in ("product", "version", "build_id", "source_commit", "build_epoch") if getattr(expected, key) != getattr(actual, key)]


def load_candidate(path: Path) -> JsonObject:
    raw = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise ReleaseGateInputError("candidate JSON root must be an object")
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
