"""Read-only engineering packet semantics; no physical or release admission."""
from __future__ import annotations

from contextlib import contextmanager
from dataclasses import dataclass, field
import hashlib
import json
import math
import os
import re
import stat
import sys


HEX64 = re.compile(r"[0-9a-f]{64}")
INDEX_LIMIT = 65536
METADATA_LIMIT = ROW_LIMIT = 4096
RECEIPT_LIMIT = PRODUCT_LIMIT = 16 * 1024 * 1024
SESSION_REQUIRED = "SOAK_SESSION_REQUIRED: exactly one soak-session-index evidence item is required"
ENGINEERING_ONLY = "SOAK_ENGINEERING_ONLY: engineering packets cannot qualify physical or release admission"


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)


def _digest(data):
    return hashlib.sha256(data).hexdigest()


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _number(value, minimum=0, maximum=float("inf")):
    return (type(value) in (int, float) and minimum <= value <= maximum
            and (type(value) is int or math.isfinite(value)))


def _integer(value, minimum=0):
    return type(value) is int and value >= minimum


def parse_json(data):
    def unique(pairs):
        value = {}
        for key, item in pairs:
            _require(key not in value, "duplicate JSON key")
            value[key] = item
        return value
    def constant(value):
        raise ValueError("nonfinite JSON number")
    def finite(value):
        number = float(value)
        _require(math.isfinite(number), "nonfinite JSON number")
        return number
    try:
        value = json.loads(data, object_pairs_hook=unique, parse_constant=constant, parse_float=finite)
        pending = [(value, 0)]
        while pending:
            item, depth = pending.pop()
            _require(depth <= 64, "JSON depth exceeds 64")
            if isinstance(item, dict):
                pending.extend((child, depth + 1) for child in item.values())
            elif isinstance(item, list):
                pending.extend((child, depth + 1) for child in item)
        return value
    except (RecursionError, UnicodeError) as exc:
        raise ValueError("invalid bounded JSON") from exc


@dataclass
class SoakReplayContext:
    # Successful bytes and failed reads are reused across all consumers in one call.
    reads: dict = field(default_factory=dict)
    sessions: dict = field(default_factory=dict)
    claims: dict = field(default_factory=dict)
    coverage: dict = field(default_factory=dict)

    def consume(self, namespace, cell, session_id):
        key = (namespace, cell)
        previous = self.coverage.get(key)
        if previous == session_id:
            return False  # identical citation receives no additional coverage credit
        if previous is not None:
            raise ValueError("conflicting soak coverage cell")
        _require(session_id not in (value for (scope, _), value in self.coverage.items() if scope == namespace),
                 "replayed session cannot fill another coverage cell")
        self.coverage[key] = session_id
        return True


def _parts(locator):
    _require(isinstance(locator, str) and bool(locator) and "\\" not in locator,
             "reference requires a safe relative locator")
    parts = locator.split("/")
    _require(not locator.startswith("/") and all(part not in ("", ".", "..") for part in parts),
             "reference requires a safe relative locator")
    return parts


def _root_key(root):
    _require(root is not None, "explicit evidence root is required")
    return os.path.abspath(os.fspath(root))


def _reference_parts(reference):
    _require(isinstance(reference, dict) and set(reference) == {"locator", "sha256"},
             "reference requires exactly locator and sha256")
    parts, digest = _parts(reference["locator"]), reference["sha256"]
    _require(isinstance(digest, str) and HEX64.fullmatch(digest), "reference SHA-256 is invalid")
    return parts, digest


@contextmanager
def _opened(root, parts):
    descriptors = []
    flags = os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW
    try:
        descriptor = os.open(root, flags | os.O_DIRECTORY)
        descriptors.append(descriptor)
        for part in parts[:-1]:
            descriptor = os.open(part, flags | os.O_DIRECTORY, dir_fd=descriptor)
            descriptors.append(descriptor)
        descriptor = os.open(parts[-1], flags, dir_fd=descriptor)
        descriptors.append(descriptor)
        yield descriptor
    finally:
        original = sys.exc_info()[1]
        cleanup_error = None
        for descriptor in reversed(descriptors):
            try:
                os.close(descriptor)
            except OSError as exc:
                cleanup_error = cleanup_error or exc
        if original is None and cleanup_error is not None:
            raise cleanup_error


