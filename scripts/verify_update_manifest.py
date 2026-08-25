#!/usr/bin/env python3
from __future__ import annotations

import argparse
import base64
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))

import update_contract as contract  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Verify an External Beta update and optionally create a sealed installer handoff")
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--root-key", action="append", default=[])
    parser.add_argument("--installed-version")
    parser.add_argument("--package", type=Path)
    parser.add_argument("--state", type=Path)
    parser.add_argument("--stage-root", type=Path)
    parser.add_argument("--handoff-output", type=Path)
    args = parser.parse_args(argv)
    try:
        policy = json.loads(args.policy.read_text(encoding="utf-8"))
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        if not isinstance(policy, dict) or not isinstance(manifest, dict):
            raise ValueError("policy and manifest roots must be objects")
        roots: dict[str, bytes] = {}
        for entry in args.root_key:
            if "=" not in entry:
                raise ValueError("--root-key must be KEY_ID=BASE64")
            key_id, encoded = entry.split("=", 1)
            roots[key_id] = base64.b64decode(encoded.encode("ascii"), validate=True)
        if not roots:
            raise ValueError("at least one offline trusted root is required")
        errors = contract.verify_update_manifest(manifest, policy, installed_version=args.installed_version, trusted_policy_roots=roots or None)
        state_value = json.loads(args.state.read_text(encoding="utf-8")) if args.state and args.state.exists() else {}
        accepted_state, state_errors = contract.accept_manifest(manifest, state_value)
        errors.extend(state_errors)
        handoff = None
        if not errors and args.package is not None:
            if args.stage_root is None:
                raise ValueError("--stage-root is required when --package is supplied")
            handoff = contract.stage_verified_package(args.package, manifest, args.stage_root)
            if args.handoff_output:
                args.handoff_output.parent.mkdir(parents=True, exist_ok=True)
                args.handoff_output.write_text(json.dumps(handoff, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        result = {"schemaVersion": 1, "status": "PASS" if not errors else "BLOCKED", "manifestId": manifest.get("manifestId", ""), "manifestSha256": contract.manifest_identity(manifest), "state": accepted_state, "handoff": handoff, "errors": errors}
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0 if not errors else 3
    except (OSError, ValueError, json.JSONDecodeError, UnicodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
