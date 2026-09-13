"""Qualify a neural candidate on held-out material; never grant musical approval.

The command drives the production worker over every held-out item in a captured
qualification configuration and writes a dossier that separates three things:

* what this command actually measured (admission, vocabulary coverage, determinism,
  finite audio, runtime);
* what it refuses to judge (intelligibility, identity, musicality -- these need
  independent human listeners and are always UNRESOLVED here);
* what must never be inferred (release eligibility, product approval).

The arithmetic fixture graphs used by the transport checks can pass every automatic
criterion and still produce a dossier whose verdict is UNRESOLVED, because no number of
deterministic runs is evidence about a voice. A candidate that fails an automatic
criterion is reported as FAILED while the auditable model and measurements stay in the
dossier.
"""
import hashlib
import json
import math
import os
import stat
import struct
import subprocess
import tempfile
import time
from pathlib import Path

MAXIMUM_CONFIGURATION_BYTES = 8 * 1024 * 1024
MAXIMUM_ITEMS = 256
MAXIMUM_FRAMES = 48000
MINIMUM_FRAMES = 16
SAMPLE_RATE = 48000
AUTOMATIC_CRITERIA = ("bundle-admission", "response-binding", "vocabulary-coverage",
                      "determinism", "finite-audio", "runtime-budget")
HUMAN_CRITERIA = ("intelligibility", "identity", "musicality")
HUMAN_REASON = ("requires independent human listening; this command measures only "
                "machine-checkable behaviour")


def canonical(value) -> bytes:
    return (json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":")) + "\n").encode()


def read_bounded(path: Path, limit: int) -> bytes:
    if path.is_symlink() or not path.is_file():
        raise ValueError(path.name + " must be a regular non-symlink file")
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    with os.fdopen(os.open(path, flags), "rb") as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise ValueError(path.name + " is not a regular file")
        payload = stream.read(limit + 1)
    if len(payload) > limit:
        raise ValueError(path.name + " exceeds its bound of " + str(limit) + " bytes")
    return payload


def load_configuration(path: Path, expected_sha256: str) -> dict:
    payload = read_bounded(path, MAXIMUM_CONFIGURATION_BYTES)
    if hashlib.sha256(payload).hexdigest() != expected_sha256:
        raise ValueError("Qualification configuration differs from its captured SHA-256")
    return json.loads(payload.decode("utf-8"))


def text(value, maximum=256) -> bool:
    return (isinstance(value, str) and 0 < len(value.encode("utf-8")) <= maximum
            and not any(ord(character) < 32 or ord(character) == 127 for character in value))


def digest(value) -> bool:
    return isinstance(value, str) and len(value) == 64 and all(c in "0123456789abcdef" for c in value)


def load_vocabulary(directory: Path) -> tuple:
    """Token map and file digest from an admitted bundle vocabulary asset."""
    payload = read_bounded(directory / "vocabulary", 1024 * 1024)
    document = json.loads(payload.decode("utf-8"))
    if not isinstance(document, dict) or not text(document.get("formatId"), 128):
        raise ValueError("Bundle vocabulary format is invalid")
    tokens = document.get("tokens")
    if not isinstance(tokens, list) or not 1 <= len(tokens) <= 4096:
        raise ValueError("Bundle vocabulary token count is invalid")
    if not all(text(token) for token in tokens) or len(set(tokens)) != len(tokens):
        raise ValueError("Bundle vocabulary tokens must be unique text")
    return {token: index for index, token in enumerate(tokens)}, hashlib.sha256(payload).hexdigest()