def _stamp(info):
    return tuple(getattr(info, key) for key in ("st_dev", "st_ino", "st_size", "st_mtime_ns", "st_ctime_ns"))


def read_reference(reference, *, evidence_root, maximum_bytes, replay_context):
    """The only content reader for new soak references, including terminal denials."""
    root = _root_key(evidence_root)
    parts, digest = _reference_parts(reference)
    key = (root, reference["locator"])
    cached = replay_context.reads.get(key)
    if cached is None:
        try:
            with _opened(root, parts) as descriptor:
                before = os.fstat(descriptor)
                _require(stat.S_ISREG(before.st_mode), "reference is not a regular file")
                _require(before.st_size <= maximum_bytes, "reference exceeds its byte limit")
                data = bytearray()
                while len(data) <= before.st_size:
                    chunk = os.read(descriptor, min(1024 * 1024, before.st_size + 1 - len(data)))
                    if not chunk:
                        break
                    data.extend(chunk)
                after = os.fstat(descriptor)
                _require(_stamp(before) == _stamp(after) and len(data) == before.st_size,
                         "reference changed while reading")
            cached = (bytes(data), _stamp(after))
        except (OSError, ValueError) as exc:
            cached = exc
        replay_context.reads[key] = cached
    if isinstance(cached, Exception):
        raise ValueError(f"reference cannot be read: {type(cached).__name__}: {cached}") from cached
    data, _ = cached
    _require(len(data) <= maximum_bytes, "reference exceeds its byte limit")
    _require(_digest(data) == digest, "reference content digest does not match bytes")
    return data


def _recheck(reference, root, context):
    parts, _ = _reference_parts(reference)
    key = (_root_key(root), reference["locator"])
    _, stamp = context.reads[key]
    try:
        with _opened(key[0], parts) as descriptor:
            _require(_stamp(os.fstat(descriptor)) == stamp, "committed index changed during validation")
    except (OSError, ValueError) as exc:
        context.reads[key] = exc  # never reopen this failed reference through another consumer
        raise


def reuse_reference(reference, *, base, maximum_bytes, replay_context):
    """Return already guarded bytes for aliases; None means unrelated legacy evidence."""
    if replay_context is None:
        return None
    _reference_parts(reference)
    target = os.path.abspath(os.path.join(os.fspath(base), reference["locator"]))
    for root, locator in replay_context.reads:
        if os.path.abspath(os.path.join(root, locator)) == target:
            return read_reference({"locator": locator, "sha256": reference.get("sha256")},
                evidence_root=root, maximum_bytes=maximum_bytes, replay_context=replay_context)
    return None


def _failure_file_absent(locator, root, context):
    root, parts = _root_key(root), _parts(locator)
    key = (root, locator)
    cached = context.reads.get(key)
    if cached is None:
        try:
            with _opened(root, parts):
                cached = ValueError("retained worker/session failure prevents completion")
        except (OSError, ValueError) as exc:
            cached = exc
        context.reads[key] = cached
    if isinstance(cached, FileNotFoundError):
        return
    if isinstance(cached, Exception):
        raise ValueError(f"failure artifact cannot be inspected: {type(cached).__name__}: {cached}") from cached
    raise ValueError("retained worker/session failure prevents completion")


@dataclass(frozen=True)
class SoakSessionValidation:
    valid: bool
    errors: tuple[str, ...] = ()
    session_id: str | None = None
    index_sha256: str | None = None
    manifest_sha256: str | None = None
    duration_seconds: int | None = None
    manifest: dict | None = None
    samples_sha256: str | None = None
    evidence_scope: str = "engineering"
    release_eligible: bool = False


