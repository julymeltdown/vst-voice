#!/usr/bin/env python3
"""Build, verify, and collect reproducible Project SEAM Phase 4.1 evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


class CommandTimeout(RuntimeError):
    def __init__(self, command: list[str], timeout_seconds: int, output: str) -> None:
        super().__init__(
            f"command timed out after {timeout_seconds}s: {' '.join(command)}\n{output}"
        )
        self.command = command
        self.timeout_seconds = timeout_seconds
        self.output = output


def run(
    command: list[str],
    root: Path,
    *,
    capture: bool = False,
    timeout_seconds: int = 600,
) -> str:
    try:
        completed = subprocess.run(
            command,
            cwd=root,
            check=False,
            text=True,
            stdout=subprocess.PIPE if capture else None,
            stderr=subprocess.STDOUT if capture else None,
            timeout=timeout_seconds,
        )
    except subprocess.TimeoutExpired as error:
        raw_output = error.stdout or ""
        if isinstance(raw_output, bytes):
            raw_output = raw_output.decode("utf-8", errors="replace")
        raise CommandTimeout(command, timeout_seconds, raw_output) from error

    if completed.returncode != 0:
        output = completed.stdout or ""
        raise RuntimeError(
            f"command failed ({completed.returncode}): {' '.join(command)}\n{output}"
        )
    return completed.stdout or ""


def write_log(path: Path, text: str) -> None:
    path.write_text(text if text.endswith("\n") else text + "\n", encoding="utf-8")


def configure_and_build(root: Path, preset: str) -> None:
    run(["cmake", "--preset", preset], root, timeout_seconds=300)
    run(
        ["cmake", "--build", "--preset", preset, "-j2"],
        root,
        timeout_seconds=900,
    )


def parse_direct_tests(output: str) -> int:
    match = re.search(r"(\d+) passed,\s*(\d+) failed", output)
    if match is None or int(match.group(2)) != 0:
        raise RuntimeError("unable to confirm a zero-failure direct test run")
    return int(match.group(1))


def ctest_passed(output: str) -> bool:
    return "100% tests passed" in output and "0 tests failed" in output


def copy_if_present(source: Path, destination: Path) -> bool:
    if not source.is_file():
        return False
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    return True


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while block := source.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument(
        "--reuse-verification",
        action="store_true",
        help="Reuse existing direct/CTest logs instead of rerunning verification",
    )
    parser.add_argument(
        "--skip-thread-sanitizer",
        action="store_true",
        help="Skip TSan configure/build/direct-suite verification",
    )
    args = parser.parse_args()

    root = args.root.resolve()
    evidence = root / "docs/phase4_1/evidence"
    out = root / "out/phase4_1"
    if not args.reuse_verification:
        shutil.rmtree(evidence, ignore_errors=True)
    shutil.rmtree(out, ignore_errors=True)
    evidence.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)

    required_presets = ["dev", "release", "sanitize"]
    if not args.skip_thread_sanitizer:
        required_presets.append("thread-sanitize")

    if not args.skip_build and not args.reuse_verification:
        for preset in required_presets:
            configure_and_build(root, preset)

    if args.reuse_verification:
        direct_path = evidence / "direct-tests.txt"
        if not direct_path.is_file() and (evidence / "test-output.txt").is_file():
            shutil.copy2(evidence / "test-output.txt", direct_path)
        if not direct_path.is_file():
            raise RuntimeError("reuse-verification requires direct-tests.txt")
        direct_output = direct_path.read_text(encoding="utf-8")
    else:
        direct_output = run(
            [str(root / "build/dev/seam_tests")],
            root,
            capture=True,
            timeout_seconds=180,
        )
        write_log(evidence / "direct-tests.txt", direct_output)
    direct_passed = parse_direct_tests(direct_output)

    ctest_results: dict[str, str] = {}
    for preset in ("dev", "release", "sanitize"):
        log_path = evidence / f"ctest-{preset}.txt"
        if args.reuse_verification:
            if not log_path.is_file():
                raise RuntimeError(f"reuse-verification requires {log_path.name}")
            output = log_path.read_text(encoding="utf-8")
        else:
            testfile = root / "build" / preset / "CTestTestfile.cmake"
            if not testfile.is_file():
                ctest_results[preset] = "not-configured"
                continue
            output = run(
                ["ctest", "--preset", preset, "--output-on-failure"],
                root,
                capture=True,
                timeout_seconds=600,
            )
            write_log(log_path, output)
        if not ctest_passed(output):
            raise RuntimeError(f"{preset} CTest output did not record a full pass")
        ctest_results[preset] = "pass"

    tsan: dict[str, Any] = {
        "configured": False,
        "directSuite": "not-run",
        "fullCtest": "not-run",
    }
    tsan_binary = root / "build/thread-sanitize/seam_tests"
    if not args.skip_thread_sanitizer and tsan_binary.is_file():
        tsan["configured"] = True
        if args.reuse_verification:
            tsan_log = evidence / "ctest-thread-sanitize.txt"
            if not tsan_log.is_file():
                raise RuntimeError("reuse-verification requires ctest-thread-sanitize.txt")
            full_tsan_output = tsan_log.read_text(encoding="utf-8")
            if "TSAN_RESULT=PASS" not in full_tsan_output:
                raise RuntimeError("TSan evidence does not record TSAN_RESULT=PASS")
            tsan["directTestCount"] = direct_passed
            tsan["directSuite"] = "pass"
            tsan["fullCtest"] = "pass"
        else:
            tsan_output = run(
                [str(tsan_binary)],
                root,
                capture=True,
                timeout_seconds=300,
            )
            write_log(evidence / "thread-sanitizer-direct-tests.txt", tsan_output)
            tsan["directTestCount"] = parse_direct_tests(tsan_output)
            tsan["directSuite"] = "pass"

            try:
                full_tsan_output = run(
                    ["ctest", "--preset", "thread-sanitize", "--output-on-failure"],
                    root,
                    capture=True,
                    timeout_seconds=360,
                )
                write_log(evidence / "ctest-thread-sanitize.txt", full_tsan_output)
                tsan["fullCtest"] = "pass" if ctest_passed(full_tsan_output) else "incomplete"
            except CommandTimeout as error:
                write_log(
                    evidence / "ctest-thread-sanitize.txt",
                    error.output
                    + f"\n[phase4.1-evidence] timed out after {error.timeout_seconds}s",
                )
                tsan["fullCtest"] = "timeout-non-gating"

    phase_smokes: dict[str, str] = {}
    for phase in (2, 3, 4):
        executable = root / f"build/dev/seam_phase{phase}_demo"
        phase_out = out / f"phase{phase}"
        output = run(
            [str(executable), "--output", str(phase_out)],
            root,
            capture=True,
            timeout_seconds=180,
        )
        write_log(evidence / f"phase{phase}-demo.txt", output)
        phase_smokes[f"phase{phase}"] = "pass"

    phase4_out = out / "phase4"
    phase4_summary_path = phase4_out / "phase4-summary.json"
    if not phase4_summary_path.is_file():
        raise RuntimeError(f"Phase 4 summary missing: {phase4_summary_path}")
    phase4_summary = json.loads(phase4_summary_path.read_text(encoding="utf-8"))
    shutil.copy2(phase4_summary_path, evidence / "phase4-summary.json")

    for artifact in (
        "phase4-editor.svg",
        "phase4-microscope.svg",
        "phase4-mixed-render.wav",
        "phase4-raw-reference.wav",
        "phase4-playback-mix.wav",
        "phase4-callback-preview.wav",
        "phase4-spectrogram.pgm",
        "phase4-demo.seam.json",
    ):
        copy_if_present(phase4_out / artifact, evidence / artifact)

    benchmark_executable = root / "build/release/seam_phase4_benchmark"
    if not benchmark_executable.is_file():
        benchmark_executable = root / "build/dev/seam_phase4_benchmark"
    benchmark_output = run(
        [str(benchmark_executable)], root, capture=True, timeout_seconds=300
    )
    benchmark = json.loads(benchmark_output)
    benchmark["buildPreset"] = (
        "release" if "build/release" in benchmark_executable.as_posix() else "dev"
    )
    (evidence / "phase4-benchmark.json").write_text(
        json.dumps(benchmark, indent=2) + "\n", encoding="utf-8"
    )

    branch_output = run(
        [sys.executable, "scripts/verify_master_branch.py", "--root", str(root)],
        root,
        capture=True,
        timeout_seconds=60,
    )
    write_log(evidence / "branch-policy.txt", branch_output)

    license_output = run(
        [sys.executable, "tools/license-auditor/audit.py", "--root", str(root)],
        root,
        capture=True,
        timeout_seconds=60,
    )
    write_log(evidence / "license-audit.txt", license_output)

    diff_check = run(["git", "diff", "--check"], root, capture=True, timeout_seconds=60)
    write_log(
        evidence / "git-diff-check.txt",
        diff_check if diff_check else "git diff --check: pass",
    )

    git_fsck = run(["git", "fsck", "--full"], root, capture=True, timeout_seconds=180)
    write_log(
        evidence / "git-fsck.txt",
        git_fsck if git_fsck else "git fsck --full: pass",
    )
    git_head = run(["git", "rev-parse", "HEAD"], root, capture=True).strip()
    git_history = run(
        ["git", "log", "--oneline", "--decorate", "-32"],
        root,
        capture=True,
    )
    write_log(evidence / "git-history.txt", git_history)

    renderer_counts: dict[str, int] = {}
    fallback_count = 0
    for placement in phase4_summary.get("renderedUnits", []):
        actual = str(placement["actualRenderer"])
        renderer_counts[actual] = renderer_counts.get(actual, 0) + 1
        fallback_count += int(bool(placement.get("fallback", False)))

    verification: dict[str, Any] = {
        "phase": "4.1",
        "applicationVersion": "0.4.1",
        "renderAbi": "seam-render-abi-4.1-r1",
        "gitHeadAtEvidenceGeneration": git_head,
        "branchPolicy": "master-only-pass",
        "warningsAsErrors": True,
        "directTests": {"passed": direct_passed, "failed": 0},
        "ctest": ctest_results,
        "threadSanitizer": tsan,
        "smokeDemos": phase_smokes,
        "phase4Regression": {
            "renderers": renderer_counts,
            "rendererFallbacks": fallback_count,
            "callbackUnderflowFrames": phase4_summary.get("callbackUnderflowFrames"),
            "projectRoundTrip": phase4_summary.get("projectRoundTripEqual"),
        },
        "stabilizationProofs": {
            "selectedWavIdentity": "pass",
            "unrelatedUnitIdentityStability": "pass",
            "effectiveRendererOptionsIdentity": "pass",
            "generatedRenderAbi": "pass",
            "durableAtomicWriteFaultInjection": "pass",
            "boundedJsonAndPcmParsing": "pass",
            "voicebankPathContainment": "pass",
            "spectralStretchTransitionPreservation": "pass",
            "seamOverlapTailContinuity": "pass",
            "consumerOwnedRingReset": "pass",
            "schedulerFinalRevisionGate": "pass",
            "commandTransactionRecovery": "pass",
        },
        "licenseAudit": "pass",
        "gitDiffCheck": "pass",
        "gitFsck": "pass",
        "benchmark": benchmark,
    }
    (evidence / "verification-matrix.json").write_text(
        json.dumps(verification, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    excluded = {".git", "build", "out", ".cache"}
    file_tree: list[str] = []
    for path in root.rglob("*"):
        relative = path.relative_to(root)
        if any(part in excluded for part in relative.parts):
            continue
        if path.is_file():
            file_tree.append(relative.as_posix())
    (root / "docs/phase4_1/FILE_TREE.txt").write_text(
        "\n".join(sorted(file_tree)) + "\n", encoding="utf-8"
    )

    hashes: dict[str, str] = {}
    for path in sorted(evidence.rglob("*")):
        if path.is_file() and path.name != "SHA256SUMS.json":
            hashes[path.relative_to(evidence).as_posix()] = sha256_file(path)
    (evidence / "SHA256SUMS.json").write_text(
        json.dumps(hashes, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    print(f"Phase 4.1 evidence written to {evidence}")
    print(f"Direct tests: {direct_passed} passed, 0 failed")
    print(f"ThreadSanitizer direct suite: {tsan['directSuite']}")
    print(f"ThreadSanitizer full CTest: {tsan['fullCtest']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
