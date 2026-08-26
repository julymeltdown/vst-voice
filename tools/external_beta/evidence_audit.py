from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any

from .evidence_archive import validate_archive_manifest


@dataclass(frozen=True, slots=True)
class EvidenceAuditResult:
    passed: bool
    errors: tuple[str, ...] = ()
    blocked: tuple[str, ...] = ()

    def as_dict(self) -> dict[str, Any]:
        return {"passed": self.passed, "errors": list(self.errors), "blocked": list(self.blocked)}


def _time(value: Any) -> datetime | None:
    if not isinstance(value, str) or not value:
        return None
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None
    return parsed


def audit_candidate(candidate: dict[str, Any], manifest: dict[str, Any], root: Path, *, trusted_anchor_sha256: str | None = None) -> EvidenceAuditResult:
    errors = list(validate_archive_manifest(manifest, root))
    blocked: list[str] = []
    if not isinstance(candidate, dict):
        return EvidenceAuditResult(False, tuple(errors + ["candidate must be an object"]), ())
    candidate_root = candidate.get("candidateRoot")
    if not isinstance(candidate_root, dict) or candidate_root.get("status") != "SEALED":
        errors.append("candidateRoot must be SEALED")
        blocked.append("candidateRoot")
    candidate_root_id = candidate_root.get("id") if isinstance(candidate_root, dict) else None
    if candidate_root_id != manifest.get("candidateRootId"):
        errors.append("candidate root does not match archive manifest")
    if not isinstance(candidate_root_id, str) or not candidate_root_id:
        errors.append("candidateRoot.id is required")
    archive = candidate.get("archive")
    if not isinstance(archive, dict) or archive.get("anchored") is not True or archive.get("immutable") is not True:
        errors.append("candidate archive must be anchored and immutable")
    elif archive.get("sha256") != manifest.get("manifestSha256"):
        errors.append("candidate archive hash does not match restored archive manifest")
    anchor = manifest.get("anchor")
    if not isinstance(trusted_anchor_sha256, str) or anchor is None or not isinstance(anchor, dict) or anchor.get("sha256") != trusted_anchor_sha256:
        errors.append("trusted archive anchor digest is required and must match the restored manifest")
    entries = {entry.get("path"): entry for entry in manifest.get("entries", []) if isinstance(entry, dict)}
    created_at = _time(manifest.get("createdAt"))
    records = candidate.get("evidence")
    if not isinstance(records, list) or not records:
        errors.append("candidate evidence must be non-empty")
        records = []
        blocked.append("evidence")
    seen: set[str] = set()
    for index, record in enumerate(records):
        label = f"evidence[{index}]"
        if not isinstance(record, dict):
            errors.append(f"{label} must be an object")
            continue
        record_id = record.get("recordId")
        if not isinstance(record_id, str) or not record_id:
            errors.append(f"{label}.recordId is required")
        elif record_id in seen:
            errors.append(f"duplicate evidence record: {record_id}")
        else:
            seen.add(record_id)
        if record.get("candidateRootId") != candidate_root_id:
            errors.append(f"{label}.candidateRootId does not match candidate root")
        if record.get("status") != "PASS":
            errors.append(f"{label}.status must be PASS")
            blocked.append(str(record_id))
        roles = record.get("roles")
        if not isinstance(roles, dict) or roles.get("producer") not in {"A3", "A4", "A5", "A6"} or roles.get("reviewer") not in {"A4", "A6"}:
            errors.append(f"{label}.roles must include an independent A4/A6 reviewer")
        elif roles.get("producer") == roles.get("reviewer"):
            errors.append(f"{label}.roles producer and reviewer must be independent")
        trusted_time = _time(record.get("trustedTime"))
        if trusted_time is None:
            errors.append(f"{label}.trustedTime must be ISO-8601")
        elif created_at is not None and trusted_time > created_at:
            errors.append(f"{label}.trustedTime occurs after archive creation")
        raw = record.get("rawArchive")
        if not isinstance(raw, dict) or not isinstance(raw.get("locator"), str) or not raw["locator"]:
            errors.append(f"{label}.rawArchive locator is required")
            continue
        entry = entries.get(raw["locator"])
        if entry is None:
            errors.append(f"{label}.rawArchive is not restored in the archive")
            continue
        if raw.get("sha256") != entry.get("sha256"):
            errors.append(f"{label}.raw archive hash does not match restored entry")
        if entry.get("candidateRootId") not in {None, candidate_root_id}:
            errors.append(f"{label}.archive entry belongs to another candidate")
        archive_path = root / raw["locator"]
        try:
            archived_record = json.loads(archive_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            errors.append(f"{label}.raw archive must be a serialized evidence record: {exc}")
            continue
        if not isinstance(archived_record, dict):
            errors.append(f"{label}.raw archive must be a serialized evidence object")
            continue
        for key, value in record.items():
            if key == "rawArchive":
                continue
            if archived_record.get(key) != value:
                errors.append(f"{label}.raw archive {key} does not match candidate evidence")
    if errors:
        blocked.append("archive-audit")
    return EvidenceAuditResult(not errors and not blocked, tuple(errors), tuple(sorted(set(blocked))))


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Audit External Beta candidate evidence from a restored archive")
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--archive-manifest", type=Path, required=True)
    parser.add_argument("--archive-root", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expect-blocked", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = audit_candidate(load_json(args.candidate), load_json(args.archive_manifest), args.archive_root)
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
        payload = {"passed": False, "errors": [str(exc)], "blocked": []}
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
