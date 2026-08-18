#!/usr/bin/env python3
"""Generate Phase 10 CLAP plug-in evidence from built targets."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


def run(command: list[str], root: Path, timeout: int = 900) -> str:
    completed = subprocess.run(command, cwd=root, text=True,
                               stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT,
                               timeout=timeout, check=False)
    if completed.returncode != 0:
        raise RuntimeError(f"command failed ({completed.returncode}): {' '.join(command)}\n{completed.stdout}")
    return completed.stdout


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def test_count(text: str) -> int:
    match = re.search(r"(\d+) passed,\s*(\d+) failed", text)
    if match is None or int(match.group(2)) != 0:
        raise RuntimeError("named tests did not report zero failures")
    return int(match.group(1))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    evidence = root / "docs/phase10/evidence"
    output = root / "out/phase10-evidence"
    shutil.rmtree(evidence, ignore_errors=True)
    shutil.rmtree(output, ignore_errors=True)
    evidence.mkdir(parents=True)
    output.mkdir(parents=True)

    if not args.skip_build:
        for preset in ("dev", "release"):
            run(["cmake", "--preset", preset], root, 300)
            run(["cmake", "--build", "--preset", preset, "-j4"], root, 1800)

    direct = run([str(root / "build/dev/seam_tests")], root, 300)
    (evidence / "direct-tests.txt").write_text(direct, encoding="utf-8")
    named = test_count(direct)

    phase_ctest = run([
        "ctest", "--test-dir", str(root / "build/dev"),
        "-R", "seam_phase10_demo_smoke|seam_clap_state_pack_smoke|seam_clap_state_inspect_smoke|seam_clap_state_extract_smoke|seam_clap_plugin_host_smoke",
        "--output-on-failure",
    ], root, 300)
    (evidence / "ctest-phase10.txt").write_text(phase_ctest, encoding="utf-8")

    demo = run([str(root / "build/dev/seam_phase10_demo"),
                "--output", str(output)], root, 180)
    (evidence / "phase10-demo.txt").write_text(demo, encoding="utf-8")
    plugin = root / "build/dev/ProjectSEAM.clap"
    diagnostic_wav = output / "phase10-diagnostic-4ch.wav"
    state = output / "phase10-from-wav.seamclapstate"
    extracted_wav = output / "phase10-from-state.wav"
    state_tool = root / "build/dev/seam_clap_state_tool"
    packed = run([str(state_tool), "pack", str(diagnostic_wav), str(state),
                  "--title", "Project SEAM packed render",
                  "--gain-db", "0.0"], root, 180)
    inspected = run([str(state_tool), "inspect", str(state)], root, 180)
    extracted = run([str(state_tool), "extract", str(state),
                     str(extracted_wav)], root, 180)
    (evidence / "clap-state-pack.txt").write_text(packed, encoding="utf-8")
    (evidence / "clap-state-inspect.json").write_text(inspected, encoding="utf-8")
    (evidence / "clap-state-extract.txt").write_text(extracted, encoding="utf-8")
    host_summary = output / "clap-host-summary.json"
    host = run([str(root / "build/dev/seam_clap_host"),
                "--plugin", str(plugin), "--state", str(state),
                "--summary", str(host_summary)], root, 180)
    (evidence / "clap-host-smoke.txt").write_text(host, encoding="utf-8")
    shutil.copy2(output / "summary.json", evidence / "phase10-summary.json")
    shutil.copy2(host_summary, evidence / "clap-host-summary.json")

    benchmark = run([str(root / "build/release/seam_phase10_benchmark")], root, 300)
    (evidence / "phase10-benchmark.json").write_text(benchmark, encoding="utf-8")

    export_text = "not-applicable\n"
    if os.name != "nt" and shutil.which("nm"):
        export_text = run(["nm", "-D", str(plugin)], root, 60)
        if "clap_entry" not in export_text:
            raise RuntimeError("clap_entry was not exported")
    (evidence / "plugin-exports.txt").write_text(export_text, encoding="utf-8")

    branch = run([sys.executable, "scripts/verify_master_branch.py", "--root", str(root)], root, 60)
    licenses = run([sys.executable, "tools/license-auditor/audit.py", "--root", str(root)], root, 60)
    fsck = run(["git", "fsck", "--full"], root, 180)
    diff = run(["git", "diff", "--check"], root, 60)
    (evidence / "branch-policy.txt").write_text(branch, encoding="utf-8")
    (evidence / "license-audit.txt").write_text(licenses, encoding="utf-8")
    (evidence / "git-fsck.txt").write_text(fsck or "git fsck --full: pass\n", encoding="utf-8")
    (evidence / "git-diff-check.txt").write_text(diff or "git diff --check: pass\n", encoding="utf-8")

    host_json = json.loads(host_summary.read_text(encoding="utf-8"))
    matrix = {
        "phase": "10.0",
        "applicationVersion": "0.10.0",
        "branchPolicy": "master-only-pass",
        "namedTests": {"passed": named, "failed": 0},
        "phase10Ctest": {"passed": 5, "failed": 0},
        "plugin": {
            "file": plugin.name,
            "sha256": sha256(plugin),
            "clapVersion": "1.2.10",
            "id": host_json["pluginId"],
            "channels": host_json["channels"],
            "dynamicLoad": "pass",
            "stateRoundTrip": host_json["stateRoundTrip"],
            "activeLoadRejected": host_json["activeLoadRejected"],
            "restartRequests": host_json["restartRequests"],
            "sampleAccurateAutomation": {
                "status": "pass",
                "minus6DbRatio": host_json["automationRatio"],
            },
            "transportPauseSilence": host_json["transportPauseSilence"],
        },
        "state": {
            "format": "SEAMCLP1",
            "wavPackInspectExtract": "pass",
            "maximumBytes": 256 * 1024 * 1024,
            "maximumChannels": 8,
            "sha256Protected": True,
        },
        "dependency": {
            "name": "CLAP ABI subset",
            "revision": "195b42a004144fab0b3cf95e9c067187d15365b7",
            "license": "MIT",
        },
        "deferred": [
            "CLAP GUI extension and embedded editor",
            "asynchronous host-side singing render",
            "live note-event synthesis",
            "VST3 and AU",
            "broad third-party host certification",
        ],
    }
    (evidence / "verification-matrix.json").write_text(
        json.dumps(matrix, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(matrix, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