def _observations(receipt, duration):
    poll, gap = receipt.get("pollIntervalSeconds"), receipt.get("maxGapSeconds")
    _require(_number(poll, .1, 1) and _number(gap, 1, 5), "supervisor timing budgets differ")
    for key, value in (("heartbeatIntervalSeconds", 1), ("heartbeatLatenessSeconds", 1), ("endpointAckSeconds", 1)):
        _require(type(receipt.get(key)) is int and receipt[key] == value, "supervisor timing budgets differ")
    _require(receipt.get("endpointProtocol") == "durable-final-sample-v1", "endpoint protocol differs")
    observations = receipt.get("observations")
    _require(isinstance(observations, list) and 0 < len(observations) <= math.ceil((duration + 1) / poll) + 2,
             "observation count is invalid")
    previous, sequence, last_progress = None, 0, 0.0
    for observation in observations:
        _require(isinstance(observation, dict) and set(observation) == {"elapsedSeconds", "heartbeatSequence", "heartbeatAgeSeconds"},
                 "observation fields differ")
        elapsed, observed, age = (observation[key] for key in ("elapsedSeconds", "heartbeatSequence", "heartbeatAgeSeconds"))
        _require(_number(elapsed, 0, duration + 1) and _integer(observed) and _number(age), "observation values are invalid")
        if previous is None:
            _require(elapsed <= poll, "initial observation missed cadence")
        else:
            target = min(previous + poll, duration if previous < duration else duration + 1)
            _require(elapsed > previous and elapsed + 1e-6 >= target, "observation clock reversed or polled early")
            _require(elapsed - previous <= gap, "observation gap exceeds limit")
        _require(sequence <= observed <= int(elapsed) + 2 and observed <= 2**63 - 1,
                 "observed heartbeat sequence reversed or advanced too fast")
        _require(observed >= max(0, int(elapsed) + 1 - 1), "observed heartbeat sequence is stale")
        if observed > sequence:
            last_progress = elapsed
        _require(age == elapsed - last_progress and age <= gap, "heartbeat age differs or is stale")
        sequence, previous = observed, elapsed
    _require(previous >= duration and sequence == duration + 1, "supervision lacks final duration or sequence")
    return previous


