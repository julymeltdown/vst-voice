"""Independent verification of a producer workspace's generation request registry (U21).

Mirrors libs/seam-voicebank-production/src/repository_generation.cpp: the same canonical bytes, the
same admission rules and the same history proofs. A request record is queue evidence and a terminal
record says what happened to that request; neither is a review, an approval or a release claim.
"""
from __future__ import annotations

import hashlib
import json
import re
import stat
from pathlib import Path
from typing import Any

REQUEST_FORMAT = "com.project-seam.generation-request"
TERMINAL_FORMAT = "com.project-seam.generation-request-terminal"
OUTCOMES = ("COMPLETED", "STALE", "BUDGET_EXHAUSTED")
UNREVIEWED = ("MARKER_REVIEW", "REJECTED")
MAX_REQUEST_BYTES = 16 * 1024 * 1024
MAX_TERMINAL_BYTES = 64 * 1024
MAX_PROJECT_BYTES = 64 * 1024 * 1024
MAX_JOURNAL_BYTES = 1024 * 1024
MAX_ENTRIES = 65536
REQUEST_KEYS = {"formatId", "schemaVersion", "requestId", "definitionLocator", "expectedGeneration",
                "expectedProjectSha256", "language", "recipe", "budget", "jobs", "submittedBy", "submittedAtUtc",
                "releaseEligible"}
RECIPE_KEYS = {"contentHash", "id", "version"}
BUDGET_KEYS = {"batchMaximumFrames", "batchMaximumJobs", "maximumBytes", "maximumFrames", "maximumJobs"}
JOB_KEYS = {"batchIndex", "coverageKey", "frameCount", "jobId", "pitchLayer", "style", "takeId"}
TERMINAL_KEYS = {"formatId", "schemaVersion", "requestId", "requestSha256", "outcome", "observedGeneration",
                 "observedProjectSha256", "completedBatches", "retainedBytes", "detail", "recordedBy",
                 "recordedAtUtc", "releaseEligible"}
_HEX64 = re.compile(r"[0-9a-f]{64}")
_UTC = re.compile(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z")


class RegistryError(ValueError):
    """A registry record that the producer history does not support."""


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise RegistryError(f"duplicate JSON field: {key}")
        result[key] = value
    return result


def _constant(name: str) -> Any:
    raise RegistryError(f"non-finite JSON number: {name}")


def _load(data: bytes) -> Any:
    return json.loads(data.decode("utf-8"), object_pairs_hook=_pairs, parse_constant=_constant)


def encode_record(value: dict[str, Any]) -> bytes:
    """The exact bytes the C++ writer produces: sorted keys, two-space indent, trailing newline."""
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n").encode("utf-8")


def _read(path: Path, limit: int) -> bytes:
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode):
        raise RegistryError(f"{path.name} is not a regular file")
    if info.st_size > limit:
        raise RegistryError(f"{path.name} exceeds its byte limit")
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise RegistryError(f"{path.name} exceeds its byte limit")
    return data


def _integer(value: Any) -> bool:
    return type(value) is int


def _hex(value: Any) -> bool:
    return isinstance(value, str) and _HEX64.fullmatch(value) is not None


def _text(value: Any, maximum: int, allow_empty: bool = False) -> bool:
    if not isinstance(value, str):
        return False
    size = len(value.encode("utf-8"))
    if (size == 0 and not allow_empty) or size > maximum:
        return False
    return not any(ord(character) < 0x20 or ord(character) == 0x7F for character in value)


def _utc(value: Any) -> bool:
    return isinstance(value, str) and _UTC.fullmatch(value) is not None


