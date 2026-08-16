# Phase 4.1 Evidence Index

Generate the stabilization evidence with:

```bash
python3 scripts/generate_phase4_1_evidence.py --root .
```

When Debug, Release, ASan/UBSan, and TSan verification has already been executed separately, retain the logs in `docs/phase4_1/evidence/` and use:

```bash
python3 scripts/generate_phase4_1_evidence.py \
  --root . --skip-build --reuse-verification
```

The CMake convenience target is:

```bash
cmake --build --preset dev --target seam_phase4_1_evidence
```

| Artifact | Meaning |
|---|---|
| `direct-tests.txt` | 83 individually named tests and aggregate result |
| `ctest-dev.txt` | Debug warnings-as-errors CTest result |
| `ctest-release.txt` | Release warnings-as-errors CTest result |
| `ctest-sanitize.txt` | AddressSanitizer + UndefinedBehaviorSanitizer CTest result |
| `ctest-thread-sanitize.txt` | Target-by-target ThreadSanitizer result with `TSAN_RESULT=PASS` |
| `phase2-demo.txt` | Phase 2 regression smoke execution |
| `phase3-demo.txt` | Phase 3 regression smoke execution |
| `phase4-demo.txt` | Phase 4 multi-renderer/playback regression execution |
| `phase4-summary.json` | Structured Phase 4 renderer, callback, cache, and project result |
| `phase4-benchmark.json` | Release benchmark retained as performance-regression evidence |
| `phase4-editor.svg` | Existing real Unit Lane model evidence |
| `phase4-microscope.svg` | Existing real Sample Microscope model evidence |
| `phase4-*.wav` | Existing four-renderer and callback/playback regression audio |
| `verification-matrix.json` | Machine-readable Phase 4.1 acceptance summary |
| `branch-policy.txt` | Master-only branch-policy result |
| `license-audit.txt` | Dependency/provenance policy result |
| `git-diff-check.txt` | Source whitespace/error check |
| `git-fsck.txt` | Git object-database integrity result |
| `git-history.txt` | Committed source head used during evidence generation |
| `SHA256SUMS.json` | SHA-256 digest for every evidence artifact |

Phase 4.1 introduces no new presentation screenshot. It stabilizes the correctness, durability, security, and concurrency underneath the existing Phase 4 visual and audio feature evidence. The synthetic voicebank remains technical test data and is not represented as Official Voicebank 01.