def validate_configuration(config: dict) -> dict:
    if not isinstance(config, dict) or config.get("formatId") != "com.project-seam.candidate-qualification":
        raise ValueError("Qualification configuration format is unknown")
    if config.get("schemaVersion") != 1:
        raise ValueError("Qualification configuration schema is unsupported")
    bundle = config.get("bundle")
    if not isinstance(bundle, dict):
        raise ValueError("Qualification configuration needs a bundle record")
    if not text(bundle.get("directory")) or not text(bundle.get("modelId")) or not text(bundle.get("modelVersion")):
        raise ValueError("Bundle directory and identity must be text")
    if not digest(bundle.get("manifestSha256")):
        raise ValueError("Bundle manifest identity must be canonical SHA-256")
    maximum_bytes = bundle.get("maximumBundleBytes")
    if not isinstance(maximum_bytes, int) or not 1 <= maximum_bytes <= 512 * 1024 * 1024:
        raise ValueError("Bundle byte budget is invalid")
    held_out = config.get("heldOut")
    if not isinstance(held_out, list) or not 1 <= len(held_out) <= MAXIMUM_ITEMS:
        raise ValueError("Held-out items must number between 1 and " + str(MAXIMUM_ITEMS))
    seen = set()
    for item in held_out:
        if not isinstance(item, dict):
            raise ValueError("Held-out item must be an object")
        if not text(item.get("itemId"), 128) or not text(item.get("songId"), 256):
            raise ValueError("Held-out item needs an item and song identity")
        if item["itemId"] in seen:
            raise ValueError("Held-out item identity repeats")
        seen.add(item["itemId"])
        phones = item.get("phones")
        if not isinstance(phones, list) or not 1 <= len(phones) <= 64 or not all(text(phone, 64) for phone in phones):
            raise ValueError("Held-out item needs 1..64 phone symbols")
        frames = item.get("frameCount")
        if not isinstance(frames, int) or not MINIMUM_FRAMES <= frames <= MAXIMUM_FRAMES:
            raise ValueError("Held-out frame count must be between " + str(MINIMUM_FRAMES)
                             + " and " + str(MAXIMUM_FRAMES))
        if not isinstance(item.get("frequencyHz"), (int, float)) or not math.isfinite(item["frequencyHz"]):
            raise ValueError("Held-out frequency must be finite")
        if (not isinstance(item.get("gain"), (int, float)) or not math.isfinite(item["gain"])
                or not 0.0 < item["gain"] <= 1.0):
            raise ValueError("Held-out gain must be finite and within (0, 1]")
    repetitions = config.get("repetitions", 2)
    if not isinstance(repetitions, int) or not 1 <= repetitions <= 5:
        raise ValueError("Repetitions must be between 1 and 5")
    budgets = config.get("budgets", {})
    if not isinstance(budgets, dict):
        raise ValueError("Budgets must be an object")
    maximum_milliseconds = budgets.get("maximumMillisecondsPerItem")
    if maximum_milliseconds is not None and (not isinstance(maximum_milliseconds, int)
                                             or not 1 <= maximum_milliseconds <= 600000):
        raise ValueError("Per-item millisecond budget is invalid")
    return {"bundle": bundle, "heldOut": held_out, "repetitions": repetitions,
            "maximumMillisecondsPerItem": maximum_milliseconds}


