from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .cohort_gate import validate_cohort
from .evidence_audit import audit_candidate
from .release_gate import evaluate_gate


@dataclass(frozen=True, slots=True)
class ReleaseAuditResult:
    passed: bool
    state: str
    errors: tuple[str, ...] = ()
    blocked: tuple[str, ...] = ()

    def as_dict(self) -> dict[str, Any]:
        return {"passed": self.passed, "state": self.state, "errors": list(self.errors), "blocked": list(self.blocked)}


def audit_release(candidate: dict[str, Any], manifest: dict[str, Any], root: Path, state: str = "READY") -> ReleaseAuditResult:
    normalized = state.upper()
    if normalized not in {"READY", "CLOSED"}:
        return ReleaseAuditResult(False, normalized, (f"unsupported release audit state: {state}",), ("state",))
    archive_result = audit_candidate(candidate, manifest, root)
    gate_state = "EXTERNAL_BETA_CLOSED" if normalized == "CLOSED" else "EXTERNAL_BETA_READY"
    gate_result = evaluate_gate(candidate, gate_state)
    errors = [f"archive: {error}" for error in archive_result.errors]
    errors.extend(f"gate: {error}" for error in gate_result.errors)
    blocked = list(archive_result.blocked)
    blocked.extend(gate_result.blocked_ids)
    if normalized == "CLOSED":
        cohort = candidate.get("cohort") if isinstance(candidate, dict) else None
        if isinstance(cohort, dict):
            cohort_result = validate_cohort(cohort, "CLOSED")
            errors.extend(f"cohort: {error}" for error in cohort_result.errors if f"cohort: {error}" not in errors)
            blocked.extend(cohort_result.blocked)
        else:
            blocked.append("cohort")
    return ReleaseAuditResult(not errors and not blocked, normalized, tuple(errors), tuple(sorted(set(blocked))))


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Audit a restored External Beta candidate before READY or CLOSED")
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--archive-manifest", type=Path, required=True)
    parser.add_argument("--archive-root", type=Path, required=True)
    parser.add_argument("--state", choices=("READY", "CLOSED"), default="READY")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expect-blocked", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = audit_release(load_json(args.candidate), load_json(args.archive_manifest), args.archive_root, args.state)
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
        payload = {"passed": False, "state": args.state, "errors": [str(exc)], "blocked": []}
        print(json.dumps(payload, sort_keys=True))
        return 0 if args.expect_blocked else 2
    text = json.dumps(result.as_dict(), ensure_ascii=False, sort_keys=True) + "\n"
    print(text, end="")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    if args.expect_blocked:
        return 0 if not result.passed else 4
    return 0 if result.passed else 3


if __name__ == "__main__":
    raise SystemExit(main())
