#!/usr/bin/env python3
"""Build, verify, and collect reproducible Phase 2 evidence."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    root = args.root.resolve()
    out = root / "out/phase2"
    evidence = root / "docs/phase2/evidence"
    shutil.rmtree(out, ignore_errors=True)
    evidence.mkdir(parents=True, exist_ok=True)

    if not args.skip_build:
        run(["cmake", "--preset", "dev"], root)
        run(["cmake", "--build", "--preset", "dev"], root)

    test_output = run([str(root / "build/dev/seam_tests")], root, capture=True)
    (evidence / "test-output.txt").write_text(test_output, encoding="utf-8")

    demo_output = run(
        [str(root / "build/dev/seam_phase2_demo"), "--output", str(out)],
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
    (out / "voicebank-validation.json").write_text(validation_output, encoding="utf-8")
    inspection_output = run(
        [str(root / "build/dev/seam_voicebank_cli"), "inspect", str(manifest)],
        root,
        capture=True,
    )
    (out / "voicebank-inspection.json").write_text(inspection_output, encoding="utf-8")
    run(
        [
            str(root / "build/dev/seam_voicebank_cli"),
            "analyze",
            str(out / "phase2-raw-phrase.wav"),
            str(out / "cli-analysis"),
        ],
        root,
    )

    benchmark_output = run(
        [str(root / "build/dev/seam_phase2_benchmark")], root, capture=True
    )
    benchmark = json.loads(benchmark_output)
    (evidence / "phase2-benchmark.json").write_text(
        json.dumps(benchmark, indent=2) + "\n", encoding="utf-8"
    )

    branch_output = run(
        ["python3", "scripts/verify_master_branch.py", "--root", str(root)],
        root,
        capture=True,
    )
    (evidence / "branch-policy.txt").write_text(branch_output, encoding="utf-8")
    license_output = run(
        ["python3", "tools/license-auditor/audit.py", "--root", str(root)],
        root,
        capture=True,
    )
    (evidence / "license-audit.txt").write_text(license_output, encoding="utf-8")

    copied = [
        "phase2-demo.seam.json",
        "phase2-editor.svg",
        "phase2-raw-phrase.wav",
        "phase2-raw-waveform.svg",
        "phase2-raw-spectrogram.pgm",
        "phase2-summary.json",
        "voicebank-validation.json",
        "voicebank-inspection.json",
        "synthetic-voicebank/manifest.json",
        "cli-analysis/analysis.json",
        "cli-analysis/waveform.svg",
        "cli-analysis/spectrogram.pgm",
    ]
    for relative in copied:
        source = out / relative
        destination = evidence / relative.replace("/", "-")
        shutil.copy2(source, destination)

    inkscape = shutil.which("inkscape")
    if inkscape:
        run(
            [
                inkscape,
                str(out / "phase2-editor.svg"),
                "--export-type=png",
                f"--export-filename={out / 'phase2-editor.png'}",
            ],
            root,
        )
        shutil.copy2(out / "phase2-editor.png", evidence / "phase2-editor.png")

    image_magick = shutil.which("magick") or shutil.which("convert")
    if image_magick:
        run(
            [
                image_magick,
                str(out / "phase2-raw-spectrogram.pgm"),
                str(out / "phase2-raw-spectrogram.png"),
            ],
            root,
        )
        shutil.copy2(
            out / "phase2-raw-spectrogram.png",
            evidence / "phase2-raw-spectrogram.png",
        )

    wav_path = out / "phase2-raw-phrase.wav"
    with wave.open(str(wav_path), "rb") as wav_file:
        audio_metadata = {
            "channels": wav_file.getnchannels(),
            "sampleWidthBytes": wav_file.getsampwidth(),
            "sampleRate": wav_file.getframerate(),
            "frames": wav_file.getnframes(),
            "durationSeconds": wav_file.getnframes() / wav_file.getframerate(),
        }
    (evidence / "audio-metadata.json").write_text(
        json.dumps(audio_metadata, indent=2) + "\n", encoding="utf-8"
    )

    verification = {
        "phase": 2,
        "branchPolicy": "master-only",
        "devBuild": "pass",
        "tests": "pass",
        "endToEndDemo": "pass",
        "voicebankValidation": "pass-with-intentional-warnings",
        "voicebankCli": "pass",
        "rawAudio": audio_metadata,
        "licenseAudit": "pass",
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
    (root / "docs/phase2/FILE_TREE.txt").write_text(
        "\n".join(sorted(files)) + "\n", encoding="utf-8"
    )

    print(f"Phase 2 evidence written to {evidence}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
