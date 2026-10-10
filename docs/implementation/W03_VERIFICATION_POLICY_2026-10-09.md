# W03 production-suite verification policy

Historical W03 scheduler checkpoint: the complete dependency-enabled Release configuration passed 235/235 under `-j6`, both with and without a training-specific serial reservation. That concurrent run executed all 469 training cases with zero skips. Scheduler disposition is recorded below; the current local scope after CI retirement is documented at the end of this file, with current receipts in the root execution ledger. This is engineering verification, not product/release acceptance; historical failures remain retained.

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


Final committed policy (after independent review): use `ctest --test-dir build/release -j6 --output-on-failure` with the training entry reserving CTest capacity through `RUN_SERIAL`. Keep the explicitly compiled locked interpreter, pinned source, verbose output and 120s limit. The reviewer approved checkpoint `40682e35` and requested this separate scheduler increment. The two saved registrations differ only in the training reservation after removing source backtraces. The compiled non-overlapped observations 78.88s (serial full suite) and 89.64s (isolated invocation) differ by 10.76s, exceeding the concurrent8.37s timeout margin. Both observed pairs show about30s additional training time with overlap (76.41→106.63 and78.88→111.63); 193 entries overlapped the latter run. Reserving capacity is a conservative choice from measured variance, not a claim that overlap exclusively caused historical timeouts. The full serial run cost19.26s (+5.6%) more. Current registration is checked against that retained passing serial registration; this policy-only increment needs no repeated full run. Other applications remain outside CTest's reservation.

Independent reviewer turn `01a11ee9-cf32-7fe1-91e2-7b66bd8ffeac` verified checkpoint `40682e35` and recommended this policy correction. Follow-up turn `01a11f08-5c25-7e42-9d83-78d9ee965132` verified pushed `e1873a7b`, independently compared the generated registration, and approved the correction. W03's supported dependency-enabled configuration is established. Product acceptance remains separate and incomplete.

## Local verification after CI retirement (2026-10-10)

Upstream `6632f584` removed all seven GitHub Actions workflows at the owner's
request. Local tests must not require or fabricate that deleted automation.
The supported Release build/CTest command above remains the local engineering
entrypoint. Its success does not prove CI orchestration, platform execution,
signed validation, installer operation or release eligibility.

The reconciliation makes these coverage changes explicit:

| Previous check | Current local coverage and limitation |
|---|---|
| Phase13A validator/auval workflow wiring, Linux runner package installation, signed packet preservation, signing-before-validation-before-packaging workflow order | Four CI-only test methods retired with the workflows. **No equivalent automated orchestration is claimed.** Actual ordered signed-installed evidence remains mandatory before release. |
| Validator attachment and clean-installer tests mixed with workflow token checks | All existing attachment-script and installer-evidence assertions retained; assertions about deleted YAML wiring removed. These are source checks, not executions of installers. |
| Credential eligibility mixed with CI secret-loading assertions | Local macOS/Windows signing and installer script order assertions retained. Removed CI credential-loading paths are no longer checked or claimed. No production credential is accessed by this test. |
| Soak workflow and native-runtime workflow token checks | Retained full/smoke source contract, actual invalid-profile/smoke probe, full-duration refusal tests and native CMake fixture entrypoint checks. Uploads, runner selection and automatic workload invocation are unverified. |
| VST3 packet command extracted from YAML | Existing packet CLI invoked directly from outside the artifact root with relative inputs; creation/verification succeeds and a changed validator is refused without publishing a packet. Fixture evidence is not actual VST3 validator acceptance. |
| Release-version checks over two workflow files | Retained checks over current application, packaging and evidence-generation surfaces; the two deleted YAML surfaces removed from that list. |
| Phase8 and Phase13A source verifier CLIs require YAML | Platform adapters, CMake selection, exact dependency locks, source signing checks, mandatory matrix and documentation contracts retained. Phase13A now also requires local VST3/auval/host-certification entrypoints. Missing required local inputs still fail. |

Phase13A reports `packagingSourceChecks=PASS`, `pipelineExecution=NOT_CHECKED`
and `externalRuntimeResults=NOT_RUN`; it no longer labels source checks as
`packagingPipelines=PASS`. Both source verifiers explicitly report GitHub Actions
as deferred. Three new CLI boundary tests check this reporting and refusal of
missing validator/host or native-adapter inputs. The six directly affected modules
therefore contain52 cases rather than53 (four retired, three added); this is not
coverage equivalence. No new conditional skips or relaxed runtime thresholds are
introduced. No release gate, mandatory target matrix or evidence validator is
removed. Exact candidate signing, validation after signing, clean installation,
nine host tuples and required long workloads still require their real evidence;
local green checks cannot supply it. A release operator must supply the required
ordered execution before candidate acceptance, independent of CI availability.

Independent technical review is pending while the authorized reviewer chat is
rate-limited. This does not authorize restoration of CI or waive product gates.
Verification receipts and current result are recorded in the root execution ledger.

## Native asynchronous functional waits (2026-10-10)

