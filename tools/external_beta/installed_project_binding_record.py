"""Native installed-content to project-reference linkage, never runtime admission."""
from __future__ import annotations
import os
from pathlib import Path
import subprocess
from . import installed_candidate_record as installed
from . import project_binding_record as project
from .full_product_report import _canonical, _parse_json

RECORD_TYPE = "seam.u45.installed-project-binding.v1"
FIXED = {"schemaVersion": 1, "recordType": RECORD_TYPE, "result": "InstalledProjectBindingVerified",
    "evidenceScope": "ENGINEERING_ONLY", "bindingEvidence": "VERIFIED_INSTALLED_CONTENT_TO_PROJECT_REFERENCE",
    "languageCoverage": "SIGNED_DECLARATION", "runtimeAvailability": "NOT_CHECKED", "playback": "NOT_RUN",
    "hostExecution": "NOT_RUN", "humanAcceptance": "NOT_RUN", "qualification": "NOT_QUALIFIED",
    "authorizesRelease": False, "releaseEligible": False}
FIELDS = frozenset(FIXED) | {"installed", "project", "resourceLanguages"}


def validate_record(record):
    if not isinstance(record, dict) or set(record) != FIELDS:
        raise ValueError("installed project record differs from its closed engineering schema")
    for key, expected in FIXED.items():
        if type(record[key]) is not type(expected) or record[key] != expected:
            raise ValueError(f"invalid installed project {key}")
    resource, binding = record["installed"], record["project"]
    installed.validate_record(resource)
    if resource["recordType"] != installed.RECORD_TYPE:
        raise ValueError("installed project linkage requires an installed v2 record")
    project.validate_record(binding)
    if resource["payloadFamily"] != binding["payloadFamily"]:
        raise ValueError("installed and project families differ")
    if resource["candidateContentSha256"] != binding["resourceContentHash"]:
        raise ValueError("project reference does not bind the verified candidate content")
    if resource["payloadFamily"] == "sample" and any(resource[left] != binding[right] for left, right in (
            ("resourceId", "resourceId"), ("resourceVersion", "resourceVersion"), ("installedContentHash", "resourceContentHash"))):
        raise ValueError("sample project reference differs from installed identity")
    # Recipe id and schema version are derived by the native verifier from its
    # private signed snapshot. They are NOT the package id/version. Whole-record
    # replay checks them; Python must not guess them or reopen project paths.
    languages = record["resourceLanguages"]
    if not isinstance(languages, list) or not 1 <= len(languages) <= 4 or any(
            type(value) is not str or value not in ("en", "ja", "ko", "und") for value in languages):
        raise ValueError("invalid signed resource language declaration")
    if len(languages) != len(set(languages)) or not set(binding["languages"]).issubset(languages):
        raise ValueError("project languages are not covered by the unique signed declaration")
    if resource["payloadFamily"] == "sample" and len(languages) != 1:
        raise ValueError("sample candidate must declare exactly one language")


def audit_installed_project_binding_record(*, record_path, record_sha256, project_path, package_path,
        installed_directory, public_key_path, public_key_sha256, cli_path, cli_sha256, timeout_seconds=60):
    result = {"status": "BLOCKED", "passed": False, "authorizesRelease": False,
        "evidenceScope": "ENGINEERING_ONLY", "errors": []}
    try:
        if type(timeout_seconds) not in (int, float) or not 0 < timeout_seconds <= 300:
            raise ValueError("native verification timeout must be within (0, 300] seconds")
        record = _parse_json(installed._pinned(record_path, record_sha256, installed.RECORD_LIMIT, "installed project record"))
        validate_record(record)
        cli, key = Path(os.path.abspath(cli_path)), Path(os.path.abspath(public_key_path))
        installed._pinned(cli, cli_sha256, installed.BINARY_LIMIT, "native verifier")
        installed._pinned(key, public_key_sha256, installed.KEY_LIMIT, "trusted public key")
        resource, binding = record["installed"], record["project"]
        actual = installed._run([str(cli), "verify-installed-project-binding", str(Path(os.path.abspath(project_path))),
            binding["projectSha256"], binding["trackId"], binding["regionId"], ",".join(binding["languages"]),
            str(Path(os.path.abspath(package_path))), resource["packageDigest"], resource["resourceCandidateSha256"],
            str(Path(os.path.abspath(installed_directory))), str(key)], timeout_seconds)
        validate_record(actual)
        installed._pinned(cli, cli_sha256, installed.BINARY_LIMIT, "native verifier")
        installed._pinned(key, public_key_sha256, installed.KEY_LIMIT, "trusted public key")
        if _canonical(actual) != _canonical(record):
            raise ValueError("retained installed project record differs from fresh native verification")
        result.update(status="ENGINEERING_PASS", passed=True, recordType=RECORD_TYPE,
            recordSha256=record_sha256, verifierSha256=cli_sha256, publicKeySha256=public_key_sha256,
            projectSha256=binding["projectSha256"], installedResourceTreeSha256=resource["installedResourceTreeSha256"])
    except (OSError, ValueError, TypeError, subprocess.SubprocessError) as error:
        result["errors"] = [str(error)[:2000]]
    return result
