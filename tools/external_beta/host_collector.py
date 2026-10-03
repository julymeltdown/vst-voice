"""Collect a host-session record by running the real host validators, not asserting one.

`validate_host_record` demands a host version, a host build, an installed-artifact digest and a
PASS for every one of twenty-three named checks. Those are exactly the facts a machine can report
about itself and its DAW, so this collector reads them and runs the validators that exist.

Two things are deliberately NOT done here. The twenty-three checks describe behaviour inside a
running DAW -- scan, instantiate, GUI lifecycle, state save and restore, a thirty-minute session --
and those are performed by a person driving the host. This module runs the host validators that
can be executed unattended (`auval`, the VST3 test host) and captures their real output, then
records the remaining checks as NOT_RUN rather than PASS. A record that claimed twenty-three
PASSes because nobody had checked them would be worse than one that says what it did not do.
"""

from __future__ import annotations

import hashlib
import json
import platform
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .host_evidence import HOST_CHECK_NAMES

ROOT = Path(__file__).resolve().parents[2]

# Checks a host validator can settle unattended. Everything else needs a person driving the DAU
# and is reported NOT_RUN rather than assumed.
AUTOMATED_CHECKS: dict[str, str] = {
    "scan": "host-validator",
    "installDiscovery": "host-validator",
    "instantiate": "host-validator",
}


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def installed_tree_digest(path: Path) -> str:
    """Hash an installed artifact the way the validator does, from real bytes."""
    digest = hashlib.sha256()
    if path.is_file():
        return _sha256(path)
    for entry in sorted(path.rglob("*"), key=lambda item: item.as_posix()):
        if entry.is_symlink() or not entry.is_file():
            continue
        relative = entry.relative_to(path).as_posix().encode("utf-8")
        digest.update(len(relative).to_bytes(8, "big"))
        digest.update(relative)
        digest.update(bytes.fromhex(_sha256(entry)))
    return digest.hexdigest()


def run_host_validator(format_name: str, output: Path, timeout: int = 900) -> dict[str, Any]:
    """Run the repository own host validator for a plugin format and capture its result.

    Delegates to scripts/run_auval.py or scripts/run_vst3_test_host.py rather than
    reimplementing discovery, because those already own component discovery and tool hashing.
    A missing tool is reported as missing instead of being treated as a pass.
    """
    output.mkdir(parents=True, exist_ok=True)
    runner = ROOT / "scripts" / ("run_auval.py" if format_name == "auv2" else "run_vst3_test_host.py")
    if not runner.is_file():
        return {"status": "NOT_RUN", "reason": "validator-runner-missing", "tool": str(runner)}
    if format_name == "auv2" and shutil.which("auval") is None:
        return {"status": "NOT_RUN", "reason": "tool-missing", "tool": "auval"}
    try:
        completed = subprocess.run(
            [sys.executable, str(runner), "--output", str(output)],
            capture_output=True, text=True, timeout=timeout, check=False,
        )
    except subprocess.TimeoutExpired:
        return {"status": "FAIL", "reason": "timeout", "tool": str(runner)}
    result_path = output / "result.json"
    if not result_path.is_file():
        return {
            "status": "NOT_RUN",
            "reason": "validator-produced-no-result",
            "tool": str(runner),
            "exitCode": completed.returncode,
            "stderr": completed.stderr[-4000:],
        }
    result = json.loads(result_path.read_text(encoding="utf-8"))
    result["tool"] = str(runner)
    return result


def observe_host() -> dict[str, Any]:
    """The host application identity, read from the running system where possible."""
    machine = platform.machine()
    architecture = "arm64" if machine in {"arm64", "aarch64"} else "x86_64"
    return {
        "platform": "macos" if sys.platform == "darwin" else sys.platform,
        "architecture": architecture,
        "osBuild": platform.mac_ver()[0] or platform.release(),
        "hostVersion": _darwin_value("/Applications/Logic Pro.app", "CFBundleShortVersionString"),
        "hostBuild": _darwin_value("/Applications/Logic Pro.app", "CFBundleVersion"),
    }


def _darwin_value(app: str, key: str) -> str:
    """Read a bundle Info.plist value; empty when the app is absent rather than a guess."""
    plist = Path(app) / "Contents" / "Info.plist"
    if not plist.is_file():
        return ""
    try:
        completed = subprocess.run(
            ["/usr/bin/defaults", "read", str(plist), key],
            capture_output=True, text=True, timeout=30, check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return ""
    if completed.returncode != 0:
        return ""
    return completed.stdout.strip()


def build_host_record(
    *,
    record_id: str,
    target: dict[str, Any],
    candidate_root_id: str,
    artifact_path: str,
    operator: str,
    verifier: str,
    workload_sha256: str,
    machine_profile_sha256: str,
    bank_identity: dict[str, Any],
    project_identity: dict[str, Any],
    validator_result: dict[str, Any],
    started_at: str,
    ended_at: str,
    artifact_digest: str,
) -> dict[str, Any]:
    """Assemble the record from what was observed.

    Only the checks a host validator can settle unattended are marked PASS, and only when it
    actually returned PASS. Every other check is NOT_RUN, because a person has not driven it. A
    record claiming twenty-three passes because nobody checked them would be worthless.
    """
    observed = observe_host()
    validator_passed = validator_result.get("status") == "PASS"
    checks: dict[str, str] = {}
    for name in HOST_CHECK_NAMES:
        if name in AUTOMATED_CHECKS:
            checks[name] = "PASS" if validator_passed else "FAIL"
        else:
            checks[name] = "NOT_RUN"
    return {
        "schemaVersion": 1,
        "recordType": "external-beta-host-session",
        "status": "PASS" if validator_passed else "FAIL",
        "recordId": record_id,
        "targetId": target["id"],
        "platform": target["platform"],
        "architecture": target["architecture"],
        "host": target["host"],
        "pluginFormat": target["format"],
        "hostVersion": observed["hostVersion"] or "unreported",
        "hostBuild": observed["hostBuild"] or "unreported",
        "osBuild": observed["osBuild"],
        "candidateRootId": candidate_root_id,
        "artifactPath": artifact_path,
        "pluginSha256": artifact_digest,
        "installedTreeSha256": artifact_digest,
        "workloadSha256": workload_sha256,
        "machineProfileSha256": machine_profile_sha256,
        "bankIdentity": bank_identity,
        "projectIdentity": project_identity,
        "operator": operator,
        "verifier": verifier,
        "startedAt": started_at,
        "endedAt": ended_at,
        "clockAuthority": "physical-device-clock",
        "checks": checks,
        "hostValidator": validator_result,
        "collector": {
            "tool": "tools/external_beta/host_collector.py",
            "collectorVersion": 1,
            "automatedChecks": sorted(AUTOMATED_CHECKS),
            "manualChecks": [n for n in HOST_CHECK_NAMES if n not in AUTOMATED_CHECKS],
        },
    }


def now_utc() -> str:
    stamp = datetime.now(timezone.utc).replace(microsecond=0).isoformat()
    return stamp.replace("+00:00", "Z")
