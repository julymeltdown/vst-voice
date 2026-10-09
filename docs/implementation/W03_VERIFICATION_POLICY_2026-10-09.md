# W03 production-suite verification policy

Status: scheduling change verified by a full 235/235 passing run under `-j6`. This is engineering test evidence, not product/release acceptance. The separate training-environment update below executes its dependency-enabled tests, but the latest full run has a new RSS-test failure and W03 remains open.

## Evidence and diagnosis

Source baseline: `b961e7547259ebb6c0553def9381910a52e9f19a`, including U21's integrated request-budget fix. All dependent Release targets were rebuilt before testing. The preceding audit retained a 231/232 full result: `seam_public_release_python_tests` exceeded 180 seconds under `-j6`, then passed unchanged in isolation in 108.60 seconds. That observation does not prove contention was the cause.

Fresh measurements:

- Production discovery: 133/133 PASS, 92.964 seconds in an isolated per-test timing run.
- Replay: 6 tests, 44.756 seconds; gate: 18 tests, 18.709 seconds; audit: 1 test, 9.537 seconds. Remaining 108 tests took approximately 20 seconds, including state-machine checks.
- Unchanged full Release CTest: 232/232 PASS, 267.70 seconds under `-j6`. The production aggregate passed in 106.75 seconds. The historical timeout did not recur; its cause remains unknown. After the split, a comparable slowdown may appear as an unusually long entry rather than a timeout, so entry timings must remain visible.
- First split run: 234/235 PASS, 236.48 seconds. All four production entries passed (remaining 24.99s, audit 10.41s, gate 24.79s, replay 51.92s). Source closure correctly failed because this new report had not yet been added to the index. This was an integration-order error, not a production test failure. The report and CMake change were staged before the final full rerun.
- Final indexed split run: **235/235 PASS**, 192.05 seconds under `-j6`. Production entries: remaining 108 cases 22.56s; audit 1 case 10.54s; gate 18 cases 25.25s; replay 6 cases 53.40s. All internal production cases ran without skips. The source-closure check passed. Differences in CTest ordering/cost history and machine load mean total-run timing is not a controlled speedup benchmark.
- An interrupted earlier run stopped at 9/232. Its process handle was gone and no CTest process remained before a fresh run was started. Its partial log is retained and is not a passing/failing full-suite verdict.

Archive/replay cases build signed nested test archives and execute real validators. Source inspection suggests pure-Python signature operations contribute to cost; no CPU profile was collected, so this is an inference rather than an established root cause. The change does not replace crypto, mock the evaluator, reuse validation results, or weaken assertions.

## Supported schedule

After a full dependent build, run:

```sh
cmake --build build/release -j6
ctest --test-dir build/release -j6 --output-on-failure
```

The retained optional configuration registers three additional CTest entries after the split: 235 instead of 232. This registration count is configuration-specific, not a universal product test total.

The existing `seam_public_release_python_tests` runs the 108 remaining cases. Three separately bounded entries run archive audit, gate and replay. All four retain `TIMEOUT 180` per entry. This changes the budget boundary: previously all 133 cases shared 180 seconds; now four separate entries can consume up to 720 seconds in summed entry time. Timeout sensitivity is therefore looser even though each numeric limit is unchanged. At the measured 53.40 seconds, replay alone has about 127 seconds of headroom. This is a scheduling trade-off, not proof that the historical stall was repaired. All four carry the `production-public-release` label. To select the entire production suite, use:

```sh
ctest --test-dir build/release -L '^production-public-release$' -j6 --output-on-failure
```

An exact-name selection of the old entry now selects only its remaining modules. Prefix selection `-R '^seam_public_release_python_tests'` includes all four.

CMake discovers every `tests/production/test_*.py` with `CONFIGURE_DEPENDS`, then assigns each module to exactly one entry. A comparison of the generated CTest commands against ordinary unittest discovery proved all 133 current test IDs occur exactly once within the `production-public-release` label: 108 + 1 + 18 + 6. After the next `cmake --build` rechecks the glob, future matching modules enter the remaining group unless explicitly split; `ctest` alone does not register newly added modules. This avoids a stale manually maintained whitelist. The full CTest configuration also has older separate entries that rerun 10 of these cases; the exactly-once statement applies to this label, not the entire configuration.

