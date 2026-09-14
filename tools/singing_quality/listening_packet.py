"""Retain a bounded, unreviewed listening packet using the existing pilot executable.

This orchestrates production exports; it does not implement DSP, approve a singer,
or infer intelligibility from the measurements. Existing output is never replaced.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import platform
import shutil
import struct
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path


CASES = [
    ("vowels", []),
    ("articulation", ["articulation"]),
    ("stops", ["stops"]),
    ("affricates", ["affricates"]),
    ("glides", ["glides"]),
    ("nasals", ["nasals"]),
    ("melisma", ["boundaries"]),
    ("events", ["events"]),
    ("range", ["phrase", "ま:60:960", "み:66:960", "む:72:960", "め:66:960", "も:60:960"]),
    ("rhythm", ["phrase", "ば:60:960", "ー:64:240", "ん:65:720", "あ:60:480"]),
    # Original diagnostic melody, eight bars at 4/4, 960 ticks/quarter, 120 BPM.
    # These kana are screening material, not independently reviewed Japanese lyrics.
    ("unfamiliar-song", ["phrase",
        "あ:60:960", "さ:62:960", "の:64:960", "そ:67:480", "ら:64:1440",
        "あ:65:960", "お:64:960", "い:62:960", "か:60:480", "ぜ:62:1440",
        "こ:64:960", "え:67:960", "が:69:960", "ひ:67:480", "び:65:480", "く:64:960",
        "き:62:960", "み:64:960", "と:65:960", "う:67:480", "た:69:480", "う:67:960",
        "あ:72:960", "さ:69:960", "の:67:960", "そ:65:480", "ら:64:1440",
        "こ:62:1920", "え:60:1920", "ー:60:2880"]),
]
MAX_FILE_BYTES = 256 * 1024 * 1024
MAX_PACKET_BYTES = 1024 * 1024 * 1024


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def write_new(path: Path, value) -> None:
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write("\n")


def wav_measurements(path: Path) -> dict:
    if path.stat().st_size > MAX_FILE_BYTES:
        raise ValueError(f"Oversized generated WAV: {path}")
    raw = path.read_bytes()
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError(f"Not RIFF/WAVE: {path}")
    chunks = {}
    position = 12
    while position + 8 <= len(raw):
        name, size = struct.unpack_from("<4sI", raw, position)
        position += 8
        if position + size > len(raw):
            raise ValueError(f"Truncated WAV: {path}")
        if name in (b"fmt ", b"data"):
            if name in chunks:
                raise ValueError(f"Duplicate WAV chunk: {path}")
            chunks[name] = raw[position:position + size]
        position += size + size % 2
    encoding, channels, rate, _, alignment, bits = struct.unpack_from("<HHIIHH", chunks[b"fmt "])
    if encoding != 3 or bits != 32 or not 1 <= channels <= 8 or not rate or alignment != channels * 4:
        raise ValueError(f"Expected Float32 pilot WAV: {path}")
    payload = chunks[b"data"]
    if not payload or len(payload) % alignment:
        raise ValueError(f"Invalid frame payload: {path}")
    energy = peak = 0.0
    clipped = 0
    for (value,) in struct.iter_unpack("<f", payload):
        if not math.isfinite(value):
            raise ValueError(f"Nonfinite audio: {path}")
        peak = max(peak, abs(value))
        energy += value * value
        clipped += abs(value) >= 1.0
    frames = len(payload) // alignment
    return dict(sampleRate=rate, channels=channels, frames=frames,
                durationSeconds=frames / rate, peak=peak,
                rms=math.sqrt(energy / (frames * channels)), clippedSamples=clipped)


def run(binary: Path, output: Path, repo: Path) -> None:
    binary = binary.resolve(strict=True)
    repo = repo.resolve(strict=True)
    output = output.absolute()
    # All runtime source must already belong to a named checkpoint. Documentation may be dirty.
    changed = subprocess.check_output(
        ["git", "diff", "HEAD", "--name-only", "--", "apps", "libs", "tools", "tests", "CMakeLists.txt"],
        cwd=repo, text=True)
    untracked = subprocess.check_output(
        ["git", "ls-files", "--others", "--exclude-standard", "--", "apps", "libs", "tools", "tests"],
        cwd=repo, text=True)
    if changed.strip() or untracked.strip():
        raise ValueError("Commit the reviewed runtime/harness checkpoint before retaining this packet")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
    output.mkdir(parents=False, exist_ok=False)
    build = output / "build"
    build.mkdir()
    retained_binary = build / "seam_singer_pilot"
    shutil.copy2(binary, retained_binary)
    generated_version = repo / "build/release/generated/seam/build/version.hpp"
    if generated_version.is_file():
        shutil.copy2(generated_version, build / "version.hpp")
    # Capture dependency identities without pretending a copied binary is a relocatable app.
    dependencies = subprocess.check_output(["otool", "-L", str(retained_binary)], text=True)
    (build / "dependencies.txt").write_text(dependencies, encoding="utf-8")
    for line in dependencies.splitlines()[1:]:
        dependency = Path(line.strip().split(" (", 1)[0])
        if str(dependency).startswith("/opt/homebrew/") and dependency.is_file():
            shutil.copy2(dependency, build / dependency.name)
    manifest = dict(formatId="com.project-seam.listening-packet", schemaVersion=1,
                    packetId=output.name, createdAt=datetime.now(timezone.utc).isoformat(),
                    status="RENDERING", sourceCommit=commit, binarySha256=digest(retained_binary),
                    system=platform.platform(), machine=platform.machine(),
                    listeningStatus="NOT_REVIEWED", releaseEligible=False,
                    bankComparison="NOT_RUN: candidates are generated audio, not installed sample-bank renders",
                    reproducibility="Retained macOS binary and Homebrew dependency copy; absolute loader paths and system frameworks still required",
                    cases=[], artifacts=[])
    write_new(output / "request.json", dict(sourceCommit=commit, cases=CASES,
              timeoutSecondsPerCase=180, maximumPacketBytes=MAX_PACKET_BYTES,
              totalTimeoutSeconds=1800, listeningStatus="NOT_REVIEWED"))
    started = time.monotonic()
    try:
        for name, arguments in CASES:
            remaining = 1800 - (time.monotonic() - started)
            if remaining <= 0:
                raise TimeoutError("Packet exceeded total render budget")
            case_dir = output / name
            begin = time.monotonic()
            process = subprocess.run([str(retained_binary), str(case_dir), *arguments],
                                     capture_output=True, text=True, timeout=min(180, remaining))
            elapsed = time.monotonic() - begin
            (output / f"{name}.stdout.txt").write_text(process.stdout, encoding="utf-8")
            (output / f"{name}.stderr.txt").write_text(process.stderr, encoding="utf-8")
            if process.returncode:
                raise RuntimeError(f"{name} failed ({process.returncode}): {process.stderr}")
            report = json.loads((case_dir / "pilot.json").read_text())
            outputs = []
            for row in report["runs"]:
                audio = Path(row["wav"]).resolve(strict=True)
                audio.relative_to(case_dir.resolve())
                if digest(audio) != row["sha256"]:
                    raise ValueError(f"Pilot report hash mismatch: {audio}")
                measured = wav_measurements(audio)
                outputs.append(dict(path=str(audio.relative_to(output)), variant=row["variant"],
                                    sha256=row["sha256"], recipeHash=row["recipeHash"], **measured))
            total_audio_seconds = sum(row["durationSeconds"] for row in outputs)
            manifest["cases"].append(dict(id=name, arguments=arguments, outputs=outputs,
                exportAndAnalysisSeconds=elapsed, emittedAudioSeconds=total_audio_seconds,
                exportAndAnalysisSecondsPerEmittedAudioSecond=elapsed / total_audio_seconds))
            # This includes multiple variants, master/candidate output and pitch analysis. It is not
            # the isolated DSP real-time factor or evidence of live callback performance.
            size = sum(path.stat().st_size for path in output.rglob("*") if path.is_file())
            if size > MAX_PACKET_BYTES:
                raise ValueError("Packet exceeded retained-storage budget")
            print(f"{name}: {len(outputs)} WAVs, {elapsed:.2f}s, {size / 1048576:.1f} MiB retained", flush=True)
        for path in sorted(output.rglob("*")):
            if path.is_file():
                manifest["artifacts"].append(dict(path=str(path.relative_to(output)),
                    bytes=path.stat().st_size, sha256=digest(path)))
        manifest["status"] = "RENDERED_UNREVIEWED"
        write_new(output / "manifest.json", manifest)
    except BaseException as error:
        manifest["status"] = "INCOMPLETE"
        manifest["failure"] = str(error)
        write_new(output / "incomplete.json", manifest)
        raise


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path, help="New directory under an existing durable artifact parent")
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    run(args.binary, args.output, args.repo)
