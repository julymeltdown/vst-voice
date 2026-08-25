#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))

from distribution_manifest import build_release_manifest, read_validation_status, tree_sha256  # noqa: E402


def _load_object(path: Path) -> dict[str, object]:
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"regular JSON file is required: {path}")
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON object is required: {path}")
    return value


def _artifact_path(result: dict[str, object], format_name: str) -> Path:
    artifacts = result.get("artifacts")
    if not isinstance(artifacts, list):
        raise ValueError("build result artifacts must be an array")
    for item in artifacts:
        if isinstance(item, dict) and item.get("format") == format_name:
            value = item.get("path")
            if isinstance(value, str) and value:
                path = Path(value)
                if path.exists() and not path.is_symlink():
                    return path
                raise ValueError(f"build result artifact is missing or linked: {path}")
    raise ValueError(f"build result artifact is missing: {format_name}")


def _attach(
    result: dict[str, object],
    result_path: Path,
    validation_name: str,
    format_name: str,
    hash_field: str,
) -> str:
    artifact = _artifact_path(result, format_name)
    status = read_validation_status(
        result_path,
        expected_artifact_sha256=tree_sha256(artifact),
        artifact_hash_field=hash_field,
    )
    validation = result.setdefault("validation", {})
    evidence = result.setdefault("validationEvidence", {})
    if not isinstance(validation, dict) or not isinstance(evidence, dict):
        raise ValueError("build result validation fields must be objects")
    validation[validation_name] = status
    evidence[validation_name] = str(result_path.resolve())
    return status


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Attach Phase 13A validator records to an existing build manifest")
    parser.add_argument("--build-result", type=Path, required=True)
    parser.add_argument("--vst3-validation-result", type=Path)
    parser.add_argument("--auval-validation-result", type=Path)
    args = parser.parse_args(argv)
    if args.vst3_validation_result is None and args.auval_validation_result is None:
        parser.error("at least one validator result is required")
    try:
        result = _load_object(args.build_result)
        if not isinstance(result.get("version"), str) or not result["version"]:
            raise ValueError("build result version is required")
        if args.vst3_validation_result is not None:
            _attach(result, args.vst3_validation_result, "vst3-validator", "VST3", "pluginSha256")
        if args.auval_validation_result is not None:
            _attach(result, args.auval_validation_result, "auval", "AUv2", "componentSha256")
        validation = result.get("validation")
        evidence = result.get("validationEvidence")
        artifacts = result.get("artifacts")
        if not isinstance(validation, dict) or not isinstance(evidence, dict) or not isinstance(artifacts, list):
            raise ValueError("build result manifest fields are invalid")
        result["releaseManifest"] = build_release_manifest(
            result["version"], artifacts, validation, evidence
        )
        args.build_result.write_text(
            json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
