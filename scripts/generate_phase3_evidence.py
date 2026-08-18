#!/usr/bin/env python3
"""Build, verify, and collect reproducible Project SEAM Phase 3 evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import shutil
import struct
import subprocess
import sys
import wave
from pathlib import Path


def run(command: list[str], root: Path, capture: bool = False) -> str:
    completed = subprocess.run(
        command,
        cwd=root,
        check=False,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
    )
    if completed.returncode != 0:
        output = completed.stdout or ""
        raise RuntimeError(
            f"command failed ({completed.returncode}): {' '.join(command)}\n{output}"
        )
    return completed.stdout or ""


def convert_svg(source: Path, destination: Path, root: Path) -> bool:
    inkscape = shutil.which("inkscape")
    if inkscape:
        run(
            [
                inkscape,
                str(source),
                "--export-type=png",
                f"--export-filename={destination}",
            ],
            root,
        )
        return True
    try:
        import cairosvg  # type: ignore

        cairosvg.svg2png(url=str(source), write_to=str(destination))
        return True
    except Exception:
        return False


def convert_pgm(source: Path, destination: Path, root: Path) -> bool:
    image_magick = shutil.which("magick") or shutil.which("convert")
    if image_magick:
        run([image_magick, str(source), str(destination)], root)
        return True
    try:
        from PIL import Image  # type: ignore

        with Image.open(source) as image:
            image.save(destination)
        return True
    except Exception:
        return False


def wav_metadata(path: Path) -> dict[str, float | int]:
    with wave.open(str(path), "rb") as wav_file:
        channels = wav_file.getnchannels()
        sample_width = wav_file.getsampwidth()
        sample_rate = wav_file.getframerate()
        frame_count = wav_file.getnframes()
        payload = wav_file.readframes(frame_count)

    if sample_width != 2:
        raise RuntimeError(f"evidence WAV must be PCM16, got {sample_width} bytes")
    values = struct.unpack(f"<{len(payload) // 2}h", payload)
    mono: list[float] = []
    for frame in range(frame_count):
        begin = frame * channels
        channel_values = values[begin : begin + channels]
        mono.append(sum(channel_values) / (32768.0 * channels))
    peak = max((abs(value) for value in mono), default=0.0)
    rms = math.sqrt(sum(value * value for value in mono) / max(1, len(mono)))
    dc = sum(mono) / max(1, len(mono))
    return {
        "channels": channels,
        "sampleWidthBytes": sample_width,
        "sampleRate": sample_rate,
        "frames": frame_count,
        "durationSeconds": frame_count / sample_rate,
        "peak": peak,
        "rms": rms,
        "dcOffset": dc,
    }


def copy_evidence(out: Path, evidence: Path, relative: str) -> None:
    source = out / relative
    if not source.exists():
        raise RuntimeError(f"expected Phase 3 artifact is missing: {source}")
    destination = evidence / relative.replace("/", "-")
    shutil.copy2(source, destination)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    root = args.root.resolve()
    out = root / "out/phase3"
    evidence = root / "docs/phase3/evidence"
    shutil.rmtree(out, ignore_errors=True)
    shutil.rmtree(evidence, ignore_errors=True)
    evidence.mkdir(parents=True, exist_ok=True)

    if not args.skip_build:
        run(["cmake", "--preset", "dev"], root)
        run(["cmake", "--build", "--preset", "dev", "-j2"], root)

    test_output = run([str(root / "build/dev/seam_tests")], root, capture=True)
    (evidence / "test-output.txt").write_text(test_output, encoding="utf-8")
    match = re.search(r"(\d+) passed,\s*(\d+) failed", test_output)
    if match is None or int(match.group(2)) != 0:
        raise RuntimeError("unable to confirm a zero-failure direct test run")
    passed_tests = int(match.group(1))

    ctest_output = run(
        ["ctest", "--preset", "dev", "--output-on-failure"], root, capture=True
    )
    (evidence / "ctest-dev.txt").write_text(ctest_output, encoding="utf-8")
    ctest_presets: dict[str, str] = {"dev": "pass"}
    for preset in ("release", "sanitize"):
        if (root / "build" / preset / "CTestTestfile.cmake").is_file():
            output = run(
                ["ctest", "--preset", preset, "--output-on-failure"],
                root,
                capture=True,
            )
            (evidence / f"ctest-{preset}.txt").write_text(output, encoding="utf-8")
            ctest_presets[preset] = "pass"

    demo_output = run(
        [str(root / "build/dev/seam_phase3_demo"), "--output", str(out)],
        root,
        capture=True,
    )
    (evidence / "demo-output.txt").write_text(demo_output, encoding="utf-8")

    manifest = out / "synthetic-voicebank/manifest.json"
    validation_output = run(
        [str(root / "build/dev/seam_voicebank_cli"), "validate", str(manifest)],
        root,
        capture=True,
    )
    (out / "voicebank-validation.json").write_text(
        validation_output, encoding="utf-8"
    )
    inspection_output = run(
        [str(root / "build/dev/seam_voicebank_cli"), "inspect", str(manifest)],
        root,
        capture=True,
    )
    (out / "voicebank-inspection.json").write_text(
        inspection_output, encoding="utf-8"
    )
    run(
        [
            str(root / "build/dev/seam_voicebank_cli"),
            "analyze",
            str(out / "phase3-psola-phrase.wav"),
            str(out / "cli-analysis"),
        ],
        root,
    )

    benchmark_output = run(
        [str(root / "build/dev/seam_phase3_benchmark")], root, capture=True
    )
    benchmark = json.loads(benchmark_output)
    (evidence / "phase3-benchmark.json").write_text(
        json.dumps(benchmark, indent=2) + "\n", encoding="utf-8"
    )

    branch_output = run(
        [sys.executable, "scripts/verify_master_branch.py", "--root", str(root)],
        root,
        capture=True,
    )
    (evidence / "branch-policy.txt").write_text(branch_output, encoding="utf-8")
    license_output = run(
        [sys.executable, "tools/license-auditor/audit.py", "--root", str(root)],
        root,
        capture=True,
    )
    (evidence / "license-audit.txt").write_text(license_output, encoding="utf-8")
    git_fsck = run(["git", "fsck", "--full"], root, capture=True)
    (evidence / "git-fsck.txt").write_text(
        git_fsck if git_fsck else "git fsck --full: pass\n", encoding="utf-8"
    )
    git_history = run(
        ["git", "log", "--oneline", "--decorate", "-20"], root, capture=True
    )
    (evidence / "git-history.txt").write_text(git_history, encoding="utf-8")

    copied = [
        "phase3-demo.seam.json",
        "phase3-editor.svg",
        "phase3-psola-phrase.wav",
        "phase3-raw-reference.wav",
        "phase3-waveform.svg",
        "phase3-spectrogram.pgm",
        "phase3-summary.json",
        "voicebank-validation.json",
        "voicebank-inspection.json",
        "synthetic-voicebank/manifest.json",
        "cli-analysis/analysis.json",
        "cli-analysis/waveform.svg",
        "cli-analysis/spectrogram.pgm",
    ]
    for relative in copied:
        copy_evidence(out, evidence, relative)

    editor_png = out / "phase3-editor.png"
    if convert_svg(out / "phase3-editor.svg", editor_png, root):
        shutil.copy2(editor_png, evidence / "phase3-editor.png")

    spectrogram_png = out / "phase3-spectrogram.png"
    if convert_pgm(out / "phase3-spectrogram.pgm", spectrogram_png, root):
        shutil.copy2(spectrogram_png, evidence / "phase3-spectrogram.png")

    audio = wav_metadata(out / "phase3-psola-phrase.wav")
    raw_audio = wav_metadata(out / "phase3-raw-reference.wav")
    audio_metadata = {"psolaPhrase": audio, "rawReference": raw_audio}
    (evidence / "audio-metadata.json").write_text(
        json.dumps(audio_metadata, indent=2) + "\n", encoding="utf-8"
    )

    summary = json.loads((out / "phase3-summary.json").read_text(encoding="utf-8"))
    verification = {
        "phase": 3,
        "branchPolicy": "master-only",
        "devBuild": "pass",
        "ctestPresets": ctest_presets,
        "individualTests": {"passed": passed_tests, "failed": 0},
        "endToEndDemo": "pass",
        "projectSchema": summary["projectSchema"],
        "voicebankSchema": summary["voicebankSchema"],
        "projectRoundTrip": summary["projectRoundTripEqual"],
        "classicPsolaPlacements": sum(
            1
            for placement in summary["renderedUnits"]
            if placement["actualRenderer"] == "classic-psola"
        ),
        "scheduler": {
            "submitted": summary["schedulerSubmitted"],
            "completed": summary["schedulerCompleted"],
            "cacheHits": summary["schedulerCacheHits"],
            "cancelled": summary["schedulerCancelled"],
            "stale": summary["schedulerStale"],
        },
        "voicebankValidation": {
            "status": "pass-with-nonfatal-synthetic-bank-warnings",
            "errors": summary["bankErrors"],
            "warnings": summary["bankWarnings"],
        },
        "audio": audio_metadata,
        "licenseAudit": "pass",
        "gitFsck": "pass",
        "benchmark": benchmark,
    }
    (evidence / "verification-matrix.json").write_text(
        json.dumps(verification, indent=2) + "\n", encoding="utf-8"
    )

    excluded = {".git", "build", "out", ".cache"}
    files: list[str] = []
    for path in root.rglob("*"):
        relative = path.relative_to(root)
        if any(part in excluded for part in relative.parts):
            continue
        if path.is_file():
            files.append(relative.as_posix())
    (root / "docs/phase3/FILE_TREE.txt").write_text(
        "\n".join(sorted(files)) + "\n", encoding="utf-8"
    )

    hashes: dict[str, str] = {}
    for path in sorted(evidence.iterdir()):
        if not path.is_file() or path.name == "SHA256SUMS.json":
            continue
        digest = hashlib.sha256()
        with path.open("rb") as source:
            while block := source.read(1024 * 1024):
                digest.update(block)
        hashes[path.name] = digest.hexdigest()
    (evidence / "SHA256SUMS.json").write_text(
        json.dumps(hashes, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    print(f"Phase 3 evidence written to {evidence}")
    print(f"Direct tests: {passed_tests} passed, 0 failed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
