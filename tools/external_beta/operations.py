from __future__ import annotations

import argparse
import copy
import json
import os
import stat
from datetime import datetime
from pathlib import Path
from typing import Any

from .full_product_report import _parse_json, _read_regular_reference
from .release_audit import audit_release
from .release_gate_validation import HEX64


STATES = {"FROZEN", "READY", "COHORT_ACTIVE", "DISTRIBUTION_PAUSED", "REVOKED", "CLOSED"}
DISTRIBUTABLE_STATES = {"READY", "COHORT_ACTIVE"}
APPROVAL_ROLES = {"A3", "A4", "A5", "A6"}


def can_distribute(state: str) -> bool:
    return state in DISTRIBUTABLE_STATES


def _time(value: Any) -> bool:
    if not isinstance(value, str) or not value:
        return False
    try:
        datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return False
    return True


def validate_snapshot(snapshot: dict[str, Any]) -> None:
    if not isinstance(snapshot, dict) or snapshot.get("schemaVersion") != 1:
        raise ValueError("operation snapshot schemaVersion must be 1")
    if not isinstance(snapshot.get("candidateRootId"), str) or not snapshot["candidateRootId"]:
        raise ValueError("operation snapshot candidateRootId is required")
    if snapshot.get("state") not in STATES:
        raise ValueError("operation snapshot state is invalid")
    log = snapshot.get("decisionLog")
    if not isinstance(log, list):
        raise ValueError("operation snapshot decisionLog must be an array")
    ids: set[str] = set()
    for item in log:
        if not isinstance(item, dict) or not isinstance(item.get("decisionId"), str) or not item["decisionId"]:
            raise ValueError("every operation decision requires a decisionId")
        if item["decisionId"] in ids:
            raise ValueError("operation decisionId must be unique")
        ids.add(item["decisionId"])


def _decision_base(snapshot: dict[str, Any], decision: dict[str, Any]) -> None:
    if not isinstance(decision, dict) or decision.get("schemaVersion") != 1:
        raise ValueError("operation decision schemaVersion must be 1")
    for key in ("decisionId", "action", "candidateRootId", "actorRole", "createdAt"):
        if not decision.get(key):
            raise ValueError(f"operation decision {key} is required")
    if decision.get("candidateRootId") != snapshot["candidateRootId"]:
        raise ValueError("operation decision candidateRootId differs from snapshot")
    if decision.get("actorRole") not in APPROVAL_ROLES:
        raise ValueError("operation decision actorRole is invalid")
    if not _time(decision.get("createdAt")):
        raise ValueError("operation decision createdAt must be ISO-8601")
    if decision["decisionId"] in {item.get("decisionId") for item in snapshot["decisionLog"]}:
        raise ValueError("operation decisionId has already been recorded")


def _approved(roles: Any) -> bool:
    return isinstance(roles, list) and "A3" in roles and bool({"A4", "A6"} & set(roles))


def _reproduce_audit(snapshot: dict[str, Any], decision: dict[str, Any], base: Path, state: str) -> dict[str, Any]:
    """Reopen the requested bytes and reproduce the real audit on every promotion.

    No cached result, caller boolean, synthetic contract, or injectable evaluator
    is accepted here. The existing archive auditor verifies its trusted anchor.
    """
    digest = snapshot.get("candidateRootSha256")
    if not isinstance(digest, str) or HEX64.fullmatch(digest) is None or decision.get("candidateRootSha256") != digest:
        raise ValueError("promotion requires the exact snapshot and decision candidateRootSha256")
    inputs = decision.get("releaseAudit")
    if not isinstance(inputs, dict) or set(inputs) != {"candidate", "archiveManifest", "archiveRoot"}:
        raise ValueError("promotion requires candidate/archiveManifest references and the restored archiveRoot")
    archive_root = inputs.get("archiveRoot")
    if not isinstance(archive_root, str) or not archive_root:
        raise ValueError("releaseAudit.archiveRoot is required")
    root = Path(archive_root)
    if not root.is_absolute():
        root = base / root
    if root.is_symlink() or not root.is_dir():
        raise ValueError("releaseAudit.archiveRoot must be an existing real directory")
    candidate = _parse_json(_read_regular_reference(inputs["candidate"], base=base, label="releaseAudit.candidate", maximum_bytes=64 * 1024 * 1024))
    manifest = _parse_json(_read_regular_reference(inputs["archiveManifest"], base=base, label="releaseAudit.archiveManifest", maximum_bytes=64 * 1024 * 1024))
    if not isinstance(candidate, dict) or not isinstance(manifest, dict):
        raise ValueError("release audit inputs must be JSON objects")
    candidate_root = candidate.get("candidateRoot")
    if not isinstance(candidate_root, dict) or candidate_root.get("id") != snapshot["candidateRootId"] or candidate_root.get("sha256") != digest:
        raise ValueError("release audit candidate differs from the exact operation snapshot")
    result = audit_release(candidate, manifest, root, state)
    if not result.passed:
        raise ValueError("reproduced release audit failed: " + "; ".join(result.errors[:8]))
    return {"state": result.state, "candidateRootSha256": digest,
            "candidateSha256": inputs["candidate"]["sha256"],
            "archiveManifestSha256": inputs["archiveManifest"]["sha256"]}


