"""Closed, caller-pinned replay of installed procedural snapshot continuity.

Engineering only. Retained data supplies no commands, keys or resource paths.
Fresh native execution plus held output reads do not authenticate past execution,
qualify acoustic quality, freeze tolerances or authorize a release.
"""
from __future__ import annotations

import hashlib
import math
import os
from pathlib import Path
import re
import stat
import struct
import subprocess
import tempfile

from . import installed_candidate_record as installed
from .installed_project_binding_record import validate_installed_binding
from .installed_export_record import _ExportSnapshot, _stamp
from .full_product_report import _canonical, _parse_json

FIXED = dict(schemaVersion=1, recordType="seam.u45.installed-procedural-continuity.v1",
    result="ContinuityObserved", scope="SELECTED_PROCEDURAL_SNAPSHOT_API", evidenceScope="ENGINEERING_ONLY",
    backend="procedural", host="seam_voicebank_cli", executableIdentity="EXTERNAL_PIN_REQUIRED",
    engineeringExpectation="EXACT_FLOAT32_WORDS_EQUAL", measuredBeforeEncoder=True,
    acousticF0Measurement="NOT_RUN", acousticPhaseMeasurement="NOT_RUN", neighborInvalidation="NOT_RUN",
    schedulerExecution="NOT_RUN_NO_CACHE_USED", standaloneAudioBlocks="NOT_RUN", windowsExecution="NOT_RUN",
    canonicalTolerance="UNRESOLVED", humanAcceptance="NOT_RUN", qualification="NOT_QUALIFIED",
    authorizesRelease=False, releaseEligible=False, exactFloat32WordsEqual=True,
    restartedMergedMarkerProjectionMatches=True, resumedMergedMarkerProjectionMatches=True)
DIFFERENCES = frozenset(("restartedMaxAbsDifference", "resumedMaxAbsDifference",
    "restartedDifferentFloat32Words", "resumedDifferentFloat32Words"))
FIELDS = frozenset(FIXED) | DIFFERENCES | {"installed", "project", "platform", "sourceCommit", "buildId",
    "sampleRate", "startFrame", "endFrame", "chunkFrames", "compiledContextSha256", "chunks", "artifacts"}
CHUNK_FIELDS = DIFFERENCES | {"index", "startFrame", "endFrame", "checkpointBefore", "checkpointAfter",
    "chunkContentSha256", "sharedCompiledContextSha256"}
MAX_FRAMES = 1920000
MAX_CHUNKS = 64
PARTITION_LIMIT = 8 * 1024 * 1024
AGGREGATE_LIMIT = 64 * 1024 * 1024
FRAME_MIN, FRAME_MAX = -(1 << 63), (1 << 63) - 1


def _shape(value, fields, label):
    if type(value) is not dict or set(value) != fields:
        raise ValueError(f"{label} differs from its closed shape")


def _integer(value, minimum, maximum, label):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"invalid continuity {label}")


def _digest(value, label):
    if type(value) is not str or installed.HEX.fullmatch(value) is None:
        raise ValueError(f"invalid continuity {label}")


def _zero_differences(value):
    for key in DIFFERENCES:
        x = value[key]
        if key.endswith("Words"):
            _integer(x, 0, 0, key)
        elif type(x) not in (int, float) or not math.isfinite(x) or x != 0:
            raise ValueError("continuity engineering expectation requires zero raw differences")


