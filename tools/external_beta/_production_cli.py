from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import tempfile
from pathlib import Path
from typing import Any

from ._production_workspace import (
    REPOSITORY_ROOT, initialize_production_workspace, prepare_production_draft_definition,
    validate_production_draft_workspace, validate_production_workspace,
)
from ._production_draft_validation import read_bounded_object
from ._source_admission import validate_source_execution


def _read_object(path: Path) -> dict[str, Any]:
    return read_bounded_object(path)[0]


def _write_new_definition(path: Path, value: dict[str, Any]) -> str:
    payload = (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2, allow_nan=False) + "\n").encode("utf-8")
    if len(payload) > 64 * 1024 * 1024:
        raise ValueError("draft definition exceeds the producer byte limit")
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".seam-draft-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        # Link publishes a complete file without replacing an existing target.
        os.link(temporary, path)
    finally:
        os.unlink(temporary)
    return hashlib.sha256(payload).hexdigest()


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Manage recoverable Project SEAM voicebank production workspaces")
    commands = parser.add_subparsers(dest="command", required=True)
    initialize = commands.add_parser("init-project")
    initialize.add_argument("--inventory", type=Path, required=True)
    initialize.add_argument("--strategies", type=Path, required=True)
    initialize.add_argument("--workspace", type=Path, required=True)
    initialize.add_argument("--project-id", required=True)
    initialize.add_argument("--operator-id", required=True)
    initialize.add_argument("--occurred-at", required=True)
    draft = commands.add_parser("prepare-draft", help="Prepare a source-aware definition for the C++ init-production command")
    draft.add_argument("--inventory", type=Path)
    draft.add_argument("--strategies", type=Path)
    draft.add_argument("--project-id", required=True)
    draft.add_argument("--operator-id", required=True)
    draft.add_argument("--output", type=Path, required=True)
    draft.add_argument("--repository-root", type=Path, default=REPOSITORY_ROOT)
    execution = commands.add_parser("check-source-execution", help="Check source-use/transformation authorization without granting candidate readiness")
    execution.add_argument("--strategies", type=Path, required=True)
    execution.add_argument("--repository-root", type=Path, default=REPOSITORY_ROOT)
    validate = commands.add_parser("validate-workspace")
    validate.add_argument("--inventory", type=Path)
    validate.add_argument("--strategies", type=Path)
    validate.add_argument("--workspace", type=Path, required=True)
    validate.add_argument("--draft", action="store_true", help="Verify source-aware schema-2/3/4 persistence/evidence, not release qualification")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = _parser()
    args = parser.parse_args(argv)
    try:
        inventory = _read_object(args.inventory) if getattr(args, "inventory", None) else None
        strategies = _read_object(args.strategies) if getattr(args, "strategies", None) else None
        if args.command == "prepare-draft":
            project = prepare_production_draft_definition(inventory, strategies, project_id=args.project_id,
                operator_id=args.operator_id, repository_root=args.repository_root)
            digest = _write_new_definition(args.output, project)
            print(json.dumps({"status": "DRAFT_DEFINITION_PREPARED", "definition": str(args.output.resolve()),
                "sha256": digest, "projectId": project["projectId"], "generation": 0,
                "nextCommand": "seam_voicebank_cli init-production", "evidenceScope": "engineering", "releaseEligible": False},
                ensure_ascii=False, sort_keys=True))
            return 0
        if args.command == "check-source-execution":
            result = validate_source_execution(strategies, args.repository_root)
            print(json.dumps(result.as_dict() | {"admissionScope": "source-use-and-transformation", "releaseEligible": False}, sort_keys=True))
            return 0 if result.passed else 1
        if args.command == "init-project":
            project = initialize_production_workspace(
                args.workspace,
                inventory,
                strategies,
                project_id=args.project_id,
                operator_id=args.operator_id,
                occurred_at=args.occurred_at,
            )
            print(json.dumps({
                "status": "INITIALIZED",
                "workspace": str(args.workspace.resolve()),
                "projectId": project["projectId"],
                "inventorySha256": project["inventorySha256"],
                "generation": project["lastDurableGeneration"],
                "unitCount": len(project["unitAssignments"]),
            }, ensure_ascii=False, sort_keys=True))
            return 0
        if args.draft:
            result = validate_production_draft_workspace(args.workspace, inventory)
            payload = result.as_dict() | {"evidenceScope": "engineering", "releaseEligible": False}
        else:
            if inventory is None or strategies is None:
                raise ValueError("legacy workspace validation requires --inventory and --strategies; use --draft for source-aware persistence checks")
            result = validate_production_workspace(args.workspace, inventory, strategies)
            payload = result.as_dict()
        print(json.dumps(payload, ensure_ascii=False, sort_keys=True))
        return 0 if result.passed else 1
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError, TypeError, RecursionError) as exc:
        print(str(exc), file=sys.stderr)
        return 2
