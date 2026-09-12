#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


try:
    from tools.voicebank_script_generator import (
        DEFAULT_PROFILE,
        GENERATOR_VERSION,
        canonical_json,
        generate_inventory,
        normalize_profile,
        production_assignments,
        render_operator_csv,
        required_sequences,
        sha256_bytes,
        sha256_json,
        validate_inventory,
    )
except ModuleNotFoundError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from tools.voicebank_script_generator import (
        DEFAULT_PROFILE,
        GENERATOR_VERSION,
        canonical_json,
        generate_inventory,
        normalize_profile,
        production_assignments,
        render_operator_csv,
        required_sequences,
        sha256_bytes,
        sha256_json,
        validate_inventory,
    )


__all__ = [
    "DEFAULT_PROFILE",
    "GENERATOR_VERSION",
    "canonical_json",
    "generate_inventory",
    "normalize_profile",
    "production_assignments",
    "render_operator_csv",
    "required_sequences",
    "sha256_bytes",
    "sha256_json",
    "validate_inventory",
]

from tools.voicebank_script_generator.draft_inventory import (
    draft_production_assignments, generate_draft_inventory, render_draft_operator_csv,
)


def _load_profile(path: Path | None) -> dict:
    if path is None:
        return dict(DEFAULT_PROFILE)
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError("profile JSON root must be an object")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Generate deterministic Project SEAM Beta Voicebank recording scripts")
    parser.add_argument("--profile", type=Path)
    parser.add_argument("--draft", action="store_true",
                        help="Generate style-owned schema-2 draft inventory; no range qualification. Requires --profile.")
    parser.add_argument("--json-output", type=Path)
    parser.add_argument("--csv-output", type=Path)
    parser.add_argument("--production-assignments-output", type=Path)
    args = parser.parse_args(argv)
    if args.draft and args.profile is None:
        parser.error("--draft requires an explicit --profile; legacy default PASS is not draft evidence")
    try:
        inventory = (generate_draft_inventory if args.draft else generate_inventory)(_load_profile(args.profile))
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as exc:
        parser.error(str(exc))
    json_text = json.dumps(inventory, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n"
    csv_text = (render_draft_operator_csv if args.draft else render_operator_csv)(inventory)
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(json_text, encoding="utf-8", newline="\n")
    if args.csv_output:
        args.csv_output.parent.mkdir(parents=True, exist_ok=True)
        args.csv_output.write_text(csv_text, encoding="utf-8", newline="\n")
    if args.production_assignments_output:
        assignments = {
            "schemaVersion": 2 if args.draft else 1,
            "inventoryId": inventory["profileId"],
            "inventorySha256": inventory["inventorySha256"],
            "unitAssignments": (draft_production_assignments if args.draft else production_assignments)(inventory),
        }
        if args.draft:
            assignments.update(language=inventory["language"], requiredProducerSchemaVersion=4)
        args.production_assignments_output.parent.mkdir(parents=True, exist_ok=True)
        args.production_assignments_output.write_text(
            json.dumps(assignments, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    if not args.json_output and not args.csv_output and not args.production_assignments_output:
        print(json_text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