def _validate_packet(reference, root, context):
    _require(_parts(reference.get("locator"))[-1] == "packet-index.json", "committed packet-index.json is required")
    index = parse_json(read_reference(reference, evidence_root=root, maximum_bytes=INDEX_LIMIT, replay_context=context))
    _require(isinstance(index, dict) and set(index) == {"status", "evidenceScope", "releaseEligible", "sessionId", "manifestSha256", "durationSeconds", "cleanupErrors", "files"},
             "committed index fields differ")
    _require(index["status"] == "COMPLETE" and index["evidenceScope"] == "engineering" and index["releaseEligible"] is False,
             "index status or engineering authority differs")
    _require(index["cleanupErrors"] == [], "adapter cleanup is unclean")
    duration = index["durationSeconds"]
    _require(type(duration) is int and duration in (1800, 7200), "index duration is invalid")
    limits = {"session.json": METADATA_LIMIT, "samples.jsonl": (duration + 1) * ROW_LIMIT,
              "final-sample.json": ROW_LIMIT, "worker-result.json": METADATA_LIMIT, "supervision.json": RECEIPT_LIMIT}
    files = index["files"]
    _require(isinstance(files, dict) and set(files) == set(limits), "required packet artifacts differ or contain failure files")
    parent = reference["locator"].rsplit("/", 1)[0] + "/" if "/" in reference["locator"] else ""
    data = {}
    for name, limit in limits.items():
        item = files[name]
        _require(isinstance(item, dict) and set(item) == {"sha256", "bytes"} and _integer(item["bytes"]),
                 "indexed artifact metadata differs")
        contents = read_reference({"locator": parent + name, "sha256": item["sha256"]},
                                  evidence_root=root, maximum_bytes=limit, replay_context=context)
        _require(len(contents) == item["bytes"], "indexed artifact byte length differs")
        data[name] = contents
    # Failure files omitted from an index still invalidate completion; use no-follow inspection.
    for name in ("worker-error.json", "session-error.json"):
        _failure_file_absent(parent + name, root, context)
    manifest = parse_json(data["session.json"])
    _require(isinstance(manifest, dict) and data["session.json"] == (canonical(manifest) + "\n").encode(), "manifest bytes are not canonical")
    _require(_digest(data["session.json"]) == index["manifestSha256"], "manifest digest binding differs")
    session = index["sessionId"]
    _require(isinstance(session, str) and bool(session.strip()) and manifest.get("sessionId") == session
             and type(manifest.get("durationSeconds")) is int and manifest["durationSeconds"] == duration,
             "manifest session or duration binding differs")
    _require(type(manifest.get("schemaVersion")) is int and manifest["schemaVersion"] == 1
             and manifest.get("recordType") == "external-beta-engineering-soak-session"
             and manifest.get("evidenceScope") == "engineering" and manifest.get("releaseEligible") is False,
             "manifest version or engineering authority differs")
    for key in ("installedTreeSha256", "workloadSha256", "machineProfileSha256"):
        _require(isinstance(manifest.get(key), str) and HEX64.fullmatch(manifest[key]), "manifest digest is invalid")
    _require(isinstance(manifest.get("machineProfileId"), str) and bool(manifest["machineProfileId"].strip()), "machine profile ID is missing")
    receipt = parse_json(data["supervision.json"])
    _require(isinstance(receipt, dict) and set(receipt) == {
        "schemaVersion", "recordType", "status", "recordId", "evidenceScope", "releaseEligible", "requiredSeconds",
        "installedTreeSha256", "workloadSha256", "productPid", "collectorPid", "supervisorPid", "clockAuthority",
        "pollIntervalSeconds", "maxGapSeconds", "heartbeatIntervalSeconds", "heartbeatLatenessSeconds",
        "endpointAckSeconds", "endpointProtocol", "cleanupErrors", "observations", "finishedAcknowledgement"},
        "receipt COMPLETE fields differ or contain failure state")
    _require(isinstance(receipt, dict) and type(receipt.get("schemaVersion")) is int and receipt["schemaVersion"] == 2
             and receipt.get("recordType") == "external-beta-soak-supervision" and receipt.get("status") == "COMPLETE"
             and receipt.get("evidenceScope") == "engineering" and receipt.get("releaseEligible") is False
             and receipt.get("clockAuthority") == "supervisor-monotonic", "receipt version, status or authority differs")
    _require(receipt.get("cleanupErrors") == [], "supervisor cleanup is unclean")
    for key, expected in (("recordId", session), ("requiredSeconds", duration), ("installedTreeSha256", manifest["installedTreeSha256"]), ("workloadSha256", manifest["workloadSha256"])):
        _require(type(receipt.get(key)) is type(expected) and receipt[key] == expected, "receipt session or identity binding differs")
    pids = [receipt.get(key) for key in ("productPid", "collectorPid", "supervisorPid")]
    _require(all(_integer(pid, 1) for pid in pids) and len(set(pids)) == 3, "receipt process identities differ")
    worker = parse_json(data["worker-result.json"])
    _require(canonical(worker) == canonical({"status": "FINISHED", "sessionId": session, "manifestSha256": index["manifestSha256"],
        "sampleCount": duration + 1, "productPid": pids[0], "collectorPid": pids[1]}), "worker result binding differs")
    lines = data["samples.jsonl"].splitlines()
    _require(data["samples.jsonl"].endswith(b"\n") and len(lines) == duration + 1, "raw sample count or complete row framing differs")
    samples, previous = [], -1
    for target, line in enumerate(lines):
        _require(len(line) + 1 <= ROW_LIMIT, "raw row exceeds byte limit")
        row = parse_json(line)
        _require(isinstance(row, dict) and set(row) == {"sessionId", "manifestSha256", "productPid", "sequence", "targetElapsedSeconds", "sample"}, "raw row fields differ")
        for key, expected in (("sessionId", session), ("manifestSha256", index["manifestSha256"]), ("productPid", pids[0]), ("sequence", target + 1), ("targetElapsedSeconds", target)):
            _require(type(row[key]) is type(expected) and row[key] == expected, "raw row session or sequence binding differs")
        sample = row["sample"]
        _require(isinstance(sample, dict) and _number(sample.get("elapsedSeconds"), target)
                 and sample["elapsedSeconds"] < target + 1 and sample["elapsedSeconds"] > previous,
                 "raw sample absolute timing differs")
        previous = sample["elapsedSeconds"]
        integer_metrics = {"rssBytes", "handles", "threads", "queueDepth", "mediaBudgetHighWaterBytes", "underflows", "xruns", "controlQueueOverflow"}
        numeric_metrics = integer_metrics | {"cpuPercent", "renderLatencyMs", "callbackLatencyUs", "queueAgeMs", "cacheEvictionStallMs"}
        for name in numeric_metrics & sample.keys():
            _require(_integer(sample[name]) if name in integer_metrics else _number(sample[name]), "raw sample metric is invalid")
        samples.append(sample)
    final_observation = _observations(receipt, duration)
    acknowledgement = receipt.get("finishedAcknowledgement")
    _require(isinstance(acknowledgement, dict) and set(acknowledgement) == {"frame", "receivedElapsedSeconds", "finalSample"}, "FINISHED acknowledgement is missing or malformed")
    received = acknowledgement["receivedElapsedSeconds"]
    _require(_number(received, duration, duration + 1) and received <= final_observation
             and any(row["elapsedSeconds"] == received for row in receipt["observations"]), "FINISHED receipt timing differs")
    completion = next((index for index, row in enumerate(receipt["observations"])
                       if row["elapsedSeconds"] >= received and row["heartbeatSequence"] == duration + 1), None)
    _require(completion == len(receipt["observations"]) - 1, "observations continue after the completed endpoint join")
    snapshot = parse_json(data["final-sample.json"])
    expected_snapshot = {"schemaVersion": 1, "recordType": "external-beta-final-soak-sample", "recordId": session,
                         "durationSeconds": duration, "heartbeatSequence": duration + 1, "sample": samples[-1]}
    _require(canonical(snapshot) == canonical(expected_snapshot) and canonical(acknowledgement["finalSample"]) == canonical(snapshot), "acknowledged final sample differs from raw endpoint")
    expected_frame = {"type": "FINISHED", "recordId": session, "durationSeconds": duration,
                      "heartbeatSequence": duration + 1, "sampleBytes": len(data["final-sample.json"]), "sampleSha256": _digest(data["final-sample.json"])}
    _require(canonical(acknowledgement["frame"]) == canonical(expected_frame), "FINISHED final hash or sequence binding differs")
    _recheck(reference, root, context)
    return SoakSessionValidation(True, session_id=session, index_sha256=reference["sha256"], manifest_sha256=index["manifestSha256"],
        duration_seconds=duration, manifest=manifest, samples_sha256=_digest(canonical(samples).encode()))


