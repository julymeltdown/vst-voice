"""Owned metadata checks matching the native prepared-donor packet facts reader.

No filesystem access, audio measurements, source registration or approval.
"""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import PurePosixPath
import re

FILE_LIMITS = {"donors.json": 16 * 1024 * 1024, "intake-plan.json": 1024 * 1024,
               "license-source.txt": 1024 * 1024, "attribution.txt": 2 * 1024 * 1024}
HEX = re.compile(r"[0-9a-f]{64}\Z")
SOURCE_ID = re.compile(r"[a-z0-9][a-z0-9_-]{0,79}\Z")
DONOR_ID = re.compile(r"[a-z0-9_-]{1,128}\Z")
LABEL_CONTRACT = {"format": "festvox-end-times-v1", "annotationStatus": "AUTOMATIC_UNREVIEWED",
                  "timeUnit": "seconds", "boundaryRule": "ceil-seconds-times-sample-rate-v1"}
DONOR_FIELDS = {"packetSha256", "donorId", "intakePlanSha256", "originalLicenseSha256", "attributionSha256"}


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError("Prepared donor " + message)


def _shape(value, keys) -> None:
    _require(isinstance(value, dict) and set(value) == set(keys), "record has missing or unknown fields")


def _text(value, key: str) -> str:
    result = value.get(key)
    _require(isinstance(result, str), "field must be text: " + key)
    return result


def _integer(value, key: str) -> int:
    result = value.get(key)
    _require(type(result) is int and -(1 << 63) <= result < (1 << 63), "field must be an int64 token: " + key)
    return result


def _digest(value) -> bool:
    return isinstance(value, str) and HEX.fullmatch(value) is not None


def _same(left, right) -> bool:
    if type(left) is not type(right):
        return False
    if isinstance(left, dict):
        return left.keys() == right.keys() and all(_same(left[key], right[key]) for key in left)
    if isinstance(left, list):
        return len(left) == len(right) and all(_same(a, b) for a, b in zip(left, right))
    return left == right


def _parse(raw: bytes) -> dict:
    def unique(pairs):
        result = {}
        for key, value in pairs:
            _require(key not in result, "JSON contains a duplicate key")
            result[key] = value
        return result

    def invalid(value):
        raise ValueError("Prepared donor JSON contains a nonfinite constant: " + value)

    def integer_token(token):
        number = int(token)
        _require(-(1 << 63) <= number < (1 << 63), "JSON integer is outside int64")
        return number

    value = json.loads(raw.decode("utf-8"), object_pairs_hook=unique, parse_constant=invalid, parse_int=integer_token)
    _require(isinstance(value, dict), "JSON root must be an object")
    pending, nodes = [(value, 0)], 0
    while pending:
        item, depth = pending.pop()
        nodes += 1
        _require(nodes <= 500_000 and depth <= 16, "JSON exceeds node/depth bounds")
        if isinstance(item, (dict, list)):
            _require(depth < 16 and len(item) <= 16384, "JSON exceeds collection/depth bounds")
            if isinstance(item, dict):
                _require(all(len(key.encode("utf-8")) <= 2 * 1024 * 1024 for key in item), "JSON key exceeds bounds")
                pending.extend((child, depth + 1) for child in item.values())
            else:
                pending.extend((child, depth + 1) for child in item)
        elif isinstance(item, str):
            _require(len(item.encode("utf-8")) <= 2 * 1024 * 1024, "JSON string exceeds bounds")
        elif type(item) is float:
            _require(math.isfinite(item), "JSON number is not finite")
        elif type(item) is int:
            _require(-(1 << 63) <= item < (1 << 63), "JSON integer is outside int64")
    return value


def _source_binding(value) -> None:
    _shape(value, {"path", "sha256"})
    name = _text(value, "path")
    _require(bool(name) and not PurePosixPath(name).is_absolute() and
             ".." not in PurePosixPath(name).parts and _digest(value.get("sha256")), "original source binding is invalid")


