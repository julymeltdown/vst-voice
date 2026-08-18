#!/usr/bin/env python3
"""Collect Phase 9 Unicode/CJK rendering and native-window evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
from pathlib import Path


def run(command: list[str], root: Path, timeout: int = 900) -> str:
    print(f"[phase9-evidence] run: {' '.join(command)}", flush=True)
    with tempfile.TemporaryFile(mode="w+", encoding="utf-8") as output:
        process = subprocess.Popen(
            command,
            cwd=root,
            text=True,
            stdout=output,
            stderr=subprocess.STDOUT,
            start_new_session=(os.name != "nt"),
        )
        try:
            return_code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            if os.name != "nt":
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=5)
            else:
                process.kill()
                process.wait(timeout=5)
            output.seek(0)
            text = output.read()
            raise RuntimeError(
                f"command timed out after {timeout}s: {' '.join(command)}\n{text}"
            ) from error
        output.seek(0)
        text = output.read()
    if return_code != 0:
        raise RuntimeError(
            f"command failed ({return_code}): {' '.join(command)}\n{text}"
        )
    return text


def write(path: Path, text: str) -> None:
    path.write_text(text if text.endswith("\n") else text + "\n", encoding="utf-8")


def configure_build(root: Path, preset: str) -> None:
    run(["cmake", "--preset", preset], root, 300)
    run(["cmake", "--build", "--preset", preset, "-j4"], root, 1200)


def named_test_count(text: str) -> int:
    match = re.search(r"(\d+) passed,\s*(\d+) failed", text)
    if match is None or int(match.group(2)) != 0:
        raise RuntimeError("named test output did not report zero failures")
    return int(match.group(1))


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            value.update(block)
    return value.hexdigest()


def convert_ppm(source: Path, destination: Path) -> None:
    try:
        from PIL import Image
    except ImportError as error:
        raise RuntimeError("Pillow is required to convert Phase 9 evidence") from error
    image = Image.open(source)
    image.save(destination)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--include-sanitizer", action="store_true")
    args = parser.parse_args()

    root = args.root.resolve()
    evidence = root / "docs/phase9/evidence"
    output = root / "out/phase9-evidence"
    shutil.rmtree(evidence, ignore_errors=True)
    shutil.rmtree(output, ignore_errors=True)
    evidence.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)

    presets = ["dev", "release"]
    if args.include_sanitizer:
        presets.append("sanitize")
    if not args.skip_build:
        for preset in presets:
            configure_build(root, preset)

    direct = run([str(root / "build/dev/seam_tests")], root, 300)
    write(evidence / "direct-tests.txt", direct)
    named = named_test_count(direct)

    ctest: dict[str, str] = {}
    sanitizer_native: dict[str, str] = {}
    native_test_pattern = (
        "seam_native_editor_x11_smoke|seam_voicebank_studio_x11_smoke"
    )
    for preset in presets:
        command = ["ctest", "--preset", preset, "--output-on-failure"]
        if preset == "sanitize":
            # Some Xvfb process wrappers outlive CTest's pipe when ASan is active.
            # Keep the sanitizer core suite deterministic, then execute both native
            # smokes explicitly below with a process-group timeout.
            command.extend(["-E", native_test_pattern])
        result = run(command, root, 900)
        if "100% tests passed" not in result or "0 tests failed" not in result:
            raise RuntimeError(f"CTest failed for {preset}")
        write(evidence / f"ctest-{preset}.txt", result)
        ctest[preset] = "pass-core" if preset == "sanitize" else "pass"

    if args.include_sanitizer:
        xvfb = shutil.which("xvfb-run")
        timeout_tool = shutil.which("timeout")
        if xvfb is not None and timeout_tool is not None:
            sanitizer_editor_ppm = output / "phase9-sanitize-native-editor.ppm"
            editor_result = run(
                [
                    timeout_tool,
                    "--kill-after=5s",
                    "90s",
                    xvfb,
                    "-a",
                    str(root / "build/sanitize/seam_editor_native"),
                    "--force-threaded-audio",
                    "--paused",
                    "--character-package",
                    str(root / "assets/character-01"),
                    "--auto-close-ms",
                    "350",
                    "--screenshot",
                    str(sanitizer_editor_ppm),
                ],
                root,
                120,
            )
            write(evidence / "sanitize-native-editor-smoke.txt", editor_result)
            if not sanitizer_editor_ppm.is_file() or sanitizer_editor_ppm.stat().st_size == 0:
                raise RuntimeError("sanitizer native editor did not create a screenshot")
            sanitizer_native["editor"] = "pass"

            sanitizer_studio_ppm = output / "phase9-sanitize-voicebank-studio.ppm"
            studio_result = run(
                [
                    timeout_tool,
                    "--kill-after=5s",
                    "90s",
                    xvfb,
                    "-a",
                    str(root / "build/sanitize/seam_voicebank_studio_native"),
                    "--manifest",
                    str(root / "build/sanitize/phase2-smoke/synthetic-voicebank/manifest.json"),
                    "--force-synthetic-input",
                    "--auto-close-ms",
                    "350",
                    "--screenshot",
                    str(sanitizer_studio_ppm),
                ],
                root,
                120,
            )
            write(evidence / "sanitize-voicebank-studio-smoke.txt", studio_result)
            if not sanitizer_studio_ppm.is_file() or sanitizer_studio_ppm.stat().st_size == 0:
                raise RuntimeError("sanitizer Voicebank Studio did not create a screenshot")
            sanitizer_native["voicebankStudio"] = "pass"
        else:
            sanitizer_native["nativeSmokes"] = "not-available"

    demo = run(
        [str(root / "build/dev/seam_phase9_demo"), "--output", str(output)],
        root,
        120,
    )
    write(evidence / "phase9-demo.txt", demo)
    shutil.copy2(output / "phase9-summary.json", evidence / "phase9-summary.json")
    shutil.copy2(output / "phase9-cjk-text.ppm", evidence / "phase9-cjk-text.ppm")
    convert_ppm(output / "phase9-cjk-text.ppm", evidence / "phase9-cjk-text.png")

    xvfb = shutil.which("xvfb-run")
    native_status = "not-available"
    if xvfb is not None:
        native_ppm = output / "phase9-native-editor.ppm"
        native = run(
            [
                xvfb,
                "-a",
                str(root / "build/dev/seam_editor_native"),
                "--force-threaded-audio",
                "--paused",
                "--character-package",
                str(root / "assets/character-01"),
                "--auto-close-ms",
                "350",
                "--screenshot",
                str(native_ppm),
            ],
            root,
            120,
        )
        write(evidence / "native-editor-smoke.txt", native)
        shutil.copy2(native_ppm, evidence / "phase9-native-editor.ppm")
        convert_ppm(native_ppm, evidence / "phase9-native-editor.png")
        native_status = "pass"

    benchmark = run([str(root / "build/release/seam_phase9_benchmark")], root, 120)
    write(evidence / "phase9-benchmark.json", benchmark)

    branch = run([sys.executable, "scripts/verify_master_branch.py", "--root", str(root)], root, 60)
    licenses = run([sys.executable, "tools/license-auditor/audit.py", "--root", str(root)], root, 60)
    fsck = run(["git", "fsck", "--full"], root, 180)
    diff = run(["git", "diff", "--check"], root, 60)
    history = run(["git", "log", "--oneline", "--decorate", "-45"], root, 60)
    write(evidence / "branch-policy.txt", branch)
    write(evidence / "license-audit.txt", licenses)
    write(evidence / "git-fsck.txt", fsck or "git fsck --full: pass")
    write(evidence / "git-diff-check.txt", diff or "git diff --check: pass")
    write(evidence / "git-history.txt", history)

    summary = json.loads((output / "phase9-summary.json").read_text(encoding="utf-8"))
    matrix = {
        "phase": "9.0",
        "applicationVersion": "0.9.0",
        "branchPolicy": "master-only-pass",
        "namedTests": {"passed": named, "failed": 0},
        "ctest": ctest,
        "sanitizerNativeSmokes": sanitizer_native,
        "unicode": {
            "strictUtf8": "pass",
            "koreanJapaneseChineseLatin": "pass",
            "fontFilesRedistributed": False,
            "trustedSystemFontFaces": len(summary.get("fonts", [])),
            "nativeX11Screenshot": native_status,
        },
        "dependency": {
            "name": "stb_truetype",
            "revision": "f58f558c120e9b32c217290b80bad1a0729fbb2c",
            "license": "MIT",
            "sourceSha256": "ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab",
        },
        "deferred": [
            "full complex-script shaping",
            "audited iPlug2 and Skia adapter",
            "CLAP/VST3/AU",
            "platform release signing/installers",
            "contracted production human voicebank",
        ],
    }
    (evidence / "verification-matrix.json").write_text(
        json.dumps(matrix, indent=2) + "\n", encoding="utf-8"
    )

    hashes = {
        path.name: digest(path)
        for path in sorted(evidence.iterdir())
        if path.is_file() and path.name != "SHA256SUMS.json"
    }
    (evidence / "SHA256SUMS.json").write_text(
        json.dumps(hashes, indent=2) + "\n", encoding="utf-8"
    )

    excluded = {".git", "build", "out", ".cache", "__pycache__"}
    files: list[str] = []
    for directory, names, filenames in os.walk(root):
        names[:] = sorted(name for name in names if name not in excluded)
        base = Path(directory)
        for filename in sorted(filenames):
            files.append(str((base / filename).relative_to(root)))
    write(root / "docs/phase9/FILE_TREE.txt", "\n".join(files))
    print(json.dumps(matrix, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