def validate_record(record):
    """This success reader refuses mismatch observations, even with native exit 0."""
    _shape(record, FIELDS, "continuity record")
    for key, expected in FIXED.items():
        if type(record[key]) is not type(expected) or record[key] != expected:
            raise ValueError(f"invalid continuity {key}; successful engineering expectation required")
    validate_installed_binding(record["installed"], record["project"])
    if record["installed"]["payloadFamily"] != "recipe":
        raise ValueError("continuity replay requires the installed procedural recipe family")
    if record["project"]["noteCount"] > 256:
        raise ValueError("continuity region exceeds the native note bound")
    if record["platform"] not in ("macos", "linux"):
        raise ValueError("continuity replay requires the native POSIX platform")
    if type(record["sourceCommit"]) is not str or re.fullmatch(r"[0-9a-f]{40}", record["sourceCommit"]) is None:
        raise ValueError("invalid continuity sourceCommit")
    if type(record["buildId"]) is not str or not 1 <= len(record["buildId"]) <= 256 or "\0" in record["buildId"]:
        raise ValueError("invalid continuity buildId")
    _digest(record["compiledContextSha256"], "compiled context digest")
    _integer(record["sampleRate"], 8000, 192000, "sample rate")
    for key in ("startFrame", "endFrame"):
        _integer(record[key], FRAME_MIN, FRAME_MAX, key)
    duration = record["endFrame"] - record["startFrame"]
    _integer(duration, 1, MAX_FRAMES, "context duration")
    _integer(record["chunkFrames"], 1, MAX_FRAMES, "chunk frame bound")
    chunks = record["chunks"]
    if type(chunks) is not list or not 2 <= len(chunks) <= MAX_CHUNKS or len(chunks) != (duration + record["chunkFrames"] - 1) // record["chunkFrames"]:
        raise ValueError("continuity needs its exact bounded forced partition")
    for i, chunk in enumerate(chunks):
        _shape(chunk, CHUNK_FIELDS, "continuity chunk")
        _integer(chunk["index"], i, i, "chunk index")
        start = record["startFrame"] + i * record["chunkFrames"]
        end = min(start + record["chunkFrames"], record["endFrame"])
        for key, expected in (("startFrame", start), ("endFrame", end), ("checkpointBefore", start), ("checkpointAfter", end)):
            _integer(chunk[key], expected, expected, key)
        _digest(chunk["chunkContentSha256"], "chunk digest")
        if chunk["sharedCompiledContextSha256"] != record["compiledContextSha256"]:
            raise ValueError("continuity chunk declares a different shared context")
        _zero_differences(chunk)
    if sum(c["endFrame"] - record["startFrame"] for c in chunks) > 64000000:
        raise ValueError("continuity restarted-prefix budget exceeded")
    _zero_differences(record)
    expected = {name: (duration, 1, "IEEE_FLOAT32_UNCLAMPED") for name in ("whole.wav", "restarted-joined.wav", "resumed-joined.wav")}
    expected["partition.json"] = (0, 0, "JSON_DERIVED_PLAN_PROJECTION")
    for i, c in enumerate(chunks):
        for mode in ("restarted", "resumed"):
            expected[f"{mode}-{i}.wav"] = (c["endFrame"] - c["startFrame"], 1, "IEEE_FLOAT32_UNCLAMPED")
    files = record["artifacts"]
    if type(files) is not list or len(files) != len(expected):
        raise ValueError("continuity artifacts must cover exactly the native flat output set")
    seen = set()
    for item in files:
        _shape(item, {"path", "sha256", "frames", "channels", "encoding"}, "continuity artifact")
        name = item["path"]
        if type(name) is not str or name not in expected or name in seen:
            raise ValueError("invalid or repeated continuity artifact path")
        seen.add(name); _digest(item["sha256"], "artifact digest")
        frames, channels, encoding = expected[name]
        _integer(item["frames"], frames, frames, "artifact frames")
        _integer(item["channels"], channels, channels, "artifact channels")
        if item["encoding"] != encoding:
            raise ValueError("continuity artifact encoding differs from native output")