def prepared_packet_facts(files: dict[str, bytes], expected_packet_sha256: str) -> dict:
    """Validate all four held files once; return declared donor digests, no WAV facts."""
    _require(set(files) == set(FILE_LIMITS) and _digest(expected_packet_sha256), "held files or expected identity are invalid")
    for name, limit in FILE_LIMITS.items():
        _require(type(files[name]) is bytes and len(files[name]) <= limit, "file exceeds byte bounds: " + name)
    digests = {name: hashlib.sha256(raw).hexdigest() for name, raw in files.items()}
    _require(digests["donors.json"] == expected_packet_sha256, "packet SHA-256 differs")
    packet = _parse(files["donors.json"])
    _shape(packet, {"schemaVersion", "documentKind", "status", "language", "planSha256", "labelContract", "license",
                    "retainedLicensePath", "retainedAttributionPath", "retainedAttributionSha256", "changeNotice",
                    "producerRegistration", "performerRights", "rightsClearance", "bankAdmission", "proposals"})
    _require(_integer(packet, "schemaVersion") == 1 and packet["labelContract"] == LABEL_CONTRACT,
             "schema or label contract is unsupported")
    for key, expected in {"documentKind": "UnassignedPhoneDonorProposals", "status": "UNAPPROVED", "language": "en",
                          "producerRegistration": "NOT_REGISTERED", "performerRights": "NOT_ESTABLISHED",
                          "rightsClearance": "NOT_APPROVED", "bankAdmission": "NOT_APPROVED",
                          "retainedLicensePath": "license-source.txt", "retainedAttributionPath": "attribution.txt"}.items():
        _require(_text(packet, key) == expected, "unapproved field differs: " + key)
    _require(bool(_text(packet, "changeNotice")), "change notice is missing")
    license_record = packet["license"]
    _shape(license_record, {"path", "sha256", "sourceUrl", "attribution"})
    _require(bool(_text(license_record, "sourceUrl")) and bool(_text(license_record, "attribution")), "source URL/attribution is missing")
    _require(digests["license-source.txt"] == _text(license_record, "sha256") and
             digests["attribution.txt"] == _text(packet, "retainedAttributionSha256") and
             digests["intake-plan.json"] == _text(packet, "planSha256"), "retained notice SHA-256 differs")
    plan = _parse(files["intake-plan.json"])
    _shape(plan, {"schemaVersion", "documentKind", "language", "labelContract", "license", "sources", "spans"})
    _require(_integer(plan, "schemaVersion") == 1 and plan["labelContract"] == LABEL_CONTRACT and
             _same(plan["license"], license_record) and _text(plan, "documentKind") == "SourceLabelDonorPlan" and
             _text(plan, "language") == "en", "retained plan provenance differs")
    proposals, spans, sources = packet["proposals"], plan["spans"], plan["sources"]
    _require(isinstance(proposals, list) and 1 <= len(proposals) <= 4096 and isinstance(spans, list) and
             len(spans) == len(proposals) and isinstance(sources, list) and 1 <= len(sources) <= 32, "plan/proposal bounds differ")
    by_source = {}
    for source in sources:
        _shape(source, {"id", "sourceType", "audio", "labels"})
        identity = _text(source, "id")
        _require(SOURCE_ID.fullmatch(identity) is not None and identity not in by_source and
                 _text(source, "sourceType") == "speech", "source identity is invalid or duplicated")
        _source_binding(source["audio"]); _source_binding(source["labels"])
        by_source[identity] = source
    donors = {}
    for row, span in zip(proposals, spans):
        _shape(row, {"id", "derivedPath", "derivedSha256", "pcmSha256", "sourceId", "sourceType", "sourceAudio", "sourceLabels",
                     "firstLabelIndex", "labelCount", "sourceAnnotations", "sourcePhones", "startFrame", "endFrameExclusive",
                     "sampleRate", "frames", "durationSeconds", "nonzeroPcm", "assignment", "pitchReview", "phoneticReview", "listeningReview"})
        _shape(span, {"sourceId", "firstLabelIndex", "labelCount"})
        for key, expected in {"assignment": "NOT_ASSIGNED", "pitchReview": "NOT_REVIEWED",
                              "phoneticReview": "NOT_REVIEWED", "listeningReview": "NOT_REVIEWED"}.items():
            _require(_text(row, key) == expected, "review field differs: " + key)
        source_id, first, count = _text(span, "sourceId"), _integer(span, "firstLabelIndex"), _integer(span, "labelCount")
        _require(source_id in by_source and 0 <= first < 16384 and 1 <= count <= 3, "span identity is invalid")
        identity = f"{source_id}_l{first:05d}_n{count}"
        _require(_text(row, "id") == identity and identity not in donors and _text(row, "derivedPath") == identity + ".wav" and
                 _text(row, "sourceId") == source_id and _integer(row, "firstLabelIndex") == first and
                 _integer(row, "labelCount") == count and _text(row, "sourceType") == "speech", "identity differs from plan")
        source = by_source[source_id]
        _require(row["sourceAudio"] == source["audio"] and row["sourceLabels"] == source["labels"], "original bindings differ")
        _require(_digest(row["derivedSha256"]) and _digest(row["pcmSha256"]), "audio digest is invalid")
        rate, frames = _integer(row, "sampleRate"), _integer(row, "frames")
        start, end = _integer(row, "startFrame"), _integer(row, "endFrameExclusive")
        duration = row["durationSeconds"]
        _require(type(row["nonzeroPcm"]) is bool and type(duration) in (int, float) and math.isfinite(duration), "PCM fact types are invalid")
        _require(8000 <= rate <= 192000 and 0 < frames <= (64 * 1024 * 1024 - 44) // 2 and
                 0 <= start < end and end - start == frames and duration == frames / rate, "declared PCM facts are inconsistent or exceed bounds")
        donors[identity] = row["derivedSha256"]
    return {"packetSha256": expected_packet_sha256, "intakePlanSha256": digests["intake-plan.json"],
            "originalLicenseSha256": digests["license-source.txt"], "attributionSha256": digests["attribution.txt"],
            "donors": donors}
