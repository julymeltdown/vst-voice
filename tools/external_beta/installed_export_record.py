"""Closed local replay of native installed sample/recipe export observations.

Caller-selected paths, verifier and trust key are external inputs. Retained
observations supply data pins, never commands. This does not authenticate a
past execution or authorize release. Retained export inputs stay read-only.
"""
from __future__ import annotations

import hashlib
import math
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import tempfile

from . import installed_candidate_record as installed
from .installed_project_binding_record import validate_installed_binding
from .full_product_report import _canonical, _parse_json

TYPES = {"sample": "seam.u45.installed-sample-export.v1",
         "recipe": "seam.u45.installed-recipe-export.v1"}
CAPTURE = {"sample": "SIGNED_ENTRIES_MATCHED_FROZEN_SAMPLE_ASSETS",
           "recipe": "SIGNED_IDENTITY_MATCHED_FROZEN_RECIPE"}
FIXED = {"schemaVersion": 1, "result": "ExportCommittedAndObserved",
         "evidenceScope": "ENGINEERING_ONLY", "operation": "NATIVE_FINAL_EXPORT_SET",
         "languageCoverage": "SIGNED_DECLARATION", "host": "seam_voicebank_cli",
         "executableIdentity": "EXTERNAL_PIN_REQUIRED", "playback": "NOT_RUN",
         "dawExecution": "NOT_RUN", "qualification": "NOT_QUALIFIED",
         "humanAcceptance": "NOT_RUN", "authorizesRelease": False, "releaseEligible": False}
FIELDS = frozenset(FIXED) | {"recordType", "resourceCapture", "installed", "project",
    "projectLanguages", "platform", "buildId", "sourceCommit", "sampleRate", "artifacts",
    "exportReceiptSha256", "masterSha256", "masterPeakAbs", "masterClipped"}
RECEIPT_LIMIT = 16 * 1024 * 1024
FILE_LIMIT = 2 * 1024 * 1024 * 1024
AGGREGATE_LIMIT = FILE_LIMIT
VOLATILE_RECEIPT = frozenset(("transactionId", "executionDateUnixMs"))


def _relative(value):
    if type(value) is not str or not value or len(value) > 2048 or "\0" in value or "\\" in value:
        raise ValueError("export path must be a bounded canonical relative path")
    path = PurePosixPath(value)
    if not path.parts or path.is_absolute() or path.as_posix() != value or any(p in (".", "..") for p in path.parts):
        raise ValueError("export path must be a bounded canonical relative path")
    return path.parts


def _digest(value, label):
    if type(value) is not str or installed.HEX.fullmatch(value) is None:
        raise ValueError(f"invalid operation {label}")


def _files(value):
    if not isinstance(value, list) or not 1 <= len(value) <= 1024:
        raise ValueError("operation requires a bounded nonempty artifact list")
    paths = set()
    for item in value:
        if not isinstance(item, dict) or set(item) != {"path", "sha256", "frames", "channels"}:
            raise ValueError("operation artifact differs from its closed shape")
        _relative(item["path"])
        if item["path"] == "receipt.json" or item["path"] in paths:
            raise ValueError("operation artifacts repeat a path or own their receipt")
        paths.add(item["path"])
        _digest(item["sha256"], "artifact digest")
        for key, maximum in (("frames", (1 << 63) - 1), ("channels", 65535)):
            if type(item[key]) is not int or not 0 <= item[key] <= maximum:
                raise ValueError(f"invalid operation artifact {key}")
        if (item["frames"] == 0) != (item["channels"] == 0):
            raise ValueError("operation artifact frames/channels disagree")


