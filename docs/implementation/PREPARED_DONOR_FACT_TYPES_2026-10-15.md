# Prepared donor selected-fact types — 2026-10-15

The independent review of `3e27aa27fad4f4f87176c47aaaf6f316fcb27542` approved the engineering API with one P3: selected proposal integers were checked after a JSON round trip. An integral double such as `80.0` could become integer `80`, contrary to the preceding reader and Python preflight. The exact verdict is retained separately; it is not rights, production-QC, listening or Beta approval.

`evidenceFrom` now checks the original selected row's `sampleRate`, `frames`, `startFrame` and `endFrameExclusive` as integers, `nonzeroPcm` as boolean, and `durationSeconds` as number before producing canonical JSON. This check is shared by both file-backed readers and the filesystem-free owned validator. Evidence-only reading still establishes metadata integrity, not measured WAV facts. `audioFacts` continues to measure and compare actual held audio.

Source base: `7aae8bdf1f17243da2461261eade2f18ef37c88e`; three-file final patch SHA-256: `e125e6338df6ebf33322d0ce9adb3d15881b31986936fc085af5e5cc9891a225`. The adjacent JSON index binds `out/evidence/prepared-donor-fact-types-2026-10-15/` and the review. The old API evidence manifest remains immutable.

| Executed check | Result |
| --- | --- |
| Test-only baseline build | PASS, 3.244 seconds |
| New native regression against unchanged reader | 0 PASS / 1 FAIL at `CHECK(!evidence)`, 0.320 seconds: the re-pinned packet with `frames: 80.0` was accepted |
| Full dependent Release rebuild after the library change | PASS, 31.186 seconds |
| Snapshot, source-intake and sample-review CLI CTest lanes | 3/3 PASS, 20.998 seconds; 5 + 30 C++ cases and 41 Python methods, zero skips |

The new unique C++ case checks six re-pinned malformed values across all three entry points, including exact invalid-argument errors naming the affected field. The existing Python adversarial CLI method gained the same six forms; the method count remains 41. Existing valid fixture paths continue to pass. Two evidence-retention setup mistakes were corrected before native execution: an assumed `build/release/tests` executable directory and an incorrect CLI binary name. Their records are retained; no native failure or CTest run is attributed to either setup mistake.

The frozen manifest contains 33 artifacts and eight preserved bindings: exact baseline/final source blobs/patches, build/test logs, copied executables, version headers, explicit native before/after hashes, parent manifests, the reviewer verdict and the unchanged four gate/dossier files. The read-only verifier passes and executes no retained native tool. No sanitizer, full-suite, additional real-donor selection or human acceptance is claimed for this unit. Independent engineering review of exact `bd35ddac9851940c9b62a8c3c4546d10632b6989` is APPROVE with no findings and closes this P3. The adjacent index binds the supplemental exact verdict/metadata; the original manifest remains immutable. Declared metadata self-consistency on the evidence-only path is a later candidate-verification task, not part of this wire-type closure.

```sh
python3 -B out/evidence/prepared-donor-fact-types-2026-10-15/verify.py
# Use --commit COMMIT after integration or later source changes.
```

Next is schema-5 durable donor binding, then candidate-4 notice closure through directory/package/install verification. Original corpus license and reviewed source-rights declaration remain distinct. Actual source admission awaits the pending owner and real actor/reviewer inputs; no recording was imported by this change. Recorded Alpha remains 0/20, U45 HELD, Beta BLOCKED. `.github` untouched; Windows TODO remains.
