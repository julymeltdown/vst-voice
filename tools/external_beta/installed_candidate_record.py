"""Replay a U14 installed-resource record through an explicitly trusted native CLI.

Engineering only. No command, executable, trust key or filesystem path is taken
from the record. The caller supplies those inputs and pins the record, executable
and public-key bytes. This is local verification, not an externally authenticated
execution attestation or qualification.
"""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import time

from .full_product_report import _canonical, _parse_json, _read_regular_file

RECORD_TYPE = "seam.u14.installed-candidate-verification.v1"
RECORD_LIMIT = 64 * 1024
BINARY_LIMIT = 128 * 1024 * 1024
KEY_LIMIT = 64 * 1024
HEX = re.compile(r"[0-9a-f]{64}")
FIELDS = frozenset(("schemaVersion", "recordType", "result", "evidenceScope", "resourceKind", "payloadFamily",
    "resourceId", "resourceVersion", "resourceCandidateSha256", "packageDigest", "candidateContentSha256",
    "installedContentHash", "signerKeyId", "receiptSha256", "installedResourceTreeSha256", "installedFiles",
    "qualification", "humanAcceptance", "authorizesRelease", "releaseEligible"))
DIGESTS = ("resourceCandidateSha256", "packageDigest", "candidateContentSha256", "installedContentHash",
    "receiptSha256", "installedResourceTreeSha256")


def validate_record(record):
    if not isinstance(record, dict) or set(record) != FIELDS:
        raise ValueError("installed candidate record fields differ from the closed engineering schema")
    fixed = {"schemaVersion": 1, "recordType": RECORD_TYPE, "result": "InstalledCandidateVerified",
        "evidenceScope": "ENGINEERING_ONLY", "qualification": "NOT_QUALIFIED", "humanAcceptance": "NOT_RUN",
        "authorizesRelease": False, "releaseEligible": False}
    for key, expected in fixed.items():
        if type(record[key]) is not type(expected) or record[key] != expected:
            raise ValueError(f"installed candidate record has an invalid {key}")
    for key in DIGESTS:
        if not isinstance(record[key], str) or HEX.fullmatch(record[key]) is None:
            raise ValueError(f"installed candidate record has an invalid {key}")
    families = {"sample-real": "sample", "sample-procedural": "sample", "recipe-original": "recipe"}
    if not isinstance(record["resourceKind"], str) or families.get(record["resourceKind"]) != record["payloadFamily"]:
        raise ValueError("installed candidate record requires an installable sample or recipe family")
    for key in ("resourceId", "resourceVersion", "signerKeyId"):
        if not isinstance(record[key], str) or not 1 <= len(record[key]) <= 256:
            raise ValueError(f"installed candidate record has an invalid {key}")
    if type(record["installedFiles"]) is not int or not 2 <= record["installedFiles"] <= 100001:
        raise ValueError("installed candidate record has an invalid installedFiles")


def _pinned(path, expected, limit, label):
    if not isinstance(expected, str) or HEX.fullmatch(expected) is None:
        raise ValueError(f"{label} requires an externally supplied lowercase SHA-256 pin")
    data = _read_regular_file(Path(path), label=label, maximum_bytes=limit)
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError(f"{label} differs from its captured digest")
    return data


def _run(command, timeout_seconds):
    if os.name != "posix":
        raise ValueError("native installed-candidate replay is not implemented on this platform")
    # Only this explicitly selected process group can be terminated. Temporary
    # files avoid unbounded in-memory capture and inherited loader overrides.
    with tempfile.TemporaryFile() as output, tempfile.TemporaryFile() as errors:
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=output, stderr=errors,
            env={"PATH": "/usr/bin:/bin", "LC_ALL": "C"}, start_new_session=True)
        try:
            deadline = time.monotonic() + timeout_seconds
            while process.poll() is None:
                if os.fstat(output.fileno()).st_size > RECORD_LIMIT or os.fstat(errors.fileno()).st_size > RECORD_LIMIT:
                    raise ValueError("native installed verifier exceeded its output limit")
                if time.monotonic() >= deadline:
                    raise ValueError("native installed verifier timed out")
                time.sleep(0.02)
            if os.fstat(output.fileno()).st_size > RECORD_LIMIT or os.fstat(errors.fileno()).st_size > RECORD_LIMIT:
                raise ValueError("native installed verifier exceeded its output limit")
            output.seek(0); errors.seek(0)
            stdout, stderr = output.read(RECORD_LIMIT), errors.read(RECORD_LIMIT)
            if process.returncode != 0:
                detail = stderr.decode("utf-8", errors="replace")[:1500].strip()
                raise ValueError(f"native installed verifier refused (exit {process.returncode}): {detail}")
            if stderr:
                raise ValueError("native installed verifier produced unexpected diagnostics")
            return _parse_json(stdout)
        finally:
            # The selected native verifier does not launch children. Kill its
            # group only while it is still live; never signal a reaped PID.
            if process.poll() is None:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                except PermissionError:
                    # Some host policies deny group signals. This verifier has
                    # no child processes; terminate only our direct child.
                    process.kill()
            process.wait()


def audit_installed_candidate_record(*, record_path, record_sha256, package_path, installed_directory,
        public_key_path, public_key_sha256, cli_path, cli_sha256, timeout_seconds=60):
    result = {"status": "BLOCKED", "passed": False, "authorizesRelease": False,
        "evidenceScope": "ENGINEERING_ONLY", "errors": []}
    try:
        if type(timeout_seconds) not in (int, float) or not 0 < timeout_seconds <= 300:
            raise ValueError("native verification timeout must be within (0, 300] seconds")
        record = _parse_json(_pinned(record_path, record_sha256, RECORD_LIMIT, "record"))
        validate_record(record)
        cli = Path(os.path.abspath(cli_path))
        key = Path(os.path.abspath(public_key_path))
        _pinned(cli, cli_sha256, BINARY_LIMIT, "native verifier")
        _pinned(key, public_key_sha256, KEY_LIMIT, "trusted public key")
        actual = _run([str(cli), "verify-installed-candidate", str(Path(os.path.abspath(package_path))),
            record["packageDigest"], record["resourceCandidateSha256"],
            str(Path(os.path.abspath(installed_directory))), str(key)], timeout_seconds)
        validate_record(actual)
        # Reconfirm selected verifier/key bytes after execution. This detects
        # ordinary concurrent replacement, not a hostile process owner/loader.
        _pinned(cli, cli_sha256, BINARY_LIMIT, "native verifier")
        _pinned(key, public_key_sha256, KEY_LIMIT, "trusted public key")
        if _canonical(actual) != _canonical(record):
            raise ValueError("retained installed record differs from fresh native verification")
        result.update(status="ENGINEERING_PASS", passed=True, recordSha256=record_sha256,
            verifierSha256=cli_sha256, publicKeySha256=public_key_sha256,
            resourceCandidateSha256=record["resourceCandidateSha256"], installedResourceTreeSha256=record["installedResourceTreeSha256"])
    except (OSError, ValueError, TypeError, subprocess.SubprocessError) as error:
        result["errors"] = [str(error)[:2000]]
    return result