class _ContinuitySnapshot(_ExportSnapshot):
    def observation(self, expected):
        """Read and derive the digest of the producer's non-self-hashed JSON."""
        name = "observation.json"
        before = os.stat(name, dir_fd=self.fd, follow_symlinks=False)
        if not stat.S_ISREG(before.st_mode) or before.st_size > installed.RECORD_LIMIT:
            raise ValueError("owned continuity observation must be bounded and regular")
        fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=self.fd)
        with os.fdopen(fd, "rb") as stream:
            opened = os.fstat(stream.fileno())
            if _stamp(opened) != _stamp(before) or not stat.S_ISREG(opened.st_mode):
                raise ValueError("owned continuity observation changed while opening")
            raw = stream.read(before.st_size + 1)
            after = os.fstat(stream.fileno())
        named = os.stat(name, dir_fd=self.fd, follow_symlinks=False)
        if len(raw) != before.st_size or _stamp(opened) != _stamp(after) or _stamp(opened) != _stamp(named):
            raise ValueError("owned continuity observation changed while reading")
        actual = _parse_json(raw); validate_record(actual)
        if _canonical(actual) != _canonical(expected):
            raise ValueError("owned continuity observation differs from supplied record")
        self.unchanged()
        return hashlib.sha256(raw).hexdigest()

    def verify_files(self, record, capture=False):
        used = 0; data = {}
        for x in record["artifacts"]:
            limit = PARTITION_LIMIT if x["path"] == "partition.json" else 44 + x["frames"] * 4
            raw, size = self.pinned(x["path"], x["sha256"], min(limit, AGGREGATE_LIMIT - used), capture)
            used += size
            if capture:
                data[x["path"]] = raw
        self.unchanged()
        return data


def _pcm(raw, rate, frames):
    if len(raw) != 44 + 4 * frames or raw[:4] != b"RIFF" or raw[8:16] != b"WAVEfmt " or raw[36:40] != b"data":
        raise ValueError("continuity WAV differs from the native float32 envelope")
    if struct.unpack_from("<I", raw, 4)[0] != len(raw) - 8 or struct.unpack_from("<I", raw, 16)[0] != 16 or struct.unpack_from("<I", raw, 40)[0] != frames * 4:
        raise ValueError("continuity WAV length fields disagree")
    if struct.unpack_from("<HHIIHH", raw, 20) != (3, 1, rate, rate * 4, 4, 32):
        raise ValueError("continuity WAV format or sample rate disagrees")
    pcm = memoryview(raw)[44:]
    if any(not math.isfinite(x[0]) for x in struct.iter_unpack("<f", pcm)):
        raise ValueError("continuity WAV contains non-finite PCM")
    return pcm


def _marker(marker, start, end):
    _shape(marker, {"key", "phone", "startFrame", "endFrame", "startClipped", "endClipped", "gestureKindNativeEnum", "palatalized"}, "phone projection")
    if type(marker["key"]) is not str or re.fullmatch(r"[0-9a-f]{16}:[0-9]{1,5}", marker["key"]) is None or int(marker["key"].split(":")[1]) > 65535:
        raise ValueError("invalid projected phone key")
    if type(marker["phone"]) is not str or not 1 <= len(marker["phone"]) <= 1024 or "\0" in marker["phone"]:
        raise ValueError("invalid projected phone symbol")
    _integer(marker["startFrame"], start, end - 1, "phone start")
    _integer(marker["endFrame"], marker["startFrame"] + 1, end, "phone end")
    _integer(marker["gestureKindNativeEnum"], 0, 10, "native gesture enum")
    if any(type(marker[k]) is not bool for k in ("startClipped", "endClipped", "palatalized")):
        raise ValueError("invalid projected phone flags")


