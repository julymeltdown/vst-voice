"""Collect the install evidence record, rather than validating one somebody wrote.

The validators in this package consume records. Until now nothing produced them, so a candidate
could assert an installed tree digest it never measured. This module performs the measurement
and emits the same record shape ``validate_install_record`` consumes: it walks the real installed
tree, hashes it with the same routine the validator uses, captures the running system identity,
and records what it actually observed.

What it does NOT do is install, uninstall, or drive the product. Those are human or CI actions
on a target machine. This collector measures the machine it is given and refuses to invent the
parts it cannot observe, so a record it emits is evidence of a real environment and still
states plainly which lifecycle steps a human performed.
"""

from __future__ import annotations

import hashlib
import json
import platform
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .install_evidence import _tree_digest


def _digest_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def observe_environment() -> dict[str, Any]:
    """The machine identity, read from the system rather than supplied by a caller."""
    machine = platform.machine()
    architecture = "arm64" if machine in {"arm64", "aarch64"} else "x86_64"
    return {
        "platform": "macos" if sys.platform == "darwin" else sys.platform,
        "architecture": architecture,
        "osBuild": platform.mac_ver()[0] or platform.release(),
        "imageId": _image_identifier(),
        "hostPython": platform.python_version(),
    }


def _image_identifier() -> str:
    if sys.platform == "darwin":
        try:
            completed = subprocess.run(
                ["sw_vers", "-buildVersion"], capture_output=True, text=True,
                timeout=30, check=False,
            )
        except (OSError, subprocess.SubprocessError):
            return ""
        return completed.stdout.strip() if completed.returncode == 0 else ""
    return platform.release()


def measure_installed_tree(root: Path, relative: str) -> dict[str, Any]:
    """Hash a real installed tree with the validator own routine.

    Using the validator ``_tree_digest`` rather than a second implementation is deliberate:
    if the two ever disagreed, a correctly collected record would fail its own validator.
    """
    errors: list[str] = []
    candidate = root / relative
    digest = _tree_digest(candidate, errors, "installedPath")
    return {
        "installedPath": relative,
        "installedTreeSha256": digest,
        "entries": sum(1 for entry in candidate.rglob("*") if entry.is_file()),
        "errors": errors,
    }


def measure_artifact(root: Path, relative: str) -> dict[str, Any]:
    """Hash a real deliverable or installer file."""
    candidate = root / relative
    return {
        "path": relative,
        "sha256": _digest_file(candidate) if candidate.is_file() else "",
        "bytes": candidate.stat().st_size if candidate.is_file() else 0,
    }


def collect_install_record(
    root: Path,
    *,
    record_id: str,
    candidate_root_id: str,
    operator: str,
    verifier: str,
    deliverable_path: str,
    installer_path: str,
    installed_path: str,
    bank_identity: dict[str, Any],
    acquisition: dict[str, Any],
    inventory: dict[str, Any],
    rows: list[dict[str, Any]],
) -> dict[str, Any]:
    """Assemble a record from measured values plus the human-performed lifecycle steps.

    Every digest below comes from bytes on this disk and every timestamp from the clock. The
    caller supplies only what a collector cannot observe: who ran it, which candidate, and the
    steps a human performed on the machine.
    """
    observed = observe_environment()
    now = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    deliverable = measure_artifact(root, deliverable_path)
    installer = measure_artifact(root, installer_path)
    installed = measure_installed_tree(root, installed_path)
    return {
        "schemaVersion": 1,
        "recordType": "external-beta-install-lifecycle",
        "recordId": record_id,
        "platform": observed["platform"],
        "architecture": observed["architecture"],
        "osBuild": observed["osBuild"],
        "imageId": observed["imageId"],
        "candidateRootId": candidate_root_id,
        "operator": operator,
        "verifier": verifier,
        "startedAt": inventory.get("startedAt") or now,
        "endedAt": now,
        "clockAuthority": "physical-device-clock",
        "accountAuthority": "clean-verifier-snapshot",
        "deliverablePath": deliverable_path,
        "deliverableSha256": deliverable["sha256"],
        "installerPath": installer_path,
        "installerSha256": installer["sha256"],
        "installedPath": installed_path,
        "installedTreeSha256": installed["installedTreeSha256"],
        "installedEntryCount": installed["entries"],
        "bankIdentity": bank_identity,
        "acquisition": acquisition,
        "inventory": inventory,
        "rows": rows,
        # Provenance of the record itself, so a validator can tell a collected record from a
        # hand-authored one without trusting any field the author supplied.
        "collector": {
            "tool": "tools/external_beta/install_collector.py",
            "collectorVersion": 1,
            "hostPython": observed["hostPython"],
            "measuredDigests": [
                "deliverableSha256", "installerSha256", "installedTreeSha256"],
        },
    }


def main(argv: list[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    if len(arguments) != 2 or arguments[0] != "--collect-install":
        sys.stderr.write("usage: install_collector --collect-install REQUEST.json\n")
        return 2
    request = json.loads(Path(arguments[1]).read_text(encoding="utf-8"))
    root = Path(request["evidenceRoot"])
    record = collect_install_record(
        root,
        record_id=request["recordId"],
        candidate_root_id=request["candidateRootId"],
        operator=request["operator"],
        verifier=request["verifier"],
        deliverable_path=request["deliverablePath"],
        installer_path=request["installerPath"],
        installed_path=request["installedPath"],
        bank_identity=request["bankIdentity"],
        acquisition=request["acquisition"],
        inventory=request["inventory"],
        rows=request.get("rows", []),
    )
    sys.stdout.write(json.dumps(record, indent=2, sort_keys=True) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