def validate_soak_session_reference(reference, *, evidence_root, expected_bindings=None,
                                     expected_samples=None, replay_context=None):
    context = replay_context if replay_context is not None else SoakReplayContext()
    try:
        root = _root_key(evidence_root)
        _reference_parts(reference)
        key = (root, reference.get("locator"), reference.get("sha256"))
        result = context.sessions.get(key)
        if result is None:
            try:
                result = _validate_packet(reference, evidence_root, context)
            except (OSError, ValueError, TypeError, KeyError, OverflowError) as exc:
                result = SoakSessionValidation(False, (f"SOAK_SESSION_INVALID: {exc}",))
            context.sessions[key] = result
        if not result.valid:
            return result
        for name, value in (expected_bindings or {}).items():
            _require(name in result.manifest and canonical(result.manifest[name]) == canonical(value), f"session binding {name} differs or is missing")
        if expected_samples is not None:
            _require(_digest(canonical(expected_samples).encode()) == result.samples_sha256, "product sample series differs from raw session samples")
        claim = canonical({name: result.manifest.get(name) for name in ("candidateRootId", "candidateRootSha256", "installedTreeSha256", "platform", "architecture", "host", "workloadSha256", "durationSeconds")})
        previous = context.claims.get(result.session_id)
        _require(previous is None or previous == (result.index_sha256, claim), "conflicting or replayed session claim")
        context.claims[result.session_id] = (result.index_sha256, claim)
        return result
    except (OSError, ValueError, TypeError, KeyError, OverflowError) as exc:
        return SoakSessionValidation(False, (f"SOAK_SESSION_INVALID: {exc}",))
