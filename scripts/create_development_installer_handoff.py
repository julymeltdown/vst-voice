#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = []
# ///

# ─── How to run ───
# 1. Install uv (if not installed):
#      curl -LsSf https://astral.sh/uv/install.sh | sh
# 2. Run directly (no venv, no pip install needed):
#      uv run scripts/create_development_installer_handoff.py --help
# 3. Or make executable and run:
#      chmod +x scripts/create_development_installer_handoff.py && ./scripts/create_development_installer_handoff.py --help
# ──────────────────

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.phase13a.development_handoff import (  # noqa: E402
    create_development_update_contract,
)
from tools.phase13a.release_payload import PayloadPlatform  # noqa: E402


def _values(output: str) -> dict[str, str]:
    return {
        key: value
        for line in output.splitlines()
        if "=" in line
        for key, value in (line.split("=", 1),)
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Create a test-only signed installer handoff"
    )
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--platform", choices=tuple(PayloadPlatform), required=True)
    parser.add_argument("--verifier", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--staging-root", type=Path)
    arguments = parser.parse_args(argv)
    try:
        output = arguments.output.resolve()
        staging = (arguments.staging_root or output / "staging").resolve()
        contract = create_development_update_contract(
            arguments.package,
            PayloadPlatform(arguments.platform),
            output,
        )
        completed = subprocess.run(
            [
                str(arguments.verifier.resolve()),
                "stage",
                "--package",
                str(arguments.package.resolve()),
                "--manifest",
                str(contract.manifest),
                "--staging-root",
                str(staging),
            ],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        values = _values(completed.stdout)
        candidate = values["candidateId"]
        handoff = Path(values["handoffPath"])
        result = {
            "schemaVersion": 1,
            "purpose": "development-installer-handoff",
            "testOnly": True,
            "platform": arguments.platform,
            "package": str(arguments.package.resolve()),
            "stagedPackage": str(staging / candidate / arguments.package.name),
            "handoff": str(handoff),
            "handoffSha256": hashlib.sha256(handoff.read_bytes()).hexdigest(),
            "manifest": str(contract.manifest),
            "policy": str(contract.policy),
            "stagingRoot": str(staging),
            "candidateId": candidate,
        }
        # macOS Installer does not propagate the invoking shell's environment to
        # package scripts, so the privileged preinstall hook cannot receive the
        # trust inputs through "sudo env". Record the same values beside the
        # staged archive instead; the hook resolves them from PACKAGE_PATH.
        # Values stay single-line and printable so the hook's strict key/value
        # parser accepts them. The hook still re-verifies everything through
        # seam_installer_verifier, so this grants no trust by itself.
        intent_keys = {
            "handoff": "SEAM_INSTALLER_HANDOFF",
            "manifest": "SEAM_UPDATE_MANIFEST",
            "policy": "SEAM_UPDATE_POLICY",
            "stagingRoot": "SEAM_UPDATE_STAGING_ROOT",
            "candidateId": "SEAM_EXPECTED_CANDIDATE",
            "handoffSha256": "SEAM_EXPECTED_HANDOFF_SHA256",
        }
        intent_lines: list[str] = []
        for key, name in intent_keys.items():
            value = str(result[key])
            if not value or not value.isprintable() or "=" in value:
                raise ValueError(f"installer intent value is not usable: {key}")
            intent_lines.append(f"{name}={value}\n")
        (staging / candidate / "installer-intent.env").write_text(
            "".join(intent_lines), encoding="utf-8"
        )
        result_path = output / "handoff-result.json"
        result_path.write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    except (
        OSError,
        ValueError,
        KeyError,
        subprocess.CalledProcessError,
    ) as error:
        print(f"DEVELOPMENT_INSTALLER_HANDOFF=BLOCKED\nerror={error}", file=sys.stderr)
        return 3
    print("DEVELOPMENT_INSTALLER_HANDOFF=PASS")
    print(f"result={result_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