def validate_record(record):
    if not isinstance(record, dict) or set(record) != FIELDS:
        raise ValueError("operation record differs from its closed engineering schema")
    for key, expected in FIXED.items():
        if type(record[key]) is not type(expected) or record[key] != expected:
            raise ValueError(f"invalid operation {key}")
    validate_installed_binding(record["installed"], record["project"])
    family = record["installed"]["payloadFamily"]
    if record["recordType"] != TYPES[family] or record["resourceCapture"] != CAPTURE[family]:
        raise ValueError("operation type/capture differs from the installed family")
    for key in ("exportReceiptSha256", "masterSha256"):
        _digest(record[key], key)
    if type(record["sourceCommit"]) is not str or re.fullmatch(r"[0-9a-f]{40}", record["sourceCommit"]) is None:
        raise ValueError("invalid operation sourceCommit")
    if record["platform"] not in ("macos", "linux"):
        raise ValueError("operation replay supports only the native POSIX platforms")
    if type(record["buildId"]) is not str or not 1 <= len(record["buildId"]) <= 256 or "\0" in record["buildId"]:
        raise ValueError("invalid operation buildId")
    if type(record["sampleRate"]) is not int or not 8000 <= record["sampleRate"] <= 192000:
        raise ValueError("invalid operation sampleRate")
    peak = record["masterPeakAbs"]
    if type(peak) not in (int, float) or not math.isfinite(peak) or peak < 0:
        raise ValueError("invalid operation masterPeakAbs")
    if type(record["masterClipped"]) is not bool:
        raise ValueError("invalid operation masterClipped")
    languages = record["projectLanguages"]
    if not isinstance(languages, list) or not 1 <= len(languages) <= 3 or any(
            type(x) is not str or x not in ("en", "ja", "ko") for x in languages):
        raise ValueError("invalid operation projectLanguages")
    if languages != sorted(set(languages)) or not set(record["project"]["languages"]).issubset(languages):
        raise ValueError("operation languages must include the selected note-linked languages")
    _files(record["artifacts"])
    master = [x for x in record["artifacts"] if x["path"] == "master.wav"]
    if len(master) != 1 or master[0]["sha256"] != record["masterSha256"] or master[0]["frames"] == 0:
        raise ValueError("operation master must bind the reported master artifact")


def _stamp(value):
    return (value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns, value.st_ctime_ns)


class _ExportSnapshot:
    """Held POSIX root and streamed no-follow file reads, with bounded work."""
    def __init__(self, path):
        self.path = Path(os.path.abspath(path))
        self.fd = -1

    def __enter__(self):
        if os.name != "posix":
            raise ValueError("native operation replay requires POSIX held export inputs")
        self.named = self.path.lstat()
        if not stat.S_ISDIR(self.named.st_mode):
            raise ValueError("export root must be a real directory")
        self.fd = os.open(self.path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        if _stamp(os.fstat(self.fd)) != _stamp(self.named):
            os.close(self.fd); self.fd = -1
            raise ValueError("export root changed during opening")
        return self

    def __exit__(self, *unused):
        if self.fd >= 0:
            os.close(self.fd); self.fd = -1

    def unchanged(self):
        if _stamp(self.path.lstat()) != _stamp(self.named) or _stamp(os.fstat(self.fd)) != _stamp(self.named):
            raise ValueError("retained export root changed during replay")

    def pinned(self, relative, expected, limit, capture=False):
        parts = _relative(relative)
        _digest(expected, "owned file digest")
        parent = os.dup(self.fd)
        try:
            for part in parts[:-1]:
                child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent)
                os.close(parent); parent = child
            before = os.stat(parts[-1], dir_fd=parent, follow_symlinks=False)
            if not stat.S_ISREG(before.st_mode) or before.st_size > limit:
                raise ValueError("owned export file must be regular and within the byte limit")
            fd = os.open(parts[-1], os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=parent)
            with os.fdopen(fd, "rb") as stream:
                opened = os.fstat(stream.fileno())
                if _stamp(opened) != _stamp(before) or not stat.S_ISREG(opened.st_mode):
                    raise ValueError("owned export file changed while opening")
                digest = hashlib.sha256()
                contents = bytearray()
                count = 0
                while True:
                    chunk = stream.read(min(1024 * 1024, before.st_size + 1 - count))
                    if not chunk:
                        break
                    digest.update(chunk); count += len(chunk)
                    if capture:
                        contents.extend(chunk)
                    if count > before.st_size:
                        raise ValueError("owned export file grew during reading")
                after = os.fstat(stream.fileno())
            named = os.stat(parts[-1], dir_fd=parent, follow_symlinks=False)
            if count != before.st_size or _stamp(after) != _stamp(opened) or _stamp(named) != _stamp(opened):
                raise ValueError("owned export file changed during reading")
            if digest.hexdigest() != expected:
                raise ValueError(f"owned export file differs from its digest: {relative}")
            return bytes(contents), count
        finally:
            os.close(parent)

    def files(self, files):
        _files(files)
        consumed = 0
        for item in files:
            _, size = self.pinned(item["path"], item["sha256"], min(FILE_LIMIT, AGGREGATE_LIMIT - consumed))
            consumed += size
        self.unchanged()


def _receipt_identity(receipt):
    if not isinstance(receipt, dict) or not VOLATILE_RECEIPT.issubset(receipt):
        raise ValueError("export receipt lacks its native execution fields")
    transaction = receipt["transactionId"]
    if type(transaction) is not str or re.fullmatch(r"[0-9a-f]{1,128}", transaction) is None:
        raise ValueError("invalid native export transaction id")
    date = receipt["executionDateUnixMs"]
    if type(date) is not int or not 0 <= date < (1 << 63):
        raise ValueError("invalid native export execution timestamp")
    return {key: value for key, value in receipt.items() if key not in VOLATILE_RECEIPT}


