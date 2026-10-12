"""Prepare unassigned PCM16 cuts from hash-bound SEAM sustained-note analyses."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
from pathlib import Path, PurePosixPath
import re
import statistics
import struct
import sys
import wave

WINDOW = 2048
HOP = 256
CONFIDENCE = 0.90
TOLERANCE_CENTS = 40.0
MINIMUM_SECONDS = 0.20
MAXIMUM_AUDIO_BYTES = 64 * 1024 * 1024
MAXIMUM_JSON_BYTES = 8 * 1024 * 1024


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def strict_json(data: bytes) -> dict:
    def invalid_constant(value: str) -> None:
        raise ValueError(f"Non-finite JSON constant: {value}")

    def unique_object(pairs: list[tuple[str, object]]) -> dict:
        result = {}
        for key, value in pairs:
            require(key not in result, f"Duplicate JSON key: {key}")
            result[key] = value
        return result

    result = json.loads(data, parse_constant=invalid_constant, object_pairs_hook=unique_object)
    require(isinstance(result, dict), "Expected a JSON object")
    return result


def bound_file(root: Path, record: dict, limit: int) -> tuple[Path, bytes]:
    require(isinstance(record, dict), "Expected a bound file record")
    name = record.get("path", "")
    require(isinstance(name, str), "Bound path must be a string")
    relative = PurePosixPath(name)
    require(bool(name) and not relative.is_absolute() and ".." not in relative.parts,
            "Bound paths must remain below the plan directory")
    path = root.joinpath(*relative.parts)
    current = path
    while current != root:
        require(not current.is_symlink(), "Symlink input is not supported")
        current = current.parent
    require(path.resolve().is_relative_to(root.resolve()), "Bound path escapes the plan directory")
    require(path.is_file() and path.stat().st_size <= limit, "Missing or oversized bound input")
    data = path.read_bytes()
    require(len(data) <= limit, "Input grew beyond its byte limit")
    expected = record.get("sha256", "")
    require(isinstance(expected, str) and re.fullmatch(r"[0-9a-f]{64}", expected) is not None,
            "Expected a lowercase SHA-256 binding")
    require(digest(data) == expected, f"SHA-256 mismatch: {name}")
    return path, data


def finite_number(value: object) -> bool:
    return type(value) in (int, float) and math.isfinite(value)


def proposal_rows(source_path: Path, audio: bytes, analysis: dict) -> tuple[list[dict], bytes, int]:
    with wave.open(io.BytesIO(audio), "rb") as wav:
        require(wav.getnchannels() == 1 and wav.getsampwidth() == 2 and wav.getcomptype() == "NONE",
                "This preparation route supports mono PCM16 only")
        rate = wav.getframerate()
        frames = wav.getnframes()
        pcm = wav.readframes(frames)
    require(8000 <= rate <= 192000 and len(pcm) == frames * 2, "Invalid or truncated PCM extent")
    require(all(type(analysis.get(key)) is int for key in ("sampleRate", "channels", "frames")) and
            analysis.get("sampleRate") == rate and analysis.get("channels") == 1 and
            analysis.get("frames") == frames, "Analysis/audio extent or format mismatch")
    require(isinstance(analysis.get("source"), str) and
            Path(analysis["source"]).resolve() == source_path.resolve(), "Analysis source identity mismatch")
    measured = analysis.get("pitchFrames")
    require(isinstance(measured, list), "Missing SEAM pitch frames")
    require(len(measured) == max(0, (frames - WINDOW) // HOP + 1), "Incomplete analysis hop grid")
    runs: list[list[dict]] = []
    run: list[dict] = []
    held_pitch = None
    for index, frame in enumerate(measured):
        require(isinstance(frame, dict) and type(frame.get("sourceFrame")) is int and
                frame["sourceFrame"] == index * HOP and frame["sourceFrame"] + WINDOW <= frames,
                "Invalid analysis frame order or extent")
        f0, confidence, voiced = frame.get("f0Hz"), frame.get("confidence"), frame.get("voiced")
        require(finite_number(f0) and f0 >= 0 and finite_number(confidence) and
                0 <= confidence <= 1 and type(voiced) is bool, "Invalid pitch measurement")
        require(not voiced or f0 > 0, "Voiced pitch must be positive")
        midi = 69 + 12 * math.log2(f0 / 440) if voiced else None
        pitch = round(midi) if midi is not None else None
        accepted = (pitch is not None and 0 <= pitch <= 127 and confidence >= CONFIDENCE and
                    abs(midi - pitch) * 100 <= TOLERANCE_CENTS)
        key = pitch if accepted else None
        if run and key != held_pitch:
            runs.append(run)
            run = []
        if key is not None:
            run.append(frame)
        held_pitch = key
    if run:
        runs.append(run)

    # Drop one full analysis window from each edge of the measured support union.
    # Rejected frames are never bridged, even when their neighbouring runs share a pitch.
    best: dict[int, dict] = {}
    for run in runs:
        start, end = run[0]["sourceFrame"] + WINDOW, run[-1]["sourceFrame"]
        if end - start < max(math.ceil(MINIMUM_SECONDS * rate), WINDOW):
            continue
        inside = [f for f in run if start <= f["sourceFrame"] and f["sourceFrame"] + WINDOW <= end]
        require(bool(inside), "A proposed cut needs complete interior analysis windows")
        median_hz = statistics.median(f["f0Hz"] for f in inside)
        midi = 69 + 12 * math.log2(median_hz / 440)
        pitch = round(midi)
        candidate = {"startFrame": start, "endFrameExclusive": end,
                     "durationSeconds": (end - start) / rate, "sampleRate": rate,
                     "measuredMedianF0Hz": median_hz, "measuredMidi": midi,
                     "observedNearestMidi": pitch, "interiorAnalysisFrames": len(inside),
                     "minimumConfidence": min(f["confidence"] for f in inside),
                     "maximumDeviationCents": max(abs(69 + 12 * math.log2(f["f0Hz"] / 440) - pitch) * 100
                                                  for f in inside)}
        previous = best.get(pitch)
        if previous is None or end - start > previous["endFrameExclusive"] - previous["startFrame"]:
            best[pitch] = candidate
    return [best[key] for key in sorted(best)], pcm, rate


def prepare(plan_path: Path, output: Path) -> dict:
    require(not output.exists() and not output.is_symlink(), "Output already exists; no overwrite")
    require(plan_path.is_file() and plan_path.stat().st_size <= MAXIMUM_JSON_BYTES, "Missing or oversized plan")
    raw_plan = plan_path.read_bytes()
    plan = strict_json(raw_plan)
    require(type(plan.get("schemaVersion")) is int and plan.get("schemaVersion") == 1 and
            plan.get("analysisContract") == {"command": "seam_voicebank_cli analyze",
                                              "windowFrames": WINDOW, "hopFrames": HOP,
                                              "coverage": "CompleteWindows"}, "Unsupported analysis contract")
    sources = plan.get("sources")
    require(isinstance(sources, list) and 1 <= len(sources) <= 32, "Expected 1 to 32 source records")
    root = plan_path.resolve().parent
    license_record = plan.get("license")
    require(isinstance(license_record, dict) and isinstance(license_record.get("attribution"), str) and
            bool(license_record["attribution"]) and isinstance(license_record.get("sourceUrl"), str),
            "License provenance and attribution are required")
    require(bool(license_record["sourceUrl"].strip()), "License sourceUrl must not be empty")
    _, legal_text = bound_file(root, license_record, MAXIMUM_JSON_BYTES)
    prepared: list[tuple[dict, bytes, int, dict]] = []
    ids: set[str] = set()
    audio_hashes: set[str] = set()
    for source in sources:
        require(isinstance(source, dict), "Invalid source record")
        identifier = source.get("id", "")
        require(isinstance(identifier, str) and re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,79}", identifier) is not None
                and identifier not in ids, "Invalid or duplicate source id")
        ids.add(identifier)
        require(source.get("sourceVowel") in ("a", "e", "i", "o", "u") and
                source.get("sourceTechnique") == "straight", "This cohort requires straight source-vowel labels")
        path, audio = bound_file(root, source["audio"], MAXIMUM_AUDIO_BYTES)
        audio_sha = digest(audio)
        require(audio_sha not in audio_hashes, "Duplicate source audio hash")
        audio_hashes.add(audio_sha)
        _, measured = bound_file(root, source["analysis"], MAXIMUM_JSON_BYTES)
        rows, pcm, rate = proposal_rows(path, audio, strict_json(measured))
        for row in rows:
            interval = pcm[row["startFrame"] * 2:row["endFrameExclusive"] * 2]
            require(any(interval), "Proposed cut is silent")
            prepared.append((source, interval, rate, row))
    require(bool(prepared), "No intervals satisfy the proposal thresholds")

    # Validate every input before creating an exclusively owned output directory.
    # An I/O failure leaves an explicit incomplete marker; foreign paths are never removed.
    output.mkdir()
    marker = output / "INCOMPLETE"
    marker.write_text("Preparation has not completed. Do not import or approve this directory.\n")
    (output / "license-source.txt").write_bytes(legal_text)
    (output / "intake-plan.json").write_bytes(raw_plan)
    cuts = []
    for source, pcm, rate, row in prepared:
        name = f"{source['id']}_midi{row['observedNearestMidi']}.wav"
        target = output / name
        with wave.open(str(target), "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(rate)
            wav.writeframes(pcm)
        peak = max(abs(v[0]) for v in struct.iter_unpack("<h", pcm)) / 32768
        cuts.append({**row, "id": target.stem, "derivedPath": name,
                     "derivedSha256": digest(target.read_bytes()), "pcmSha256": digest(pcm),
                     "decodedPeak": peak, "sourceId": source["id"],
                     "sourceAudio": source["audio"], "sourceAnalysis": source["analysis"],
                     "sourceVowel": source["sourceVowel"], "sourceTechnique": source["sourceTechnique"],
                     "assignment": "NOT_ASSIGNED", "pitchReview": "NOT_REVIEWED",
                     "phoneticReview": "NOT_REVIEWED", "listeningReview": "NOT_REVIEWED"})
    result = {"schemaVersion": 1, "documentKind": "UnassignedVocalCutProposals",
              "status": "UNAPPROVED", "planSha256": digest(raw_plan),
              "license": license_record, "retainedLicensePath": "license-source.txt",
              "changeNotice": "Original PCM intervals copied into new RIFF/WAV containers. Ancillary chunks removed; "
                              "no resampling, normalization, pitch shift or phoneme assignment.",
              "thresholds": {"confidence": CONFIDENCE, "maximumDeviationCents": TOLERANCE_CENTS,
                             "minimumCutSeconds": MINIMUM_SECONDS, "edgeGuardFrames": WINDOW},
              "producerRegistration": "NOT_REGISTERED", "performerRights": "NOT_ESTABLISHED",
              "rightsClearance": "NOT_APPROVED", "bankAdmission": "NOT_APPROVED",
              "cuts": cuts}
    (output / "cuts.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
    marker.unlink()
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        result = prepare(args.plan, args.output)
    except (ValueError, OSError, KeyError, TypeError, EOFError, ArithmeticError, RecursionError, wave.Error) as error:
        print(f"Preparation refused: {error}", file=sys.stderr)
        return 2
    print(f"Prepared {len(result['cuts'])} unassigned, unapproved cuts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
