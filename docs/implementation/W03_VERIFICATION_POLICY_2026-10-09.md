# W03 production-suite verification policy

Status: scheduling change verified by a full 235/235 passing run under `-j6`. This is engineering test evidence, not product/release acceptance. The separate W03 training-environment obligation remains open.

## Evidence and diagnosis

Source baseline: `b961e7547259ebb6c0553def9381910a52e9f19a`, including U21's integrated request-budget fix. All dependent Release targets were rebuilt before testing. The preceding audit retained a 231/232 full result: `seam_public_release_python_tests` exceeded 180 seconds under `-j6`, then passed unchanged in isolation in 108.60 seconds. That observation does not prove contention was the cause.

Fresh measurements:

- Production discovery: 133/133 PASS, 92.964 seconds in an isolated per-test timing run.
- Replay: 6 tests, 44.756 seconds; gate: 18 tests, 18.709 seconds; audit: 1 test, 9.537 seconds. Remaining 108 tests took approximately 20 seconds, including state-machine checks.
- Unchanged full Release CTest: 232/232 PASS, 267.70 seconds under `-j6`. The production aggregate passed in 106.75 seconds. The historical timeout did not recur.
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

The existing `seam_public_release_python_tests` runs the 108 remaining cases. Three separately bounded entries run archive audit, gate and replay. All four retain `TIMEOUT 180`; no timeout is increased. All four carry the `production-public-release` label. To select the entire production suite, use:

```sh
ctest --test-dir build/release -L '^production-public-release$' -j6 --output-on-failure
```

An exact-name selection of the old entry now selects only its remaining modules. Prefix selection `-R '^seam_public_release_python_tests'` includes all four.

CMake discovers every `tests/production/test_*.py` with `CONFIGURE_DEPENDS`, then assigns each module to exactly one entry. A comparison of the generated CTest commands against ordinary unittest discovery proved all 133 current test IDs occur exactly once: 108 + 1 + 18 + 6. Future matching modules automatically enter the remaining group unless explicitly split. This avoids a stale manually maintained whitelist.

## Scope and retained evidence

Raw logs, per-case timings, generated registration, exact partition comparison, cache and build logs are under `out/evidence/w03-2026-10-09/`. They are local and git-ignored, not a pushed or externally anchored archive. The final receipt records their hashes and the source diff.

A passing CTest entry does not mean all internal optional cases ran. The unchanged baseline's training entry discovered 408 cases and skipped 90 with its existing interpreter. Its singing-quality entry discovered 141 and skipped 19: the 12 packet003 native cases require explicit fixture/binary environment variables, while 7 other native lanes require their own inputs. Those 12 were exercised in the preceding dedicated W01 run; the default aggregate result does not rerun them. The production entry ran all 133 with no skips.

The pinned training environment and execution of dependency-gated model/export tests remain W03 follow-up work. Human listening, sample-bank production approval, macOS/Windows installed acceptance, host workloads and final release authority remain separate unmet product obligations.
