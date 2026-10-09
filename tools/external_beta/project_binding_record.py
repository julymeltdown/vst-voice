"""Replay native project reference semantics; never installation or singing evidence."""
from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess

from .installed_candidate_record import HEX, RECORD_LIMIT, BINARY_LIMIT, _pinned, _run
from .full_product_report import _canonical, _parse_json

RECORD_TYPE = "seam.u45.project-binding-verification.v1"
FIXED = {"schemaVersion": 1, "recordType": RECORD_TYPE, "result": "ProjectBindingVerified",
    "evidenceScope": "ENGINEERING_ONLY", "bindingScope": "SELECTED_REGION_NOTE_REFERENCES",
    "resourceAdmission": "NOT_CHECKED", "runtimeAvailability": "NOT_CHECKED",
    "languageEvidence": "DECLARED_LYRIC_LANGUAGE", "phonemization": "NOT_RUN",
    "playback": "NOT_RUN", "hostExecution": "NOT_RUN", "qualification": "NOT_QUALIFIED", "humanAcceptance": "NOT_RUN",
    "authorizesRelease": False, "releaseEligible": False}
FIELDS = frozenset(FIXED) | {"projectSha256", "projectId", "trackId", "regionId", "payloadFamily",
    "sourceSchemaVersion", "codecSchemaVersion", "storedVoicebankRole", "resourceId", "resourceVersion", "resourceContentHash", "languages", "noteCount", "linkedLyricCount", "unusedLyricCount"}
ID = re.compile(r"[0-9a-f]{16}")


def validate_record(record):
    if not isinstance(record, dict) or set(record) != FIELDS:
        raise ValueError("project binding record differs from its closed engineering schema")
    for key, expected in FIXED.items():
        if type(record[key]) is not type(expected) or record[key] != expected:
            raise ValueError(f"invalid project binding {key}")
    for key in ("projectSha256", "resourceContentHash"):
        if not isinstance(record[key], str) or HEX.fullmatch(record[key]) is None:
            raise ValueError(f"invalid project binding {key}")
    for key in ("projectId", "trackId", "regionId"):
        if not isinstance(record[key], str) or ID.fullmatch(record[key]) is None or int(record[key], 16) == 0:
            raise ValueError(f"invalid project binding {key}")
    if record["payloadFamily"] not in ("sample", "recipe", "model"):
        raise ValueError("invalid project binding payloadFamily")
    if type(record["codecSchemaVersion"]) is not int or record["codecSchemaVersion"] < 20:
        raise ValueError("invalid codec schema version")
    if type(record["sourceSchemaVersion"]) is not int or not 1 <= record["sourceSchemaVersion"] <= record["codecSchemaVersion"]:
        raise ValueError("invalid source schema version")
    if record["storedVoicebankRole"] != ("SELECTED" if record["payloadFamily"] == "sample" else "INACTIVE"):
        raise ValueError("invalid inactive/selected voicebank role")
    for key in ("resourceId", "resourceVersion"):
        if not isinstance(record[key], str) or not 1 <= len(record[key]) <= 256 or "\0" in record[key]:
            raise ValueError(f"invalid project binding {key}")
    languages = record["languages"]
    if not isinstance(languages, list) or not 1 <= len(languages) <= 3 or any(
            type(value) is not str or value not in ("en", "ja", "ko") for value in languages):
        raise ValueError("invalid project binding languages")
    if languages != sorted(set(languages)):
        raise ValueError("project binding languages must be sorted and unique")
    for key in ("noteCount", "linkedLyricCount", "unusedLyricCount"):
        if type(record[key]) is not int or not 0 <= record[key] <= 250000:
            raise ValueError(f"invalid project binding {key}")
    if not len(languages) <= record["linkedLyricCount"] <= record["noteCount"]:
        raise ValueError("project binding note/lyric counts are inconsistent")


def audit_project_binding_record(*, record_path, record_sha256, project_path, cli_path, cli_sha256,
        timeout_seconds=60):
    result = {"status": "BLOCKED", "passed": False, "authorizesRelease": False,
        "evidenceScope": "ENGINEERING_ONLY", "errors": []}
    try:
        if type(timeout_seconds) not in (int, float) or not 0 < timeout_seconds <= 300:
            raise ValueError("native verification timeout must be within (0, 300] seconds")
        record = _parse_json(_pinned(record_path, record_sha256, RECORD_LIMIT, "project binding record"))
        validate_record(record)
        cli = Path(os.path.abspath(cli_path))
        _pinned(cli, cli_sha256, BINARY_LIMIT, "native verifier")
        # Only data arguments come from the pinned record. The executable,
        # project path and fixed command never come from evidence fields.
        actual = _run([str(cli), "verify-project-binding", str(Path(os.path.abspath(project_path))),
            record["projectSha256"], record["trackId"], record["regionId"], record["payloadFamily"],
            record["resourceId"], record["resourceVersion"], record["resourceContentHash"],
            ",".join(record["languages"])], timeout_seconds)
        validate_record(actual)
        _pinned(cli, cli_sha256, BINARY_LIMIT, "native verifier")
        if _canonical(actual) != _canonical(record):
            raise ValueError("retained project binding differs from fresh native verification")
        result.update(status="ENGINEERING_PASS", passed=True, recordType=RECORD_TYPE,
            recordSha256=record_sha256, verifierSha256=cli_sha256, projectSha256=record["projectSha256"],
            trackId=record["trackId"], regionId=record["regionId"])
    except (OSError, ValueError, TypeError, subprocess.SubprocessError) as error:
        result["errors"] = [str(error)[:2000]]
    return result