def _projections(record, partition):
    _shape(partition, {"schemaVersion", "timingSource", "scoreNotes", "wholeMarkers", "chunkMarkers", "boundaries"}, "continuity partition")
    if type(partition["schemaVersion"]) is not int or partition["schemaVersion"] != 1 or partition["timingSource"] != "SHARED_COMPILED_PLAN_PROJECTION_NOT_ACOUSTIC_MEASUREMENT":
        raise ValueError("continuity projections must declare their native derived-plan source")
    notes = partition["scoreNotes"]
    if type(notes) is not list or len(notes) != record["project"]["noteCount"] or len(notes) > 256:
        raise ValueError("projected note count differs from selected project")
    seen_notes = set()
    for note in notes:
        _shape(note, {"noteId", "startFrame", "endFrame", "midiKey", "articulationNativeEnum", "vibratoEnabled", "vibratoPhaseTurns"}, "score note projection")
        if type(note["noteId"]) is not str or re.fullmatch(r"[0-9a-f]{16}", note["noteId"]) is None or int(note["noteId"], 16) == 0 or note["noteId"] in seen_notes:
            raise ValueError("invalid or repeated projected note identity")
        seen_notes.add(note["noteId"])
        _integer(note["startFrame"], FRAME_MIN, FRAME_MAX, "note start")
        _integer(note["endFrame"], note["startFrame"] + 1, FRAME_MAX, "note end")
        _integer(note["midiKey"], 0, 127, "MIDI key"); _integer(note["articulationNativeEnum"], 0, 2, "articulation")
        if type(note["vibratoEnabled"]) is not bool or type(note["vibratoPhaseTurns"]) not in (int, float) or not math.isfinite(note["vibratoPhaseTurns"]):
            raise ValueError("invalid projected vibrato parameters")
    whole = partition["wholeMarkers"]
    if type(whole) is not list or len(whole) > 128:
        raise ValueError("phone projection exceeds native marker bound")
    expected = {}
    for marker in whole:
        _marker(marker, record["startFrame"], record["endFrame"])
        if marker["key"] in expected or marker["key"].split(":")[0] not in seen_notes:
            raise ValueError("repeated or unknown projected phone identity")
        expected[marker["key"]] = marker
    chunks = partition["chunkMarkers"]
    if type(chunks) is not list or len(chunks) != len(record["chunks"]):
        raise ValueError("projected chunk count differs")
    merged = {mode: {} for mode in ("restarted", "resumed")}
    for i, part in enumerate(chunks):
        _shape(part, {"index", "restarted", "resumed"}, "chunk projections"); _integer(part["index"], i, i, "projected index")
        c = record["chunks"][i]
        for mode, result in merged.items():
            markers = part[mode]
            if type(markers) is not list or len(markers) > 128:
                raise ValueError("invalid bounded chunk phone projections")
            for marker in markers:
                _marker(marker, c["startFrame"], c["endFrame"])
                old = result.get(marker["key"])
                if old is None:
                    result[marker["key"]] = dict(marker)
                else:
                    if any(old[k] != marker[k] for k in ("phone", "gestureKindNativeEnum", "palatalized")) or old["endFrame"] != marker["startFrame"] or not old["endClipped"] or not marker["startClipped"]:
                        raise ValueError("split phone projections do not merge contiguously")
                    old.update(endFrame=marker["endFrame"], endClipped=marker["endClipped"])
    if any(_canonical(value) != _canonical(expected) for value in merged.values()):
        raise ValueError("merged phone projections differ from the whole plan")
    boundaries = partition["boundaries"]
    if type(boundaries) is not list or len(boundaries) != len(record["chunks"]) - 1:
        raise ValueError("invalid projected boundary count")
    for i, boundary in enumerate(boundaries):
        _shape(boundary, {"frame", "insidePlannedPhones"}, "boundary projection")
        frame = record["chunks"][i]["endFrame"]; _integer(boundary["frame"], frame, frame, "boundary frame")
        phones = [m for m in whole if m["startFrame"] < frame < m["endFrame"]]
        if _canonical(boundary["insidePlannedPhones"]) != _canonical(phones):
            raise ValueError("projected boundary phones differ from the whole plan")


def _verify_raw(record, data):
    duration = record["endFrame"] - record["startFrame"]; rate = record["sampleRate"]
    whole = _pcm(data["whole.wav"], rate, duration)
    for mode in ("restarted", "resumed"):
        if _pcm(data[mode + "-joined.wav"], rate, duration) != whole:
            raise ValueError("joined continuity PCM differs from whole float32 words")
        for c in record["chunks"]:
            begin = c["startFrame"] - record["startFrame"]; length = c["endFrame"] - c["startFrame"]
            pcm = _pcm(data[f'{mode}-{c["index"]}.wav'], rate, length)
            if pcm != whole[begin * 4:(begin + length) * 4]:
                raise ValueError("owned continuity PCM differs from whole float32 words")
    _projections(record, _parse_json(data["partition.json"]))


