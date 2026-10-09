# W03 production-suite verification policy

Status: the complete dependency-enabled Release configuration now passes 235/235 under `-j6`, both with and without a training-specific serial reservation. The latest concurrent run executes all 469 training cases with zero skips. Final scheduler disposition is recorded below. This is engineering verification, not product/release acceptance; historical failures remain retained.

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

Warm-cache focused training CTest PASS in 76.41 seconds: **469 tests, zero skips**. Native pitch CLI CTest PASS in 1.94 seconds. The enabled discovery count exceeds the old 408 because module-level optional skips previously prevented complete discovery. No timeout, thread count or acoustic threshold was changed. The cause of the initial 120-second event remains unproven.

The subsequent full `-j6` run is explicitly **FAILED: 234/235**, 384.92 seconds. Its training entry passed in 106.63 seconds with all 469 tests and zero skips. The sole failure, before the training entry started, was `test_rss_is_read_from_the_live_process_not_a_constant`: a self-process live RSS baseline of 181,387,264 bytes was compared with a later 36,814,848-byte reading and failed an assumed factor-four stability bound. Both measurements use the live collector; this is not peak-RSS versus current-RSS. The preceding growth test allocates/releases a large ballast, so a shared test-process baseline is an uncontrolled oracle. The exact reason for the shrink has not been established. Next work must check this with a controlled live target, retaining production soak thresholds and the failed log. W03 and the full verification gate remain open until that is resolved and the full matrix passes.

Install/build/configuration logs, before/after environment check, source pin and successful/failed tests are retained in `out/evidence/w03-2026-10-09/training-receipt.json` and its hashed files. These dependencies and receipts are local and git-ignored; repository instructions make the setup reproducible but are not a release archive. This establishes warm-cache focused test coverage, not cold-start reliability, model or singer qualification. Independent reviewer turn `01a11ec0-e7ad-7702-bc4f-26005b112d50` approved the source/checkout changes and required this cache qualification: the diagnostic run wrote 1,306 dependency bytecode files, while CTest disables bytecode writes. The content fingerprint excludes bytecode and therefore does not establish equal cache state. Both passing training runs above used warm caches; the original cold run timed out. A later warm-cache full run also timed out, so bytecode state alone does not explain the failures.


## Controlled RSS target and explicit bytecode setup

The RSS test now starts a fresh child, observes an 8 MiB allocation, adds 64 MiB and requires the minimum later RSS to exceed the maximum earlier RSS by 32 MiB. It uses incompressible bytes and periodically touches their pages; retaining idle zero-filled objects was insufficient under this machine's memory pressure. Readiness and teardown are bounded. The collector and product soak criteria are unchanged. Revised fixture: focused PASS 5.227s; all 30 product-soak tests PASS 11.870s. Injecting a constant 100,000,000-byte sampler fails the growth assertion as intended (negative-control wrapper PASS).

The intervening full run used the **superseded idle child**, not this final fixture: 232/235 PASS, 405.32s. It failed the idle RSS fixture, the native output-level concurrent transient test, and training at 120.13s despite partially warmed bytecode. Preserve all three failures. The meter binary postdates its source; a separate injected-publication probe reproduces a startup data-loss race, pending product repair. No full-matrix pass is claimed for the revised RSS fixture yet.

A new environment from the same lock (`build/neural-runtime/training-w03-cache-probe`, Python 3.11.15, initially two bytecode files) timed out at 120.29s in an isolated CTest invocation. After explicit `uv pip sync --compile-bytecode` compiled 11,168 files in 10.24s, the same entry passed: 469 cases, zero skips, 81.899s internally / 89.64s CTest. Both setup documents now include that flag. This sequential comparison supports explicit cache preparation; it does not prove the historical timeout's cause or that compilation alone makes concurrent runs reliable. Other applications imposed substantial CPU and memory pressure. No external processes were stopped, and no test timeout, Torch thread count or product threshold was changed.

Evidence: local ignored `out/evidence/w03-rss-2026-10-09/receipt.json`, including failed full/cold logs, compiled pass, active-fixture checks and host-load samples. Review and a full matrix with all repairs remain pending. W03 remains open; no installed-product, listening, model-quality or Beta acceptance changes.


## Complete dependency-enabled verification comparison

