#!/usr/bin/env python3
"""Build, verify, and collect reproducible Project SEAM Phase 5 evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path


def run(command: list[str], root: Path, timeout: int = 900) -> str:
    completed = subprocess.run(
        command,
        cwd=root,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"command failed ({completed.returncode}): {' '.join(command)}\n"
            f"{completed.stdout}"
        )
    return completed.stdout


def write(path: Path, text: str) -> None:
    path.write_text(text if text.endswith("\n") else text + "\n", encoding="utf-8")


def configure_build(root: Path, preset: str) -> None:
    run(["cmake", "--preset", preset], root, 300)
    run(["cmake", "--build", "--preset", preset, "-j2"], root, 1200)


def parse_tests(text: str) -> int:
    match = re.search(r"(\d+) passed,\s*(\d+) failed", text)
    if match is None or int(match.group(2)) != 0:
        raise RuntimeError("named test output did not report zero failures")
    return int(match.group(1))


def ctest_ok(text: str) -> bool:
    return "100% tests passed" in text and "0 tests failed" in text


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def convert_ppm(source: Path, destination: Path) -> None:
    try:
        from PIL import Image

        with Image.open(source) as image:
            image.save(destination)
        return
    except Exception:
        pass
    executable = shutil.which("magick") or shutil.which("convert")
    if executable is None:
        return
    subprocess.run([executable, str(source), str(destination)], check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--skip-thread-sanitizer", action="store_true")
    args = parser.parse_args()

    root = args.root.resolve()
    evidence = root / "docs/phase5/evidence"
    output = root / "out/phase5"
    shutil.rmtree(evidence, ignore_errors=True)
    shutil.rmtree(output, ignore_errors=True)
    evidence.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)

    presets = ["dev", "release", "sanitize"]
    if not args.skip_thread_sanitizer:
        presets.append("thread-sanitize")
    if not args.skip_build:
        for preset in presets:
            configure_build(root, preset)

    direct = run([str(root / "build/dev/seam_tests")], root, 300)
    write(evidence / "direct-tests.txt", direct)
    direct_count = parse_tests(direct)

    ctest: dict[str, str] = {}
    for preset in ("dev", "release", "sanitize"):
        text = run(["ctest", "--preset", preset, "--output-on-failure"], root, 900)
        write(evidence / f"ctest-{preset}.txt", text)
        if not ctest_ok(text):
            raise RuntimeError(f"{preset} CTest did not record a complete pass")
        ctest[preset] = "pass"

    tsan = "not-run"
    if not args.skip_thread_sanitizer:
        text = run([str(root / "build/thread-sanitize/seam_tests")], root, 600)
        write(evidence / "thread-sanitizer-direct-tests.txt", text)
        if parse_tests(text) != direct_count:
            raise RuntimeError("ThreadSanitizer and dev named-test counts differ")
        tsan = "pass"

    demo_text = run(
        [str(root / "build/dev/seam_phase5_demo"), "--output", str(output)],
        root,
        300,
    )
    write(evidence / "phase5-demo.txt", demo_text)
    for name in (
        "phase5-summary.json",
        "phase5-editor-1x.ppm",
        "phase5-editor-2x.ppm",
        "phase5-playback-reference.wav",
    ):
        shutil.copy2(output / name, evidence / name)
    convert_ppm(evidence / "phase5-editor-1x.ppm", evidence / "phase5-editor-1x.png")
    convert_ppm(evidence / "phase5-editor-2x.ppm", evidence / "phase5-editor-2x.png")

    xvfb = shutil.which("xvfb-run")
    if xvfb is None:
        raise RuntimeError("xvfb-run is required for Phase 5 native evidence")
    native_ppm = evidence / "phase5-native-window.ppm"
    native_text = run(
        [
            xvfb,
            "-a",
            str(root / "build/dev/seam_editor_native"),
            "--force-threaded-audio",
            "--auto-close-ms",
            "500",
            "--screenshot",
            str(native_ppm),
        ],
        root,
        120,
    )
    write(evidence / "native-window-smoke.txt", native_text)
    convert_ppm(native_ppm, evidence / "phase5-native-window.png")

    benchmark_executable = root / "build/release/seam_phase5_benchmark"
    if not benchmark_executable.is_file():
        benchmark_executable = root / "build/dev/seam_phase5_benchmark"
    benchmark_text = run([str(benchmark_executable)], root, 300)
    benchmark = json.loads(benchmark_text)
    benchmark["buildPreset"] = (
        "release" if "/release/" in benchmark_executable.as_posix() else "dev"
    )
    (evidence / "phase5-benchmark.json").write_text(
        json.dumps(benchmark, indent=2) + "\n", encoding="utf-8"
    )

    branch = run(
        [sys.executable, "scripts/verify_master_branch.py", "--root", str(root)],
        root,
        60,
    )
    write(evidence / "branch-policy.txt", branch)
    licenses = run(
        [sys.executable, "tools/license-auditor/audit.py", "--root", str(root)],
        root,
        60,
    )
    write(evidence / "license-audit.txt", licenses)
    fsck = run(["git", "fsck", "--full"], root, 180)
    write(evidence / "git-fsck.txt", fsck or "git fsck --full: pass")
    history = run(["git", "log", "--oneline", "--decorate", "-32"], root, 60)
    write(evidence / "git-history.txt", history)
    diff = run(["git", "diff", "--check"], root, 60)
    write(evidence / "git-diff-check.txt", diff or "git diff --check: pass")

    summary = json.loads((output / "phase5-summary.json").read_text(encoding="utf-8"))
    matrix = {
        "phase": "5.0",
        "applicationVersion": "0.5.0",
        "branchPolicy": "master-only-pass",
        "warningsAsErrors": True,
        "namedTests": {"passed": direct_count, "failed": 0},
        "ctest": ctest,
        "threadSanitizerNamedSuite": tsan,
        "headlessDemo": "pass",
        "nativeX11Window": "pass",
        "nativeImePath": "XIM/XIC",
        "physicalAudioAdapter": "PulseAudio Simple runtime adapter",
        "evidenceAudioBackend": summary["audio"]["backend"],
        "evidenceAudioPhysical": summary["audio"]["physical"],
        "callbackUnderflowFrames": summary["audio"]["underflowFrames"],
        "oneXChecksum": summary["nativeUi"]["oneXChecksum"],
        "twoXChecksum": summary["nativeUi"]["twoXChecksum"],
        "benchmark": benchmark,
    }
    (evidence / "verification-matrix.json").write_text(
        json.dumps(matrix, indent=2) + "\n", encoding="utf-8"
    )

    hashes = {
        path.name: sha256(path)
        for path in sorted(evidence.iterdir())
        if path.is_file() and path.name != "SHA256SUMS.json"
    }
    (evidence / "SHA256SUMS.json").write_text(
        json.dumps(hashes, indent=2) + "\n", encoding="utf-8"
    )

    file_tree = "\n".join(
        str(path.relative_to(root))
        for path in sorted(root.rglob("*"))
        if path.is_file()
        and ".git" not in path.parts
        and "build" not in path.parts
        and "out" not in path.parts
    )
    write(root / "docs/phase5/FILE_TREE.txt", file_tree)
    print(json.dumps(matrix, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