def build_request(item: dict, vocabulary: dict, vocabulary_sha256: str, bundle: dict) -> bytes:
    """One frozen SNW1 request frame for a held-out item.

    The phones are spread evenly over the item's frames. That mapping is a
    qualification fixture, not an alignment: a real held-out item carries the alignment
    the label stage produced.
    """
    count = item["frameCount"]
    tokens = []
    phones = item["phones"]
    for index, phone in enumerate(phones):
        tokens.append({"tokenId": vocabulary[phone],
                       "startFrame": index * count // len(phones),
                       "endFrame": (index + 1) * count // len(phones)})
    metadata = {
        "kind": "seam-neural-request-v3",
        "requestId": item["requestId"],
        "modelId": bundle["modelId"],
        "modelVersion": bundle["modelVersion"],
        "modelContentHash": bundle["manifestSha256"],
        "bundleContentHash": bundle["manifestSha256"],
        "pronunciationHash": hashlib.sha256(item["itemId"].encode("utf-8")).hexdigest(),
        "sampleRate": SAMPLE_RATE,
        "channels": 1,
        "frameCount": count,
        "featureKind": "f0-dynamics-phonemes",
        "vocabularyHash": vocabulary_sha256,
        "vocabularySize": len(vocabulary),
        "phonemes": tokens,
    }
    header = json.dumps(metadata, sort_keys=True, separators=(",", ":")).encode("utf-8")
    payload = struct.pack("<" + str(count * 2) + "f",
                          *([float(item["frequencyHz"])] * count + [float(item["gain"])] * count))
    frame_header = struct.pack("<4sHBBIQ", b"SNW1", 1, 1, 0, len(header), len(payload))
    return frame_header + header + payload


def run_worker_process(worker: Path, arguments: list, request: bytes, timeout: float):
    completed = subprocess.run([str(worker), *arguments], input=request, capture_output=True, timeout=timeout)
    return completed.returncode, completed.stdout, completed.stderr


def decode_response(stdout: bytes, request: bytes, item: dict) -> tuple:
    if len(stdout) < 20:
        raise ValueError("response frame is shorter than its header")
    magic, version, kind, reserved, size, payload_size = struct.unpack("<4sHBBIQ", stdout[:20])
    if (magic, version, kind, reserved) != (b"SNW1", 1, 2, 0):
        raise ValueError("response frame header is not a worker response")
    if len(stdout) != 20 + size + payload_size:
        raise ValueError("response frame length disagrees with its header")
    reply = json.loads(stdout[20:20 + size].decode("utf-8"))
    if reply.get("kind") != "seam-neural-response-v3":
        raise ValueError("response metadata kind is unknown")
    if reply.get("requestId") != item["requestId"]:
        raise ValueError("response belongs to a different request")
    if reply.get("requestContentHash") != hashlib.sha256(request).hexdigest():
        raise ValueError("response is not bound to the exact request bytes")
    if reply.get("frameCount") != item["frameCount"] or reply.get("sampleRate") != SAMPLE_RATE:
        raise ValueError("response frame count or sample rate differs from the request")
    if reply.get("channels") != 1:
        raise ValueError("response channel count differs from the request")
    if payload_size != item["frameCount"] * 4:
        raise ValueError("response audio payload is not one mono float per frame")
    pcm = stdout[20 + size:]
    if len(pcm) != payload_size:
        raise ValueError("response audio payload length differs from its header")
    return reply, pcm


def evaluate_item(item: dict, runs: list, vocabulary: dict, maximum_milliseconds,
                  repetitions: int = 2) -> dict:
    """One held-out item's record. Never approves musical content.

    Every automatic criterion keeps its own status, so a candidate that is
    deterministic but slow is reported that way instead of failing wholesale.
    """
    criteria = {criterion: "PASS" for criterion in AUTOMATIC_CRITERIA}
    if maximum_milliseconds is None:
        criteria["runtime-budget"] = "UNRESOLVED"
    if repetitions < 2:
        criteria["determinism"] = "UNRESOLVED"
    result = {"itemId": item["itemId"], "songId": item["songId"], "status": "PASS",
              "detail": "measured", "frames": item["frameCount"], "milliseconds": [],
              "criteria": criteria}
    def failed(criterion: str, detail: str) -> dict:
        criteria[criterion] = "FAIL"
        result["status"] = "FAIL"
        result["detail"] = detail
        return result

    missing = [phone for phone in item["phones"] if phone not in vocabulary]
    if missing:
        return failed("vocabulary-coverage",
                      "phones absent from the admitted vocabulary: " + ", ".join(sorted(missing)))
    if not runs:
        return failed("bundle-admission", "no worker run was recorded")
    frames = []
    for run in runs:
        result["milliseconds"].append(round(run["milliseconds"], 3))
        if run["returncode"] != 0:
            return failed("bundle-admission",
                          "worker exit status " + str(run["returncode"]) + ": "
                          + run["stderr"][:160].decode("utf-8", "replace").strip())
        try:
            reply, pcm = decode_response(run["stdout"], run["request"], item)
        except ValueError as error:
            return failed("response-binding", str(error))
        frames.append(pcm)
        samples = struct.unpack("<" + str(item["frameCount"]) + "f", pcm)
        if not all(math.isfinite(sample) and -1.0 <= sample <= 1.0 for sample in samples):
            return failed("finite-audio", "audio contains nonfinite or out-of-range samples")
        if not any(sample != 0.0 for sample in samples):
            return failed("finite-audio", "audio is digital silence for every frame")
        result["audioSha256"] = hashlib.sha256(pcm).hexdigest()
        result["modelContentHash"] = reply.get("modelContentHash")
        result["backendId"] = reply.get("backendId")
    if criteria["determinism"] == "PASS" and len(set(frames)) != 1:
        return failed("determinism", "repeated identical requests produced different audio")
    if maximum_milliseconds is not None and max(result["milliseconds"]) > maximum_milliseconds:
        return failed("runtime-budget",
                      "slowest run " + str(max(result["milliseconds"])) + " ms exceeds the declared "
                      + str(maximum_milliseconds) + " ms budget")
    return result


def _criterion_status(items: list, criterion: str) -> str:
    values = {item.get("criteria", {}).get(criterion, "UNRESOLVED") for item in items}
    if "FAIL" in values:
        return "FAIL"
    if "PASS" in values:
        return "PASS"
    return "UNRESOLVED"


def aggregate(items: list, bundle: dict, worker: dict, repetitions: int,
              maximum_milliseconds) -> dict:
    criteria = []
    for criterion in AUTOMATIC_CRITERIA:
        if criterion == "runtime-budget" and maximum_milliseconds is None:
            criteria.append({"id": criterion, "status": "UNRESOLVED",
                             "detail": "no per-item budget was declared, so no runtime claim is made"})
            continue
        criteria.append({"id": criterion, "status": _criterion_status(items, criterion),
                         "detail": "measured over " + str(len(items)) + " held-out items"})
    for criterion in HUMAN_CRITERIA:
        criteria.append({"id": criterion, "status": "UNRESOLVED", "detail": HUMAN_REASON})
    failed = [entry for entry in criteria if entry["status"] == "FAIL"]
    return {
        "formatId": "com.project-seam.neural-candidate-qualification",
        "schemaVersion": 1,
        "bundle": dict(bundle),
        "worker": dict(worker),
        "repetitions": repetitions,
        "items": items,
        "criteria": criteria,
        "verdict": "FAILED" if failed else "UNRESOLVED",
        "failedCriteria": sorted(entry["id"] for entry in failed),
        "musicalJudgement": "UNRESOLVED",
        "releaseEligible": False,
        "approval": None,
        "notice": ("This dossier records machine-checkable measurements over held-out items. It is "
                   "not musical, identity or release approval, and an UNRESOLVED verdict is the "
                   "expected result until independent listeners report."),
    }


def qualify(configuration: Path, expected_sha256: str, worker: Path, output: Path,
            runner=run_worker_process) -> tuple:
    config = validate_configuration(load_configuration(configuration, expected_sha256))
    bundle = config["bundle"]
    directory = Path(bundle["directory"])
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError("Bundle directory must be a real directory")
    manifest = read_bounded(directory / "manifest.json", 8 * 1024 * 1024)
    if hashlib.sha256(manifest).hexdigest() != bundle["manifestSha256"]:
        raise ValueError("Bundle manifest differs from the captured SHA-256")
    vocabulary, vocabulary_sha256 = load_vocabulary(directory)
    if output.exists() or output.is_symlink():
        raise ValueError("Qualification dossier must be new")
    worker_payload = read_bounded(worker, 512 * 1024 * 1024)
    launch = ["--seam-neural-worker-v2", str(directory), bundle["modelId"], bundle["modelVersion"],
              bundle["manifestSha256"], str(bundle["maximumBundleBytes"])]
    items = []
    for index, item in enumerate(config["heldOut"]):
        prepared = dict(item)
        prepared["requestId"] = index + 1
        runs = []
        missing = [phone for phone in item["phones"] if phone not in vocabulary]
        if not missing:
            request = build_request(prepared, vocabulary, vocabulary_sha256, bundle)
            for _ in range(config["repetitions"]):
                started = time.perf_counter()
                returncode, stdout, stderr = runner(worker, launch, request, timeout=120.0)
                runs.append({"returncode": returncode, "stdout": stdout, "stderr": stderr,
                             "request": request,
                             "milliseconds": (time.perf_counter() - started) * 1000.0})
        items.append(evaluate_item(prepared, runs, vocabulary,
                                   config["maximumMillisecondsPerItem"], config["repetitions"]))
    dossier = aggregate(items, bundle,
                        {"path": str(worker), "sha256": hashlib.sha256(worker_payload).hexdigest()},
                        config["repetitions"], config["maximumMillisecondsPerItem"])
    return dossier, (4 if dossier["verdict"] == "FAILED" else 0)


def publish_new(output: Path, result: dict) -> None:
    encoded = canonical(result)
    descriptor, temporary = tempfile.mkstemp(prefix=".seam-qualification-", dir=output.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.link(temporary, output)
    finally:
        os.unlink(temporary)


def qualify_command(configuration: Path, expected_sha256: str, worker: Path, output: Path) -> int:
    dossier, exit_code = qualify(configuration, expected_sha256, worker, output)
    publish_new(output, dossier)
    return exit_code