def audit_installed_continuity_record(*, record_path, record_sha256, project_path, package_path,
        installed_directory, continuity_directory, public_key_path, public_key_sha256,
        cli_path, cli_sha256, timeout_seconds=60):
    result = dict(status="BLOCKED", passed=False, authorizesRelease=False, evidenceScope="ENGINEERING_ONLY", errors=[])
    try:
        if type(timeout_seconds) not in (int, float) or not math.isfinite(timeout_seconds) or not 0 < timeout_seconds <= 300:
            raise ValueError("native continuity replay timeout must be within (0, 300] seconds")
        record = _parse_json(installed._pinned(record_path, record_sha256, installed.RECORD_LIMIT, "continuity record")); validate_record(record)
        cli, key = Path(os.path.abspath(cli_path)), Path(os.path.abspath(public_key_path))
        installed._pinned(cli, cli_sha256, installed.BINARY_LIMIT, "native verifier")
        installed._pinned(key, public_key_sha256, installed.KEY_LIMIT, "trusted public key")
        with _ContinuitySnapshot(continuity_directory) as retained:
            owned_observation_sha = retained.observation(record)
            _verify_raw(record, retained.verify_files(record, True))
            with tempfile.TemporaryDirectory(prefix="seam-continuity-replay-") as scratch:
                output = Path(scratch) / "continuity"
                actual = installed._run([str(cli), "observe-installed-procedural-continuity",
                    str(Path(os.path.abspath(project_path))), record["project"]["projectSha256"],
                    record["project"]["trackId"], record["project"]["regionId"], ",".join(record["project"]["languages"]),
                    str(Path(os.path.abspath(package_path))), record["installed"]["packageDigest"],
                    record["installed"]["resourceCandidateSha256"], str(Path(os.path.abspath(installed_directory))),
                    str(key), str(record["chunkFrames"]), str(output)], timeout_seconds)
                validate_record(actual)
                if _canonical(actual) != _canonical(record):
                    raise ValueError("retained continuity differs from fresh native rendering")
                with _ContinuitySnapshot(output) as fresh:
                    fresh_observation_sha = fresh.observation(actual)
                    _verify_raw(actual, fresh.verify_files(actual, True))
                    fresh.pinned("observation.json", fresh_observation_sha, installed.RECORD_LIMIT)
                    fresh.verify_files(actual)
            retained.pinned("observation.json", owned_observation_sha, installed.RECORD_LIMIT)
            retained.verify_files(record)
            retained.unchanged()
        installed._pinned(record_path, record_sha256, installed.RECORD_LIMIT, "continuity record")
        installed._pinned(cli, cli_sha256, installed.BINARY_LIMIT, "native verifier")
        installed._pinned(key, public_key_sha256, installed.KEY_LIMIT, "trusted public key")
        result.update(status="ENGINEERING_PASS", passed=True, recordType=record["recordType"],
            recordSha256=record_sha256, verifierSha256=cli_sha256, publicKeySha256=public_key_sha256,
            ownedObservationSha256=owned_observation_sha, projectSha256=record["project"]["projectSha256"],
            installedResourceTreeSha256=record["installed"]["installedResourceTreeSha256"],
            chunks=len(record["chunks"]), observation="FRESH_NATIVE_CONTINUITY_MATCH", rawFloatWordsEqual=True,
            derivedPhoneProjectionsMatch=True, historicalExecution="NOT_AUTHENTICATED",
            qualification="NOT_QUALIFIED", canonicalTolerance="UNRESOLVED", ignoredRecordFields=[])
    except (OSError, ValueError, TypeError, KeyError, OverflowError, struct.error, subprocess.SubprocessError) as error:
        result["errors"] = [str(error)[:2000]]
    return result