Native meter repair `aef6ee80` prevents a running empty/stale UI read from erasing a concurrent audio publication. Independent reviewer `01a11ed7-68e8-7dd3-9222-197a6e69a0cf` approved that repair and RSS/setup checkpoint `946f5694`. The new startup regression fails on old uninstrumented source in 20/20 invocations (each stops at its first failed CHECK, with at most 256 attempts), versus 20/20 successful fixed CTest invocations completing 5,120 attempts. The deterministic instrumented before/after probe and focused 10-case ThreadSanitizer run are retained in the meter receipt. This is not whole-project sanitizer or installed-product evidence.

After a full dependent rebuild, both scheduling settings passed on the same compiled environment and native code:

| Setting | Full CTest | Training CTest | Internal training cases | Remaining training timeout margin |
| --- | --- | --- | --- | --- |
| Training `RUN_SERIAL`, other entries `-j6` | 235/235, 365.82s | 78.88s | 469, zero skips | 41.12s |
| Normal `-j6`, no training reservation | 235/235, 346.56s | 111.63s | 469, zero skips | 8.37s |

The concurrent production entries also executed all 133 cases without skips: remaining108/32.78s, audit1/13.76s, gate18/43.70s and replay6/86.88s. No production assertion, crypto check, acoustic criterion, numerical thread setting or entry timeout changed. These sequential busy-host runs do not establish a causal performance improvement or prove serialization necessary. Swap use was 12,570.25M before the concurrent run and 12,410.25M near its end; exact `vm.swapusage` output and `vm_stat` snapshots are retained rather than treated as a controlled machine profile.

The active compiled environment is `build/neural-runtime/training-w03-cache-probe`: Python3.11.15, 63 distributions, 31,209 hashed files and 863,305,460 bytes. Fingerprint `dd37440583850ab6dbc81e4c8e8f3609c1931b6c0c8f7e1bbfadc8dd42c90dfc`; `pip check` PASS. It uses the same locked package set as the earlier environment. Its content fingerprint differs because entrypoint shebangs bind a different environment prefix: every changed bin script was reproduced by prefix substitution. Bytecode is excluded from both fingerprints.

CTest now requests verbose unittest names. A one-second known-answer timeout probe printed a complete line followed by explicitly flushed text without a newline; CTest retained the line and dropped the trailing text. Thus the repeated warning after 116 dots in earlier failed logs cannot locate the actual test at termination. The historical timeouts remain unexplained; partial cache state and host contention are plausible factors, not proved causes. The late concurrent process sample shows the vocoder recovery subprocess running and the completed verbose log confirms its PASS. No unsupported timeout signal was added.

### Remaining skips and their named coverage

There are 24 skip instances across aggregate entries, representing 23 distinct cases. Four native phase13a cases skipped in the general entry run in `seam_neural_package_materialization_tests` (three probe cases) and `seam_neural_worker_relocatability` (one staging case). The staging case also accounts for the one duplicate skip in the materialization entry. Of the 19 singing-quality aggregate skips, four run in `seam_singing_quality_workflow`, three in `seam_import_refuses_unrenderable`, and twelve in the separate W01 packet-native lane retained in `out/evidence/w01-2026-10-09/packet003-tools/receipt.json`. Those twelve were exercised in the prior dedicated W01 run, not rerun by this default matrix. Their evidence proves integrity/refusal behavior; Q1 and a valid formal listening packet remain unresolved under W01/W05a.

Raw serial/concurrent logs, generated registrations, the compiled runtime fingerprint, host snapshots, diagnostic probe, and skip mapping are under local ignored `out/evidence/w03-final-2026-10-09/`. They are not a signed or externally anchored release archive. Human listening, rights-cleared bank approval, 20-row UA acceptance, learned-voice qualification, Windows, host/platform and final release gates remain separate open work.


Final committed policy: retain normal `ctest --test-dir build/release -j6 --output-on-failure`, use the hash-locked explicitly bytecode-compiled interpreter and pinned upstream source, and keep verbose training output. `RUN_SERIAL` was tested but is not retained: the concurrent configuration passes, and the measurements do not establish that serialization is necessary. Its 8.37s training margin is narrow; a later timeout must retain its named-test log and be investigated, not silently retried or hidden by a larger budget. The reviewer already approved `-v` and both product/test repairs; review of these final comparison records is pending in `01a11ee9-cf32-7fe1-91e2-7b66bd8ffeac`. The passing configuration is established; independent final-record review remains explicit. This permits source integration checks using the demonstrated configuration, without promoting any product acceptance gate.