def audit_installed_export_record(*, record_path, record_sha256, project_path, package_path,
        installed_directory, export_directory, public_key_path, public_key_sha256,
        cli_path, cli_sha256, timeout_seconds=60):
    result = {"status": "BLOCKED", "passed": False, "authorizesRelease": False,
              "evidenceScope": "ENGINEERING_ONLY", "errors": []}
    try:
        if type(timeout_seconds) not in (int, float) or not math.isfinite(timeout_seconds) or not 0 < timeout_seconds <= 300:
            raise ValueError("native replay timeout must be within (0, 300] seconds")
        record = _parse_json(installed._pinned(record_path, record_sha256, installed.RECORD_LIMIT, "operation record"))
        validate_record(record)
        cli, key = Path(os.path.abspath(cli_path)), Path(os.path.abspath(public_key_path))
        resource, project = record["installed"], record["project"]
        installed._pinned(cli, cli_sha256, installed.BINARY_LIMIT, "native verifier")
        installed._pinned(key, public_key_sha256, installed.KEY_LIMIT, "trusted public key")
        with _ExportSnapshot(export_directory) as retained:
            journal = retained.path.parent / ("." + retained.path.name + "-export-journal.json")
            if os.path.lexists(journal):
                raise ValueError("retained export has an interrupted transaction; replay never recovers it")
            raw = retained.pinned("receipt.json", record["exportReceiptSha256"], RECEIPT_LIMIT, True)[0]
            receipt = _parse_json(raw)
            stable = _receipt_identity(receipt)
            retained.files(receipt.get("files"))
            retained.files(record["artifacts"])
            # A new private directory owns only replay outputs. Fixed command,
            # caller paths and data-only pins are the entire native interface.
            with tempfile.TemporaryDirectory(prefix="seam-operation-replay-") as temporary:
                output = Path(temporary) / "export"
                family = resource["payloadFamily"]
                actual = installed._run([str(cli), "export-installed-" + family + "-project",
                    str(Path(os.path.abspath(project_path))), project["projectSha256"], project["trackId"],
                    project["regionId"], ",".join(project["languages"]), str(Path(os.path.abspath(package_path))),
                    resource["packageDigest"], resource["resourceCandidateSha256"],
                    str(Path(os.path.abspath(installed_directory))), str(key), str(output)], timeout_seconds)
                validate_record(actual)
                expected_fields = {k: v for k, v in record.items() if k != "exportReceiptSha256"}
                actual_fields = {k: v for k, v in actual.items() if k != "exportReceiptSha256"}
                if _canonical(actual_fields) != _canonical(expected_fields):
                    raise ValueError("retained operation differs from fresh native rendering")
                with _ExportSnapshot(output) as fresh:
                    replay_receipt = _parse_json(fresh.pinned("receipt.json", actual["exportReceiptSha256"], RECEIPT_LIMIT, True)[0])
                    if _canonical(_receipt_identity(replay_receipt)) != _canonical(stable):
                        raise ValueError("retained receipt differs from fresh native export semantics")
                    fresh.files(replay_receipt.get("files"))
                    fresh.files(actual["artifacts"])
            retained.pinned("receipt.json", record["exportReceiptSha256"], RECEIPT_LIMIT)
            retained.files(receipt["files"])
            retained.files(record["artifacts"])
            if os.path.lexists(journal):
                raise ValueError("retained export transaction changed during replay")
            retained.unchanged()
        installed._pinned(cli, cli_sha256, installed.BINARY_LIMIT, "native verifier")
        installed._pinned(key, public_key_sha256, installed.KEY_LIMIT, "trusted public key")
        result.update(status="ENGINEERING_PASS", passed=True, recordType=record["recordType"],
            recordSha256=record_sha256, verifierSha256=cli_sha256, publicKeySha256=public_key_sha256,
            projectSha256=project["projectSha256"], installedResourceTreeSha256=resource["installedResourceTreeSha256"],
            masterSha256=record["masterSha256"], observation="FRESH_NATIVE_RENDER_MATCH",
            historicalExecution="NOT_AUTHENTICATED", qualification="NOT_QUALIFIED",
            ignoredReceiptFields=sorted(VOLATILE_RECEIPT))
    except (OSError, ValueError, TypeError, KeyError, OverflowError, subprocess.SubprocessError) as error:
        result["errors"] = [str(error)[:2000]]
    return result