The source-binding checkpoint `9d0027b3` exposed two instrumented verification
limits, retained under `out/evidence/render-source-binding-2026-10-10`: the ASan
original-song aggregate reached its 300-second CTest limit in case 5, and the TSan
export suite failed its Studio batch-drain helper. These are distinct failures.
The aggregate song scheduling issue remains open; this increment does not change
its registration or timeout.

The batch helper made 3,000 polls separated by one-millisecond sleeps. A diagnostic
kept that original failure verdict while observing the same live worker afterward.
The timed TSan run exhausted the poll budget at 3.78704 seconds and reached terminal
success at 5.41479 seconds, with `GENERATED BATCH: MARKER REVIEW`. An earlier probe
also reached success, 1.76968 seconds after its original budget expired. Neither
probe is relabelled as passing the old criterion. These observations establish
completion for those runs; they do not establish a production latency guarantee
or prove instrumentation is the sole timing cause.

Batch functional completion now uses a **10-second steady-clock test budget**,
the bound already used by the export fixture's import wait. This deliberately
loosens this helper's liveness budget from the old approximately three-second
poll allowance, providing 4.58521 seconds of headroom over the timed observation.
It is not an equivalent timeout policy or a performance improvement. Workloads,
frame limits, marker/identity assertions, product performance criteria and acoustic
thresholds are unchanged. No case is skipped or replaced by a mock.

A harness deadline now throws `test::Failure`; it cannot masquerade as an ordinary
application error accepted by `CHECK(!result)`. The shared helper requires a real
terminal result, checks the deadline both before and after polling/readiness, and
refuses an error while the operation is still busy. Deterministic fake-clock tests
cover real success/refusal, a never-finishing worker, late success/error, late
readiness, nonterminal errors and invalid budgets. The other generation and
preparation helpers retain their 3,000-poll limits but now also throw on budget
exhaustion. Their negative-path assertions therefore require actual worker errors.

Independent review was initially PENDING because the authorized reviewer returned
HTTP429. Reviewer turn `01a12236-fba7-7752-b435-915dd847bce9` subsequently approved
`f71b33fb`; the root ledger records that disposition and its remaining P3 follow-up.
Current build/test outcomes belong in the root execution ledger. This is a test-harness correction, not installed-product,
model, listening, platform or release acceptance.


## Original-song sanitizer aggregate scheduling (2026-10-10)

The retained ASan/UBSan aggregate on `9d0027b3` timed out at300.08s after
four case bodies passed in42.8744,127.175,54.5343 and53.2203 seconds.
The remaining three bodies subsequently passed in35.292,17.966 and0.129 seconds
in separate invocations. A new diagnostic run on `f71b33fb` with an opt-in
compiled-registry runner passed all seven cases in344.16s; its slowest body took
121.724s. These observations establish a workload exceeding the old aggregate
allowance, not the cause of runtime variance or a performance improvement.

Independent review rejected landing that runner: separate processes lose
instrumented cross-case state coverage, the TSan per-case limit lacked direct
measurements, and output decoding/recoverable-UBSan handling needed correction.
The proposed runner and its fixtures were removed from the change. Their source,
ASan result, known-answer checks and deliberately aborted TSan diagnostic remain
local evidence; none constitutes the final aggregate verification result.

The existing test executable, ordinary main, all seven bodies, their ordering,
and shared process are retained. On macOS, ASan/UBSan now uses a900-second
aggregate timeout with `RUN_SERIAL`. The final single-process run passed7/7
in323.95s. The budget gives2.78 times that observation (576.05s remaining), and
2.62 times the344.16-second diagnostic. These are sequential busy-host
observations, not a controlled speed comparison. The retained host snapshot
showed6714.50MiB of8192MiB swap in use; memory pressure can affect these timings.
CTest reservation prevents other CTest entries from overlapping; other apps
remain outside it. The lead ran no overlapping build or test workload.

At checkpoint `96e252a4`, Release, TSan and non-Apple configurations retained300
seconds. TSan and non-Apple sanitizer budgets were explicitly unverified gaps.
The TSan follow-up below supplies its first complete measurement; non-Apple
sanitizer timing remains unverified. The aborted TSan diagnostic was still in case1 at134s,
versus41.39s for the ASan diagnostic. That earlier ratio approximation above3.2
was one observation under a different schedule/load, not a fixed workload
multiplier. The older paired ratio around2.8 likewise did not establish this
suite's TSan total; the completed aggregate below now supplies direct evidence. Non-Apple timing is unmeasured and
may also exceed300s; a timeout there does not alone establish a product defect.

This is an explicit ASan aggregate loosening from300 to900 seconds, not
unchanged timeout semantics. A hang or sanitizer abort still fails the entire
aggregate and leaves later cases unobserved. Existing flushed START/PASS lines
identify the active case and completed bodies. Separate-case diagnostics can
investigate a failure but cannot replace the required single-process aggregate
pass. No case, workload, acoustic criterion, frame-count assertion or product
acceptance threshold changes.

