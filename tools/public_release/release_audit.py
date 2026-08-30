from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from .archive_validation import audit_archive
from .contracts import JsonObject, ReleaseGateInputError
from .release_gate import evaluate_gate


@dataclass(frozen=True, slots=True)
class ReleaseAuditResult:
    passed: bool
    state: str
    errors: tuple[str, ...] = ()
    blocked: tuple[str, ...] = ()

    def as_dict(self) -> JsonObject:
        return {
            "passed": self.passed,
            "state": self.state,
            "errors": list(self.errors),
            "blocked": list(self.blocked),
        }


def audit_release(
    candidate: JsonObject,
    manifest: JsonObject,
    root: Path,
    state: str = "PUBLIC_ACTIVE",
    *,
    acceptance_contract: JsonObject | None = None,
) -> ReleaseAuditResult:
    archive_errors = audit_archive(candidate, manifest, root)
    gate = evaluate_gate(
        candidate,
        state,
        acceptance_contract=acceptance_contract,
        archive_verified=not archive_errors,
    )
    errors = tuple(
        [
            *(f"archive: {error}" for error in archive_errors),
            *(f"gate: {error}" for error in gate.errors),
        ]
    )
    blocked_values = set(gate.blocked_ids)
    if archive_errors:
        blocked_values.add("archive-audit")
    blocked = tuple(sorted(blocked_values))
    return ReleaseAuditResult(not errors and not blocked, state, errors, blocked)


def load_json(path: Path) -> JsonObject:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ReleaseGateInputError(f"JSON root must be an object: {path}")
    return value