def transition(snapshot: dict[str, Any], decision: dict[str, Any], *, base: Path | None = None) -> dict[str, Any]:
    validate_snapshot(snapshot)
    _decision_base(snapshot, decision)
    current = snapshot["state"]
    action = decision["action"]
    if current in {"REVOKED", "CLOSED"}:
        raise ValueError(f"{current} candidate cannot transition")
    next_state: str
    audit_receipt = None
    if action == "PROMOTE_READY":
        if current != "FROZEN" or not _approved(decision.get("approvals")):
            raise ValueError("PROMOTE_READY requires a passing audit and A3 plus A4/A6 approval")
        audit_receipt = _reproduce_audit(snapshot, decision, base or Path.cwd(), "READY")
        next_state = "READY"
    elif action == "START_COHORT":
        if current != "READY" or not decision.get("consentVersion"):
            raise ValueError("START_COHORT requires READY and a consent version")
        audit_receipt = _reproduce_audit(snapshot, decision, base or Path.cwd(), "READY")
        next_state = "COHORT_ACTIVE"
    elif action == "PAUSE":
        if current not in {"READY", "COHORT_ACTIVE"} or not decision.get("reason"):
            raise ValueError("PAUSE requires a distributable state and reason")
        next_state = "DISTRIBUTION_PAUSED"
    elif action == "RESUME":
        if current != "DISTRIBUTION_PAUSED" or not _approved(decision.get("approvals")):
            raise ValueError("RESUME requires fresh GO and A3 plus A4/A6 approval")
        audit_receipt = _reproduce_audit(snapshot, decision, base or Path.cwd(), "READY")
        next_state = "COHORT_ACTIVE"
    elif action == "REVOKE":
        if not decision.get("reason"):
            raise ValueError("REVOKE requires a reason")
        next_state = "REVOKED"
    elif action == "CLOSE":
        if current != "COHORT_ACTIVE":
            raise ValueError("CLOSE requires active cohort, passing audit, and ended evaluation window")
        audit_receipt = _reproduce_audit(snapshot, decision, base or Path.cwd(), "CLOSED")
        next_state = "CLOSED"
    else:
        raise ValueError(f"unsupported operation action: {action}")
    updated = copy.deepcopy(snapshot)
    updated["state"] = next_state
    recorded = copy.deepcopy(decision)
    # These historical assertions never authorize a transition. Record only the
    # actual audit receipt, not a caller-provided result masquerading as ours.
    for key in ("auditPassed", "freshGo", "cohortAuditPassed", "reproducedAudit"):
        recorded.pop(key, None)
    if audit_receipt is not None:
        recorded["reproducedAudit"] = audit_receipt
    updated["decisionLog"].append(recorded)
    return updated


def load_json(path: Path) -> dict[str, Any]:
    # No-follow, bounded input with duplicate-key and nonfinite rejection.
    maximum = 64 * 1024 * 1024
    before = path.lstat()
    if not stat.S_ISREG(before.st_mode) or before.st_size > maximum:
        raise ValueError("operation input must be a bounded regular file")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0))
    with os.fdopen(descriptor, "rb") as stream:
        opened = os.fstat(stream.fileno())
        if not stat.S_ISREG(opened.st_mode) or (opened.st_dev, opened.st_ino) != (before.st_dev, before.st_ino):
            raise ValueError("operation input changed while opening")
        contents = stream.read(maximum + 1)
    if len(contents) > maximum:
        raise ValueError("operation input exceeds its byte limit")
    value = _parse_json(contents)
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Apply one fail-closed External Beta operation decision")
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--decision", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expect-blocked", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = transition(load_json(args.snapshot), load_json(args.decision), base=args.decision.absolute().parent)
        payload = {"passed": True, "state": result["state"], "snapshot": result}
        exit_code = 4 if args.expect_blocked else 0
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
        payload = {"passed": False, "errors": [str(exc)], "blocked": ["operation"]}
        exit_code = 0 if args.expect_blocked else 2
    text = json.dumps(payload, ensure_ascii=False, sort_keys=True) + "\n"
    print(text, end="")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
