# Phase 4.1 Verification Evidence

Generate the stabilization evidence with:

```bash
python3 scripts/generate_phase4_1_evidence.py --root .
```

When all build directories are already configured and current:

```bash
python3 scripts/generate_phase4_1_evidence.py --root . --skip-build
```

The CMake convenience target is:

```bash
cmake --build --preset dev --target seam_phase4_1_evidence
```

## Verification policy

The release gate requires:

- the warnings-as-errors development suite;
- Release CTest;
- ASan + UBSan CTest;
- the complete named test suite under ThreadSanitizer;
- Phase 2–4 smoke demos;
- master-only, license, whitespace, and Git-object checks.

A full TSan CTest run is attempted and recorded separately, but it is non-gating because some CI/container runtimes stall while CTest captures instrumented child processes even when the same TSan binaries complete directly. A timeout is never rewritten as a pass.

## Current source-tree result

The final evidence script derives counts from the executable instead of hard-coding them. At the current Phase 4.1 source revision:

| Check | Result |
|---|---:|
| Direct named tests | 84 passed / 0 failed |
| Development CTest | 6/6 passed |
| Release CTest | 6/6 passed |
| ASan + UBSan CTest | 6/6 passed |
| ThreadSanitizer direct named suite | 84 passed / 0 failed |
| ThreadSanitizer CTest | 6/6 passed |
| Phase 2–4 smoke demos | passed |
| Master-only policy | passed |
| License audit | passed |
| `git diff --check` | passed |
| `git fsck --full` | passed |
| Clean ZIP extraction/rebuild | recorded after final packaging |

## Evidence files

| Artifact | Meaning |
|---|---|
| `direct-tests.txt` | Individually named tests and aggregate result |
| `ctest-dev.txt` | Debug warnings-as-errors CTest result |
| `ctest-release.txt` | Release warnings-as-errors CTest result |
| `ctest-sanitize.txt` | AddressSanitizer + UndefinedBehaviorSanitizer result |
| `thread-sanitizer-direct-tests.txt` | Complete named suite under TSan |
| `ctest-thread-sanitize.txt` | Optional full TSan CTest attempt, including timeout status when applicable |
| `phase2-demo.txt` | Phase 2 regression smoke execution |
| `phase3-demo.txt` | Phase 3 regression smoke execution |
| `phase4-demo.txt` | Phase 4 multi-renderer/playback regression execution |
| `phase4-summary.json` | Structured Phase 4 renderer, callback, cache, and project result |
| `phase4-benchmark.json` | Release performance-regression evidence |
| `phase4-editor.svg` | Existing Unit Lane model evidence |
| `phase4-microscope.svg` | Existing Sample Microscope model evidence |
| `phase4-*.wav` | Four-renderer and callback/playback regression audio |
| `verification-matrix.json` | Machine-readable Phase 4.1 acceptance summary |
| `branch-policy.txt` | Master-only branch-policy result |
| `license-audit.txt` | Dependency/provenance policy result |
| `git-diff-check.txt` | Source whitespace/error check |
| `git-fsck.txt` | Git object-database integrity result |
| `git-history.txt` | Committed source head used during evidence generation |
| `SHA256SUMS.json` | SHA-256 digest for every evidence artifact |

Phase 4.1 introduces no fabricated native-window screenshot. It stabilizes the identity, persistence, security, DSP, scheduler, and playback foundations beneath the existing Phase 4 visual/audio evidence. The bundled voicebank remains synthetic technical test data.