Sanitizer configurations also set CTest `FAIL_REGULAR_EXPRESSION` for
`runtime error:`, `ERROR: AddressSanitizer` and `WARNING: ThreadSanitizer`.
Recoverable UBSan output must fail even with exit0. Known-answer temporary CTest
emitters validate the actual generated property against each marker and clean
output, ordinary nonzero exits, timeout, stderr-only markers and a marker after
4MiB of stdout. This is scoped to output visible to this test: swallowed child output
and negative tests that accept any child failure remain separate verification
gaps. It is not a whole-project sanitizer-policy repair or leak-detection claim.

Final build/aggregate results, source and binary hashes, retained failures and
review disposition are recorded in the root execution ledger. No human,
installed-product, platform, learned-singer or Beta acceptance follows from this
engineering scheduling correction; Windows qualification remains TODO.


## Measured macOS TSan aggregate policy (2026-10-10)

On committed `96e252a4`, the rebuilt Debug/TSan original-song executable ran all
seven unchanged cases in one process. Before starting, the lead saved the300s
registration and locally changed only this generated CTest entry to a2400s
measurement ceiling with `RUN_SERIAL`. The command, working directory, visible
sanitizer-report rejection expression, executable and test source were recorded
and held unchanged. This was an explicit measurement override, not a claim that
`ctest --timeout` supersedes a test's own TIMEOUT property.

The prospective rule was `ceil(2.6 * complete_seconds / 300) * 300`. Following
review, a3600s upper threshold for automatic proposals was recorded **after case1
completed and before the full aggregate total**. The lead initially said this
threshold preceded case1; timestamp review corrected that statement, preserved
in `measurement-notes.md`. A failed measurement or a derived value above3600s
requires another scoped review, not automatic extension or splitting of cases.

**Result:** CTest exited0, passed all7 cases in955.44s, reported exactly the seven
expected PASS names in order and `7 passed, 0 failed`, and contained no visible
sanitizer-report markers. Executable and test-source SHA256 hashes were unchanged
at completion. Body times were114.078,330.814,151.520,199.508,100.528,57.9238 and
0.000346042 seconds. Tuning alone exceeded the old300s aggregate budget. The
first body was114.078s versus42.6273s in the prior single-process ASan run; the
older aborted per-case observation at134s elapsed remains a different, incomplete
run. These observations do not prove a fixed instrumentation multiplier or the
cause of timing variance.

The rule gives **2700s**, below the3600s review threshold:2.826 times the955.44s
measurement, with1744.56s headroom. CMake now sets that timeout and `RUN_SERIAL`
for this macOS TSan entry. This explicitly loosens the old300s aggregate ceiling
ninefold. ASan remains900s/serial; Release and non-Apple timeouts remain300s.
The existing shared-process coverage, every case, assertions and sanitizer-marker
rejection remain intact. A hang can now take longer to fail; no production latency,
acoustic, model-quality or acceptance threshold changes.

This budget rests on **one completed TSan aggregate measurement**; variance is
uncharacterized. The lead overlapped no build or other test suite, but other apps
were uncontrolled and host load changed during the run. Eighteen45-second samples
from elapsed03:02 through15:48 observed a peak RSS of492960KiB (481.41MiB), not a
proved whole-run peak. Sampled swap use ranged6279.44–6682.50M; it did not grow
above its6682.50M starting value in those observations. Heavy pre-existing swap
use and changing load are retained with the timings; neither decreasing swap use
nor these RSS samples prove an absence of paging or establish a performance cause.
A later timeout requires diagnosis and scoped review, not an automatic budget bump.

The final generated registration is compared with the passing measurement: only
the derived timeout may differ for TSan; command, working directory, marker checks
and serial scheduling must match, and the measured TSan executable hash must
remain unchanged. Release and ASan registrations must remain unchanged. Their
regeneration advanced the generated source-commit stamp from `f71b33fb` to
`96e252a4`, rebuilding dependent objects and changing those two executable hashes.
The freshly stamped Release binary passed7/7 in19.82s. The refreshed ASan run
was interrupted after three PASS bodies and the fourth START; it has no aggregate
verdict and remains pending a coordinated heavy-validation window. Earlier ASan
receipts are not assigned to this rebuilt binary. This interruption does not
invalidate the complete TSan measurement on its unchanged executable. The initial comparison incorrectly required
these hashes to equal the preceding unit's artifacts and failed; the corrected
comparison separates registration equality from artifact identity.
The passing TSan run used the stricter2400s measurement ceiling; no repeated
TSan run or full-matrix result is implied by the configuration comparison.
Raw evidence is local ignored `out/evidence/song-tsan-aggregate-2026-10-10/`; the
root ledger records final comparison and review disposition. This is scoped
engineering verification, not whole-project TSan, installed-product, human,
Windows/host or `EXTERNAL_BETA_READY` acceptance.


Resumption checkpoint: the reviewer confirmed the prospective measurement setup
and challenged the budget rule, but its final review turn was interrupted before
a final verdict on this TSan policy. Final independent review remains PENDING;
no replacement reviewer or approval is inferred. Shared-machine coordination now
requires an atomic cooperative validation lock, gives Dadum the next heavy window,
and preserves active PacePitch/other work. No new heavy checks were started merely
to replace the interrupted ASan receipt. The TSan policy is committed as a scoped
engineering increment; current-ASan requalification and product acceptance stay open.