## Scope and retained evidence

Raw logs, per-case timings, generated registration, exact partition comparison, cache and build logs are under `out/evidence/w03-2026-10-09/`. They are local and git-ignored, not a pushed or externally anchored archive. The final receipt records their hashes and the source diff.

A passing CTest entry does not mean all internal optional cases ran. The unchanged baseline's training entry discovered 408 cases and skipped 90 with its existing interpreter. Its singing-quality entry discovered 141 and skipped 19: the 12 packet003 native cases require explicit fixture/binary environment variables, while 7 other native lanes require their own inputs. Those 12 were exercised in the preceding dedicated W01 run; the default aggregate result does not rerun them. The production entry ran all 133 with no skips.

The baseline above predates the pinned training environment described below. Human listening, sample-bank production approval, macOS/Windows installed acceptance, host workloads and final release authority remain separate unmet product obligations.

Independent review: APPROVE in reviewer turn `01a11eac-a490-7ad2-a902-425105ae2ab5`, based on source, Git objects, the complete static inventory and retained runtime logs. The reviewer ran no tests. The required clarification of timeout-budget semantics and the unknown historical cause is incorporated above.

## Pinned training environment update

Created a separate Python 3.11.15 environment at `build/neural-runtime/training-w03-20261009`, synchronized the existing hash-locked macOS arm64 requirements, and passed `pip check`. The 63-distribution content fingerprint is `fcdda51f1fc6b42cba63341fc29a637095ad76eba77d5400bf9685efa915699a` (31,209 files; 863,305,394 installed bytes), unchanged on re-capture after testing. CMake's existing `SEAM_VOICE_TRAINING_PYTHON` cache entry points to it. A full dependent rebuild succeeded; the native pitch executable was rebuilt before its use in the focused test.

The first enabled CTest hit the existing 120-second limit. A bounded verbose diagnostic rerun completed in 74.373 seconds and exposed a missing pinned SingingVocoders source checkout. Its hard-coded user-home path is now replaced with a repository-relative default and `SEAM_VOCODER_TEST_CHECKOUT` override. The official documented source revision `4d0889c4c180c75ad3000cc565864656344f8190` was fetched into a fresh ignored directory and verified clean; no pretrained weights were acquired. Missing/dirty/wrong-revision source remains an error when Torch is available. CTest now binds the actual native pitch target automatically, so the waveform-control test executes without a manual environment variable.

Focused training CTest PASS in 76.41 seconds: **469 tests, zero skips**. Native pitch CLI CTest PASS in 1.94 seconds. The enabled discovery count exceeds the old 408 because module-level optional skips previously prevented complete discovery. No timeout, thread count or acoustic threshold was changed. The cause of the initial 120-second event remains unproven.

The subsequent full `-j6` run is explicitly **FAILED: 234/235**, 384.92 seconds. Its training entry passed in 106.63 seconds with all 469 tests and zero skips. The sole failure, before the training entry started, was `test_rss_is_read_from_the_live_process_not_a_constant`: a self-process live RSS baseline of 181,387,264 bytes was compared with a later 36,814,848-byte reading and failed an assumed factor-four stability bound. Both measurements use the live collector; this is not peak-RSS versus current-RSS. The preceding growth test allocates/releases a large ballast, so a shared test-process baseline is an uncontrolled oracle. The exact reason for the shrink has not been established. Next work must check this with a controlled live target, retaining production soak thresholds and the failed log. W03 and the full verification gate remain open until that is resolved and the full matrix passes.

Install/build/configuration logs, before/after environment check, source pin and successful/failed tests are retained in `out/evidence/w03-2026-10-09/training-receipt.json` and its hashed files. These dependencies and receipts are local and git-ignored; repository instructions make the setup reproducible but are not a release archive. This establishes tool/test readiness, not model or singer qualification. Independent review of this increment is pending in turn `01a11ec0-e7ad-7702-bc4f-26005b112d50`.
