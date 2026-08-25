#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path

try:
    from .host_certification_validation import (
        CHECK_NAMES,
        JsonObject,
        JsonValue,
        validate_record,
    )
except ImportError:
    from host_certification_validation import (
        CHECK_NAMES,
        JsonObject,
        JsonValue,
        validate_record,
    )

__all__ = ["CHECK_NAMES", "JsonObject", "apply_record", "main", "validate_record"]


class HostCertificationInputError(ValueError):
    pass


def apply_record(matrix: JsonObject, record: JsonObject) -> JsonObject:
    updated = copy.deepcopy(matrix)
    targets = updated.get("targets")
    if not isinstance(targets, list):
        raise HostCertificationInputError("validation matrix targets must be an array")
    target_id = record.get("targetId")
    matches = [
        target
        for target in targets
        if isinstance(target, dict) and target.get("id") == target_id
    ]
    if len(matches) != 1:
        raise HostCertificationInputError(
            f"targetId must match exactly one validation target: {target_id}"
        )
    target = matches[0]
    target["runtimeResult"] = record["runtimeResult"]
    if record["runtimeResult"] == "PASS":
        target["implementationState"] = "TARGET_BUILD_PASS"
        target["evidence"] = [
            {
                "candidateBuildId": record["candidateBuildId"],
                "candidateManifestSha256": record["candidateManifestSha256"],
                "osVersion": record["osVersion"],
                "hostVersion": record["hostVersion"],
                "pluginFormat": record["pluginFormat"],
                "pluginSha256": record["pluginSha256"],
                "artifactPath": record["artifactPath"],
                "artifactTreeSha256": record["artifactTreeSha256"],
                "toolIdentity": copy.deepcopy(record["toolIdentity"]),
                "executedAt": record["executedAt"],
                "executor": record["executor"],
                "checks": copy.deepcopy(record["checks"]),
                "logs": copy.deepcopy(record["evidence"]),
                "evidenceSha256": copy.deepcopy(record["evidenceSha256"]),
            }
        ]
    else:
        target["evidence"] = []
    return updated


def _load_object(path: Path) -> JsonObject:
    raw: JsonValue = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise HostCertificationInputError(f"JSON root must be an object: {path}")
    return raw


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Record actual target OS and commercial DAW certification evidence"
    )
    parser.add_argument("--matrix", type=Path, required=True)
    parser.add_argument("--record", type=Path, required=True)
    parser.add_argument("--evidence-root", type=Path, required=True)
    parser.add_argument("--candidate-manifest", type=Path, required=True)
    parser.add_argument("--installed-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        matrix = _load_object(args.matrix)
        record = _load_object(args.record)
        candidate_manifest = _load_object(args.candidate_manifest)
        errors = validate_record(
            record, args.evidence_root, candidate_manifest, args.installed_root
        )
        if errors:
            for error in errors:
                print(f"ERROR: {error}")
            return 3
        updated = apply_record(matrix, record)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(updated, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
    except (
        OSError,
        UnicodeError,
        HostCertificationInputError,
        json.JSONDecodeError,
    ) as exc:
        print(f"ERROR: {exc}")
        return 2
    print(f"PHASE13A_CERTIFICATION_RECORDED={record['targetId']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