def _object(value: Any, keys: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != keys:
        raise RegistryError(f"{label} is malformed")
    return value


def _request_shape_error(request: dict[str, Any]) -> str | None:
    budget = request["budget"]
    recipe = request["recipe"]
    if not _hex(request["requestId"]) or not _text(request["definitionLocator"], 4096, True):
        return "identity is invalid"
    if request["expectedGeneration"] < 1 or not _hex(request["expectedProjectSha256"]):
        return "expected producer state is invalid"
    if not (_text(request["language"], 64) and _text(recipe["id"], 256) and _text(recipe["version"], 256)
            and _hex(recipe["contentHash"])):
        return "language or recipe identity is invalid"
    if not (1 <= budget["maximumJobs"] <= 16384 and 1 <= budget["maximumFrames"] <= 1 << 32
            and 1 <= budget["maximumBytes"] <= 1 << 40 and 1 <= budget["batchMaximumJobs"] <= 64
            and 1 <= budget["batchMaximumFrames"] <= 32 * 1024 * 1024):
        return "budget is outside supported limits"
    if not _text(request["submittedBy"], 128) or not _utc(request["submittedAtUtc"]):
        return "submitter or time is invalid"
    jobs = request["jobs"]
    if not jobs or len(jobs) > budget["maximumJobs"]:
        return "job count is outside its budget"
    job_ids: set[str] = set()
    take_ids: set[str] = set()
    total = batch = batch_jobs = batch_frames = 0
    for job in jobs:
        if not (_text(job["jobId"], 128) and _text(job["takeId"], 128) and _text(job["style"], 128)
                and _text(job["coverageKey"], 256) and 0 <= job["pitchLayer"] <= 127
                and 1 <= job["frameCount"] <= budget["batchMaximumFrames"]):
            return "job identity, pitch or duration is invalid"
        if job["jobId"] in job_ids or job["takeId"] in take_ids:
            return "repeats a job or take ID"
        job_ids.add(job["jobId"])
        take_ids.add(job["takeId"])
        if batch_jobs > 0 and job["batchIndex"] == batch + 1:
            batch, batch_jobs, batch_frames = job["batchIndex"], 0, 0
        if job["batchIndex"] != batch:
            return "batches are not contiguous from zero"
        batch_jobs += 1
        if (batch_jobs > budget["batchMaximumJobs"] or job["frameCount"] > budget["batchMaximumFrames"] - batch_frames
                or job["frameCount"] > budget["maximumFrames"] - total):
            return "exceeds its batch or aggregate frame budget"
        batch_frames += job["frameCount"]
        total += job["frameCount"]
    return None


def decode_request(data: bytes) -> dict[str, Any]:
    request = _object(_load(data), REQUEST_KEYS, "generation request")
    _object(request["recipe"], RECIPE_KEYS, "generation request recipe")
    _object(request["budget"], BUDGET_KEYS, "generation request budget")
    if (request["formatId"] != REQUEST_FORMAT or request["schemaVersion"] != 1 or not _integer(request["schemaVersion"])
            or request["releaseEligible"] is not False or not isinstance(request["jobs"], list) or len(request["jobs"]) > 16384):
        raise RegistryError("generation request record is malformed")
    strings = ("requestId", "definitionLocator", "expectedProjectSha256", "language", "submittedBy", "submittedAtUtc")
    if (not all(isinstance(request[key], str) for key in strings) or not _integer(request["expectedGeneration"])
            or not all(isinstance(request["recipe"][key], str) for key in RECIPE_KEYS)
            or not all(_integer(request["budget"][key]) for key in BUDGET_KEYS)):
        raise RegistryError("generation request record is malformed")
    for job in request["jobs"]:
        _object(job, JOB_KEYS, "generation request job")
        if (not all(isinstance(job[key], str) for key in ("jobId", "takeId", "style", "coverageKey"))
                or not all(_integer(job[key]) for key in ("pitchLayer", "frameCount", "batchIndex"))):
            raise RegistryError("generation request job is malformed")
    error = _request_shape_error(request)
    if error:
        raise RegistryError(f"generation request {error}")
    if encode_record(request) != data:
        raise RegistryError("generation request record is not canonical")
    return request


def decode_terminal(data: bytes, request_id: str, request_sha256: str) -> dict[str, Any]:
    terminal = _object(_load(data), TERMINAL_KEYS, "generation request terminal")
    strings = ("formatId", "requestId", "requestSha256", "outcome", "observedProjectSha256", "detail", "recordedBy",
               "recordedAtUtc")
    if (not all(isinstance(terminal[key], str) for key in strings)
            or not all(_integer(terminal[key]) for key in ("schemaVersion", "observedGeneration", "completedBatches", "retainedBytes"))
            or terminal["formatId"] != TERMINAL_FORMAT or terminal["schemaVersion"] != 1 or terminal["releaseEligible"] is not False
            or terminal["outcome"] not in OUTCOMES or terminal["requestId"] != request_id
            or terminal["requestSha256"] != request_sha256):
        raise RegistryError("generation request terminal record is malformed or bound to another request")
    if (terminal["observedGeneration"] < 1 or not _hex(terminal["observedProjectSha256"]) or terminal["completedBatches"] < 0
            or terminal["retainedBytes"] < 0 or not _text(terminal["detail"], 1024, True) or not _text(terminal["recordedBy"], 128)
            or not _utc(terminal["recordedAtUtc"]) or not _hex(request_id) or not _hex(request_sha256)):
        raise RegistryError("generation request terminal outcome is invalid")
    if encode_record(terminal) != data:
        raise RegistryError("generation request terminal record is not canonical")
    return terminal


def _generation(workspace: Path, generation: int, digest: str) -> dict[str, Any]:
    data = _read(workspace / "generations" / f"{generation:020}.json", MAX_PROJECT_BYTES)
    if hashlib.sha256(data).hexdigest() != digest:
        raise RegistryError(f"producer generation {generation} does not have the recorded hash")
    project = _load(data)
    if not isinstance(project, dict) or project.get("lastDurableGeneration") != generation:
        raise RegistryError(f"producer generation {generation} is not durable history")
    return project


def _journaled(workspace: Path, generation: int) -> tuple[str, dict[str, Any]]:
    journal = _load(_read(workspace / "journal" / f"{generation:020}.json", MAX_JOURNAL_BYTES))
    if (not isinstance(journal, dict) or journal.get("generation") != generation or not isinstance(journal.get("action"), str)
            or not isinstance(journal.get("projectSha256"), str)):
        raise RegistryError("generation request history is not in the producer journal")
    return journal["action"], _generation(workspace, generation, journal["projectSha256"])


def _registered(project: dict[str, Any], operator_id: str) -> bool:
    return any(isinstance(row, dict) and row.get("operatorId") == operator_id for row in project.get("operators") or [])


def _jobs_error(project: dict[str, Any], request: dict[str, Any]) -> str | None:
    if project.get("language", "") != request["language"]:
        return "language differs from the producer"
    if not _registered(project, request["submittedBy"]):
        return "submitter is not a registered operator"
    assignments = [row for row in project.get("unitAssignments") or [] if isinstance(row, dict)]
    takes = [row for row in project.get("takes") or [] if isinstance(row, dict)]
    for job in request["jobs"]:
        rows = [row for row in assignments if row.get("plannedTakeId") == job["takeId"]]
        if len(rows) != 1:
            return f"take {job['takeId']} does not identify exactly one assignment"
        row = rows[0]
        if row.get("style", "") != job["style"] or row.get("coverageKey") != job["coverageKey"] or row.get("pitchLayer") != job["pitchLayer"]:
            return f"take {job['takeId']} differs from its assignment identity"
        if any(take.get("takeId") == job["takeId"] for take in takes):
            return f"take {job['takeId']} already exists; a request cannot duplicate or replace it"
    return None


def _collected_error(workspace: Path, request: dict[str, Any], expected: dict[str, Any], count: int) -> str | None:
    previous = expected
    for batch in range(count):
        action, project = _journaled(workspace, request["expectedGeneration"] + batch + 1)
        if action != "import-generated-batch":
            return f"batch {batch} is not a generated batch collection"
        old = {take.get("takeId") for take in previous.get("takes") or [] if isinstance(take, dict)}
        takes = {take.get("takeId"): take for take in project.get("takes") or [] if isinstance(take, dict)}
        introduced = {take_id for take_id in takes if take_id not in old}
        requested: set[str] = set()
        for job in request["jobs"]:
            if job["batchIndex"] != batch:
                continue
            requested.add(job["takeId"])
            take = takes.get(job["takeId"])
            if (take is None or take.get("style", "") != job["style"] or take.get("coverageKey") != job["coverageKey"]
                    or take.get("pitchLayer") != job["pitchLayer"] or take.get("state") not in UNREVIEWED):
                return f"take {job['takeId']} was not collected as unreviewed material of its request"
        if introduced != requested:
            return f"batch {batch} does not introduce exactly its requested takes"
        previous = project
    return None


def _terminal_error(workspace: Path, request: dict[str, Any], expected: dict[str, Any], terminal: dict[str, Any]) -> str | None:
    batches = request["jobs"][-1]["batchIndex"] + 1
    completed = terminal["completedBatches"]
    if completed > batches or terminal["observedGeneration"] < request["expectedGeneration"] + completed:
        return "terminal outcome claims more collection than its history holds"
    observed = _generation(workspace, terminal["observedGeneration"], terminal["observedProjectSha256"])
    if not _registered(observed, terminal["recordedBy"]):
        return "terminal outcome was not recorded by a registered operator"
    error = _collected_error(workspace, request, expected, completed)
    if error:
        return error
    outcome = terminal["outcome"]
    if outcome == "COMPLETED" and (completed != batches or terminal["retainedBytes"] != 0
                                   or terminal["observedGeneration"] != request["expectedGeneration"] + batches):
        return "a completed request must end at its final batch collection"
    if outcome == "STALE" and (completed >= batches or terminal["retainedBytes"] != 0
                               or terminal["observedGeneration"] <= request["expectedGeneration"] + completed):
        return "a stale request must observe a producer beyond its own collections"
    if outcome == "BUDGET_EXHAUSTED" and (completed >= batches or terminal["retainedBytes"] <= request["budget"]["maximumBytes"]):
        return "an exhausted request must retain more than its admitted bytes"
    return None


def _unsafe(path: Path) -> bool:
    return path.is_symlink() or (path.exists() and not path.is_file())


def _inspect(workspace: Path, directory: Path, request_id: str) -> dict[str, Any] | None:
    if directory.is_symlink() or not directory.is_dir():
        raise RegistryError("request directory is unsafe")
    request_path, terminal_path = directory / "request.json", directory / "terminal.json"
    if _unsafe(request_path) or _unsafe(terminal_path):
        raise RegistryError("request record is unsafe")
    if not request_path.exists():
        return None  # an interrupted submission, not a request
    data = _read(request_path, MAX_REQUEST_BYTES)
    request = decode_request(data)
    if request["requestId"] != request_id:
        raise RegistryError("request is filed under another ID")
    expected = _generation(workspace, request["expectedGeneration"], request["expectedProjectSha256"])
    error = _jobs_error(expected, request)
    if error:
        raise RegistryError(error)
    request_sha256 = hashlib.sha256(data).hexdigest()
    terminal = None
    if terminal_path.exists():
        terminal = decode_terminal(_read(terminal_path, MAX_TERMINAL_BYTES), request_id, request_sha256)
        error = _terminal_error(workspace, request, expected, terminal)
        if error:
            raise RegistryError(error)
    return {
        "requestId": request_id, "requestSha256": request_sha256,
        "state": terminal["outcome"] if terminal else "SUBMITTED",
        "expectedGeneration": request["expectedGeneration"], "expectedProjectSha256": request["expectedProjectSha256"],
        "jobs": len(request["jobs"]), "batchCount": request["jobs"][-1]["batchIndex"] + 1,
        "submittedBy": request["submittedBy"], "submittedAtUtc": request["submittedAtUtc"],
        "definitionLocator": request["definitionLocator"],
        "terminal": None if terminal is None else {key: terminal[key] for key in (
            "outcome", "observedGeneration", "observedProjectSha256", "completedBatches", "retainedBytes", "recordedBy",
            "recordedAtUtc", "detail")},
    }


def inspect_generation_requests(workspace: Path) -> tuple[list[dict[str, Any]], list[str]]:
    """Every retained request in submission order, and every record the history does not support."""
    registry = workspace / "generation-requests"
    if registry.is_symlink() or (registry.exists() and not registry.is_dir()):
        return [], ["generation request registry is unsafe"]
    if not registry.exists():
        return [], []
    names: list[str] = []
    errors: list[str] = []
    for entry in registry.iterdir():
        if len(names) >= MAX_ENTRIES:
            return [], ["generation request registry has too many entries"]
        if entry.name == ".registry.lock":
            continue
        if _HEX64.fullmatch(entry.name) is None:
            errors.append(f"generation request registry holds an unrecognized entry: {entry.name}")
            continue
        names.append(entry.name)
    summaries: list[dict[str, Any]] = []
    for name in sorted(names):
        try:
            summary = _inspect(workspace, registry / name, name)
        except (RegistryError, OSError, UnicodeError, ValueError, TypeError, KeyError, RecursionError) as exc:
            errors.append(f"generation request {name}: {exc}")
            continue
        if summary is not None:
            summaries.append(summary)
    summaries.sort(key=lambda summary: (summary["submittedAtUtc"], summary["requestId"]))
    return summaries, errors


def generation_request_errors(workspace: Path) -> list[str]:
    return inspect_generation_requests(workspace)[1]
