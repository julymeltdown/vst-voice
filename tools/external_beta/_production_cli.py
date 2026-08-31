from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from ._production_workspace import initialize_production_workspace, validate_production_workspace


def _read_object(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return value


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
    validate = commands.add_parser("validate-workspace")
    validate.add_argument("--inventory", type=Path, required=True)
    validate.add_argument("--strategies", type=Path, required=True)
    validate.add_argument("--workspace", type=Path, required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = _parser()
    args = parser.parse_args(argv)
    try:
        inventory = _read_object(args.inventory)
        strategies = _read_object(args.strategies)
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
        result = validate_production_workspace(args.workspace, inventory, strategies)
        print(json.dumps(result.as_dict(), ensure_ascii=False, sort_keys=True))
        return 0 if result.passed else 1
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 2
