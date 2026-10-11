# Repository staleness remediation: code-level development tasks

> **Follow-up completion — 2026-10-12.** All 11 task dispositions are closed: nine repairs/documentation tasks COMPLETE, RS-10 and RS-11 NO_CHANGE_RECOMMENDED after current source review and controlled measurement. The previously pending installed-recipe operation is fixed and pushed at `95d093b1`, with its original peak threshold retained; RS-09 is pushed at `6f8da65c`. See the [current completion report](REPOSITORY_STALENESS_REMEDIATION_COMPLETION_2026-10-12.md) for source traces, exact checks and the boundary from product acceptance. Earlier pending/deferred statements and preservation restrictions below are dated planning history; the user subsequently authorized completing that operation.

> **Execution update — 2026-10-11.** Mandatory RS-01–RS-08 are implemented, locally verified and pushed. See [completion record](REPOSITORY_STALENESS_REMEDIATION_EXECUTION_2026-10-11.md) for exact commits/checks, the preserved pending test failure and conditional dispositions. The original planning baseline and criteria below remain historical planning context; no product/Beta acceptance is promoted.

> **Completion-audit supplement — 2026-10-11.** RS-02's previously missing sanitizer checks now pass after a test-only symbolizer correction at `2d739855`. Normal CTest in a clean committed checkout also passes all five selected suites, including CLI 24/24. The completion record preserves the initial sanitizer failures and the separate unfinished-feature failure.

Date: 2026-10-11, Asia/Seoul. Planning baseline: `6ba6d60998d290e1862e1c37d425a08fde083362`, `master`.

Repository root: `/Users/lhs/Downloads/project-seam-usable-alpha-u3-master`.

This document converts the [Repository staleness and structural audit](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/reviews/REPOSITORY_STALENESS_AND_STRUCTURE_AUDIT_2026-10-11.md) into implementation tasks. It is a development reference, not a record that the repairs have been implemented. The accompanying [task manifest](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/implementation/REPOSITORY_STALENESS_REMEDIATION_TASKS_2026-10-11.tasks.json) provides stable IDs, dependencies, source references, and completion criteria.

## Intended outcome and execution boundary

Complete the eight repair tasks RS-01 through RS-08 so that current project exports reach the supported capture consumer, expected CLI refusals are distinguishable from crashes, current documentation and evidence describe the actual implementation, and tool/evidence lifecycle boundaries are explicit. Preserve historical readability, provenance, bounded execution, and existing admission holds.

RS-09 through RS-11 are conditional structural investigations. They are outside the mandatory repair outcome until their entry conditions are demonstrated. A useful outcome for one of these investigations may be a documented decision to keep the current structure. This plan does not authorize a wholesale rewrite or require every large source file to be split.

All tasks are **unimplemented in this planning change**. “READY” means the task has enough definition to start once development is requested; it does not mean code-complete or product-approved. No runtime verification command below was executed as part of authoring this document. Existing audit test results remain evidence of the old behavior, including the known schema rejection.

The repository was refreshed with `git fetch origin` and `git pull --ff-only --no-rebase`; pull reported already up to date. All 55 source files cited by the audit still match its recorded committed hashes. Two dirty implementation files remain byte-for-byte unchanged:

| Existing unfinished file | Integration rule |
|---|---|
| [candidate_package_commands.cpp](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/apps/seam-voicebank-cli/candidate_package_commands.cpp) | Contains pending installed-recipe export observation work. Do not absorb or rewrite it under this plan. |
| [test_sample_review_cli.cpp](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_sample_review_cli.cpp) | Contains associated tests and an uncommitted shell-based refusal helper. RS-02 touches the same file; preserve that work and reconcile deliberately. |

Before implementation, inspect the live diff again. A clean isolated checkout can support unrelated work, but it does not make pending edits disappear or authorize their integration. Findings and source references below describe the committed baseline unless explicitly labeled pending.

## Task map and order

| ID | Task | Audit coverage | Initial state | Completion dependency |
|---|---|---|---|---|
| RS-01 | Restore current export-to-capture compatibility | F01 | READY | None |
| RS-02 | Preserve child termination details and strengthen CLI refusal tests | F06 | READY; shared-file integration required | None |
| RS-03 | Reconcile deferred-CI evidence state | F02 | READY | None |
| RS-04 | Establish current documentation and API entrypoints | F03, F04, F07 | READY to draft | RS-03 and RS-05 before final consistency check |
| RS-05 | Correct neural version and launch guidance | F05, F07 | READY | None |
| RS-06 | Make singer-pilot help side-effect free | F09 | READY | None; shares a test file with RS-01 |
| RS-07 | Classify historical experiment tools and supported replacements | F08 | READY | None |
| RS-08 | Make historical evidence availability explicit | F10 | READY | None |
| RS-09 | Consolidate one demonstrated UI geometry boundary | S02 | CONDITIONAL | Concrete geometry problem plus bounded reproducer |
| RS-10 | Assess one runtime/dependency boundary using existing admission types | S01 | CONDITIONAL | Concrete repeated cross-layer change |
| RS-11 | Measure build/verification cost before optimizing | S03 | CONDITIONAL | A representative slow feedback loop to measure |

Recommended sequence: RS-01, RS-02, RS-03, RS-05, RS-04, then RS-06 through RS-08. RS-03/RS-05 and documentation drafting can proceed independently of the first two, but one integrator should own changes to shared documents and test files. This is a logical order, not a request to start parallel sessions.

Each task below defines small commit boundaries. Follow [AGENTS.md](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/AGENTS.md): commit verified units, push promptly, inspect the staged diff, and verify local HEAD, `origin/master`, and the live remote. Preserve `.github`, Windows TODO, unrelated stashes, and the root-owned implementation ledger. Update the ledger only when integrating completed work; writing this plan is not such completion.

## RS-01 — Restore current export-to-capture compatibility

**Owner:** formats / captured-data tooling. **Priority:** P1. **Result:** a current supported native export can be captured without weakening provenance or inventing training approval.

**Read and edit boundaries.** The current writer is [ProjectJsonCodec::kSchemaVersion](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-formats/include/seam/formats/project_json.hpp:24). The [export service](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-authoring-runtime/src/export_service.cpp:689) encodes the project and packaged project through it. The primary edit is [capture_inputs](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/voice_model_training/prepare_captured_teacher.py:74); preserve its existing five-element return tuple and the later `prepare_bundle` behavior unless a separately justified migration is needed. Direct consumers include [prepare_corpus](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/voice_model_training/prepare_corpus.py) and [render_frozen_evaluation](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/voice_model_training/render_frozen_evaluation.py:65).

The existing version predicate accepts 17–19. The repair must cover the selected track/region semantics of [schema 20](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/formats/PROJECT_JSON_V20.md) and [schema 21](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/formats/PROJECT_JSON_V21.md), using the [native installation decoder](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-formats/src/project_json.cpp:32) and lyric decoding as the source of truth. Do not implement a second complete project codec in Python.

**Required behavior.**

| Input | Required capture behavior |
|---|---|
| Valid 17/18/19 captured procedural teacher | Preserve current supported behavior and historical output derivation. |
| Schema 20 lyric | Require `readingHint`; accept null or a valid 1–4096 Unicode-scalar string; reject missing/wrong-type/invalid-Unicode values. |
| Schema 21 non-null procedural reference | Require `installation`; accept null or the exact eight-field metadata shape defined by the native codec/domain validation. |
| Bound portable recipe copy | Retain original project hash and recipe-byte binding. Do not require the source machine's original installed directory. |
| Unsupported future schema, including 22 at this baseline | Explicit unsupported-version refusal before publishing a complete preparation. |
| Non-integer schema, including JSON boolean | Refuse; Python `bool` must not pass an integer check. |
| Sample StyleBlend pair | Preserve the existing procedural-teacher refusal. |
| Modified receipt/project/candidate/recipe/WAV or unsafe path | Preserve existing refusal and no-complete-receipt behavior. |

For non-null installation metadata, validate required strings, exact keys, lowercase digest shapes, allowed empty receipt pins, positive uint32 engine revision, and normalized relative `recipeEntry`. Use the native/domain rules rather than substituting generic URL/path validation. The installed content identity is distinct from the recipe hash. Parsing these fields does not validate a signature, admit an installation, or approve training rights.

**Pronunciation and label contract.** Keep displayed `surface` as the lyric text. A `readingHint` is pronunciation intent, not replacement display text. Captured candidate markers already provide the actual phones used for the audio; do not invoke another phonemizer or regenerate labels from a hint. Keep note-level `phoneticHint == "pau"` handling and phone agreement checks. The receipt-bound original project hash binds the reading hint and installation metadata; do not silently strip or rewrite the original project to make it look like schema 19.

The current [generated-teacher conversion](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/voice_model_training/generated_teacher.py:268) consumes captured phones and supplied lyric/midi information. The [phrase fingerprint](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/voice_model_training/phrase_fingerprint.py:56) intentionally describes musical content separately from performance. Preserve that fingerprint meaning; adding reading hints to its hashed fields would invalidate frozen plans and requires a separate versioned change.

**Implementation steps and commit units.**

1. Add small private validation helpers for supported project version and selected lyric/recipe metadata. Validate selected content before `prepare_bundle` creates output. Keep old-schema behavior, tuple shape, provenance key meaning, and all approval flags unchanged. Commit with focused Python tests.
2. Replace the test that treats schema 20 as categorically unsupported with a real compatibility matrix. Populate required fields for each schema; changing only `schemaVersion` is not a valid success fixture. Include non-null reading hints and non-null installation metadata, not just null fields. Commit together with any necessary bounded consumer correction.
3. Extend [test_singer_pilot_cli.py](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_singer_pilot_cli.py:52) to pass one already-generated baseline export into `capture_inputs`. Recompute the actual receipt hash, select its actual candidate path, and assert matching project/audio identities. Reuse its existing first run instead of rendering another full packet. Ensure repository imports work when CTest runs the script from the build directory. This test must use the rebuilt current native writer and must not patch subprocess execution or edit its emitted schema.

The native boundary test should assert the expected current schema explicitly. A future writer bump should require a conscious consumer update, not silently change the compatibility promise. Handcrafted older fixtures remain useful migration tests; they cannot replace the current-writer test.

**Tests and completion.** Use V01 and V02 below. Add cases for missing/null/string/oversized/surrogate reading hints; malformed/missing/unknown installation members; bool/fractional/zero/overflowing engine revisions; unsafe recipe entries; allowed empty pins; preserved package-independent capture; tampering and unsupported sample selection. A non-null reading hint case must retain the display lyric and captured phones. A successful preparation remains `PREPARED_UNAPPROVED` with rights, labels, training, and release flags false. Historical 17–19 `verify_existing` derivation must remain compatible.

**Done when:** Python compatibility tests and the current native producer/consumer test pass against the identified build; supported frozen-evaluation capture no longer fails solely on schema 20/21; unsupported and tampered inputs still fail for their intended reasons. A mocked frozen-evaluation test alone cannot close this task. Full corpus generation, training, listening qualification, and migration of old frozen campaign identities are outside scope.

## RS-02 — Preserve child outcomes without weakening supervision

**Owner:** platform runtime / CLI tests. **Priority:** P2. **Result:** callers can distinguish an expected exit code from a signal/crash while existing success-only callers retain their behavior.

**Read and edit boundaries.** Start at [HelperProcessOutput and runBoundedHelperProcess](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-platform/include/seam/platform/helper_process.hpp:29), the [POSIX completion branch](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-platform/src/helper_process.cpp:441), and the [Windows completion/cleanup branch](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-platform/src/helper_process.cpp:544). The [authoring header](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-authoring-runtime/include/seam/authoring/helper_process.hpp) is an alias, not another process supervisor.

The audit's output-loss finding is strongest on POSIX. Windows already attaches captured stderr to an error context at its failure return, although it still does not provide a typed termination outcome. Preserve that useful behavior in the compatibility wrapper.

**Proposed API shape, not existing symbols.** Names may follow local conventions; semantics are required:

```cpp
struct HelperExited { std::uint32_t code; };
struct HelperSignaled { int signal; };  // POSIX only; never invent a Windows signal.
struct CompletedHelperProcess {
  HelperProcessOutput output;
  std::variant<HelperExited, HelperSignaled> termination;
};

core::Result<CompletedHelperProcess> runBoundedHelperProcessObserved(
    const HelperProcessRequest&, std::stop_token = {});
// Existing runBoundedHelperProcess remains a success-only compatibility wrapper.
```

A successful `Result<CompletedHelperProcess>` means bounded supervision completed and termination details are available. It does **not** mean exit code zero. On Windows retain the full unsigned exit code, including exception-like status values, without narrowing to a POSIX-sized code. On POSIX use `WIFEXITED/WEXITSTATUS` and `WIFSIGNALED/WTERMSIG` after successful reaping.

Launch failure, cancellation, timeout, resource-ceiling breach, overflow, pipe I/O failure, incomplete required stdin delivery, or reaping failure remain `Result` errors. A child that exits before required input delivery must not become a successful observed result just because its exit code is available. Preserve request validation, bounded streams, environment/descriptor isolation, no shell/PATH lookup, process-tree cleanup, and stop-token behavior.

Implement supervision once. Adapt the existing success-only function to call the observed function, accept only `HelperExited{0}`, and preserve existing error-code/message/context behavior where callers rely on it. Do not copy the entire POSIX/Windows supervisor into a second function or change every production caller merely to use the new type.

**Implementation steps and commit units.**

1. Add the observed result and shared implementation, retaining the old public API. Update both platform branches and aliases where needed. Extend [test_helper_process.cpp](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_helper_process.cpp) and the existing [reading_process_probe](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/helpers/reading_process_probe.cpp). Commit after V03.
2. Convert targeted refusal assertions in the committed [test_sample_review_cli.cpp](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_sample_review_cli.cpp:47) to assert an exact expected exit and stable diagnostic fragment, plus the existing filesystem invariants. Preserve positive JSON/report assertions. Commit after the CLI subset in V03.

The pending working-tree `cliRefusal` helper currently wraps a child with `/bin/sh` to retain exit text. That is evidence of the need, not a landed API. Reconcile it after its containing work is integrated or by a separately reviewed hunk-level merge. Do not overwrite it, count it as RS-02 completion, or move the shell workaround into production. The observed API should eventually make this wrapper unnecessary.

**Negative-test oracle.** An intended refusal passes only if supervision succeeded, termination is an ordinary exit with the command's documented code, the expected diagnostic is present in the intended stream, sanitizer-failure markers are absent, and no forbidden output was created or altered. Read each CLI branch before selecting the code: voicebank-CLI refusal helpers and singer-pilot do not necessarily use the same error code.

| Probe/test | Required assertion |
|---|---|
| Exit 0 with two streams | Exact stdout/stderr; old wrapper succeeds. |
| Exit 7 with partial stdout and stderr | Observed API returns both streams and code 7; old wrapper fails. |
| Deliberate POSIX signal with empty required input | Signal is reported distinctly; it cannot satisfy an expected exit-2 test. |
| Windows ordinary nonzero exit | Full code and bounded output survive; Windows runtime result stays unverified until actually run. |
| Output flood, timeout, cancellation, inherited descendant pipe | Existing bounds and process-tree cleanup still hold; no conversion into an ordinary completed refusal. |
| Expected refusal diagnostic plus sanitizer marker | Test helper rejects it even if the exit code happens to match. |

**Done when:** the observed API and compatibility wrapper have distinct, tested semantics; selected CLI negatives prove their intended refusal; no duplicate supervisor or shell execution path was added. Use V03, with a full dependent rebuild before CTest when library code changes. macOS results do not close Windows execution acceptance.

## RS-03 — Reconcile deferred-CI evidence without changing release gates

**Owner:** release tooling. **Priority:** P2. **Result:** current evidence distinguishes source readiness from automation and runtime execution.

**Files and symbols.** Edit the final evidence construction in [generate_phase13a_evidence.py](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/scripts/generate_phase13a_evidence.py:80), current target states in the [Phase 13A matrix](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/phase13a/mandatory-validation-matrix.json), and the relevant checks in [verify_phase13a_contracts.py](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/scripts/verify_phase13a_contracts.py:48). Read [release_gate.IMPLEMENTATION_STATES / evaluate_matrix](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/phase13a/release_gate.py:11) before changing any state value.

`SOURCE_READY` already exists. Use it for retained local implementation where warranted; use `NOT_STARTED` if no corresponding source implementation exists. Review the affected rows individually. The audit's 14 text occurrences include an enum declaration, not 14 live target claims. Do not perform a repository-wide substitution of `CI_CONFIGURED`.

Keep `CI_CONFIGURED` as an understood historical/possible state in the parser and fixtures. Keep `TARGET_BUILD_PASS` evidence semantics. Do not introduce a new `implementationState: DEFERRED` that the evaluator rejects. Deferred automation can be explained in current documentation/checker output without widening the matrix schema.

**Implementation steps.**

1. Extract the final evidence dictionary construction into a small pure helper if needed to test it without building/packaging. Feed it already-obtained results; do not make its extraction bypass package-smoke failure or invent local success. Proposed unit fixtures are test data, never real execution receipts.
2. Set truthful implementation state in generated local evidence and review current matrix rows against retained scripts. Leave target IDs, categories, mandatory gate membership, runtime results, evidence records, and `acceptanceStatus: BLOCKED` intact.
3. Add a regression that checks the current checked-in matrix and generated summary agree with deferred orchestration. Keep generic evaluator tests for all valid states, including historical `CI_CONFIGURED`.

**Tests:** extend [test_release_gate.py](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/phase13a/test_release_gate.py) or add a narrowly named evidence-builder test under the same directory. Inspect [release identity tests](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/external_beta/test_release_identity.py:78) so the generator still derives the source version correctly. Use V04. Exercise the pure builder with fixtures; do not invoke the Linux packaging generator on macOS just to inspect its status string.

**Done when:** the current matrix and generator no longer imply configured GitHub jobs, all G2–G5 mandatory evidence obligations remain unchanged, and `--expect-blocked` gate checks succeed because the baseline is blocked without schema errors. Commit the coordinated source/matrix/tests as one unit. Restoring CI or fabricating target PASS is outside scope.

## RS-04 — Establish a current documentation and API entrypoint

**Owner:** integrating developer. **Priority:** P2. **Result:** a developer can identify the current app, supported development route, API lifecycle, and remaining acceptance boundaries from one entrypoint.

**Files.** Update [README.md](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/README.md:51) and [DESIGN.md](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/DESIGN.md:5). Add a concise engineering index at the proposed path `/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/implementation/CURRENT_ENGINEERING_ENTRYPOINTS.md`. Mark [STATUS_KO.md](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/STATUS_KO.md), [REMAINING_TASKS_KO.md](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/REMAINING_TASKS_KO.md), and [RELEASE_READINESS_KO.md](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/RELEASE_READINESS_KO.md) as dated historical material where they are linked as current. Preserve their observations rather than rewriting old results into new results.

**Index content must come from source.**

| Surface | Current source of truth | Required classification |
|---|---|---|
| Main editor | [SING shell](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/design/sing_shell.cpp), [standalone presenter](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-standalone/src/native_editor_app.cpp:2094), [CLAP presenter](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-clap-editor/src/editor_runtime_paint.cpp:116) | Current macOS editor; unavailable-editor fallback is not a classic editor. |
| Project format | [ProjectJsonCodec](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-formats/include/seam/formats/project_json.hpp:22) | Writer 21, compatibility readers 1–21; frozen identity migration is a separate concern. |
| Production campaigns | [advanceGenerationRequest](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-authoring-runtime/include/seam/authoring/generation_campaign.hpp:116) | Production durable lifecycle. |
| Legacy campaign entrypoint | [advanceGenerationCampaign](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-authoring-runtime/include/seam/authoring/generation_campaign.hpp:81) | Compatibility/test consumers; no durable terminal-record guarantee. |
| Neural execution | RS-05's version matrix and production runner | Current bundle path versus legacy probes. |
| U45 typed audit | [full_product_gate.py](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/external_beta/full_product_gate.py:1) | Implemented engineering adapter, canonical admission still held. |
| Listening tools | RS-07's lifecycle inventory | Supported diagnostic command versus archived experiment; no human acceptance implied. |

For the main editor, root design rules must follow the current [UI fidelity contract](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/design/ui-fidelity-contract-v1.json). Retain separately applicable Studio/raster utilities. Do not remove reusable rendering code on the assumption that an obsolete design document proves it dead.

The macOS quickstart must match [CMakePresets.json](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/CMakePresets.json) and the [app-bundle target](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/CMakeLists.txt:998). Show configure/build and the actual configured bundle location, plus prerequisites that the source enforces. Keep X11 instructions explicitly historical/platform-specific. Only claim that a launch command was run when it was actually run; source-checked instructions are a narrower result.

**Commit units:** current README/design routing, then the small API/lifecycle index and historical notices. Final consistency review depends on RS-03 and RS-05. Do not make document ownership depend on editing multiple status ledgers on every code commit.

**Done when:** root docs point to one current engineering route; every public API listed has a source symbol, caller purpose, and compatibility/admission label; Windows TODO and missing non-macOS vector backend remain explicit; old CI observations are dated; U45 is not promoted. Use V00 and source review. No new unit tests are needed for wording-only edits.

## RS-05 — Correct neural version guidance and production examples

**Owner:** neural runtime. **Priority:** P2. **Result:** an integrator can construct the current production path without confusing version dimensions.

**Files and current behavior.** Update [NEURAL_PHONETIC_CONDITIONING.md](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/formats/NEURAL_PHONETIC_CONDITIONING.md), the contradictory paragraph in [tools/neural_runtime/README.md](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/neural_runtime/README.md:327), and the protocol-2 comment on [NeuralWorkerRunOptions::inferenceSteps](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-neural-synthesis/include/seam/neural_synthesis/neural_phrase_backend.hpp:48). Source checks are [decodeRequest](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-neural-synthesis/src/worker_protocol.cpp:311), [runNeuralWorker / runNeuralBundleWorker](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-neural-synthesis/src/neural_phrase_backend.cpp:348), and [the production runner's validation](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-authoring-runtime/src/neural_phrase_runner.cpp:23).

Publish this behavior table, checking it against source at integration time:

| Dimension | Values and meaning |
|---|---|
| SNW1 binary framing | Version 1; independent of all versions below. |
| Request/response metadata kind | v1 legacy, v2 conditioned, v3 bundle-bound. |
| Helper package launch protocol | 1/2/3 parseable; parseability is not render admission. |
| Legacy `runNeuralWorker` | Protocol 1 and no bundle-bound request. |
| Production `runNeuralBundleWorker` | Protocol 3, bundle-bound metadata, explicit 1–1000 inference steps, admitted identities and nonzero budgets. |

Document where trusted options come from: deployment resolution binds the helper/module/dependencies; production runner construction checks protocol/budgets; the render execution supplies inference steps. Do not recommend setting a raw helper path and hashing it locally as self-authorization. Keep request identity, bundle hash, and response identity checks distinct.

Do not globally change `NeuralWorkerRunOptions` defaults from protocol 1 to 3: legacy probe callers use them intentionally, and changing a default would not supply the missing budgets or admission. Existing production validation already rejects the wrong combination. A new options factory is **not required** for this task; consider one later only if a concrete new caller duplicates production construction incorrectly.

**Test references:** [test_neural_worker_protocol.cpp](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_neural_worker_protocol.cpp:929) covers package protocol parsing/resolution; [test_neural_phrase_runner.cpp](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_neural_phrase_runner.cpp:381) covers legacy/unbudgeted production refusal. Add behavioral tests only if runtime behavior changes. V05 is required for such a change; documentation/comment-only work uses V00 and direct source comparison.

**Done when:** top-level “current” statements, package resolution, production launch syntax, comments, and lifecycle index agree. Legacy decoder/probe behavior remains intact. Historical v2 narrative can remain in a clearly dated migration section. Commit this as one focused documentation/comment unit unless a separately justified runtime change is necessary.

## RS-06 — Make singer-pilot help side-effect free

**Owner:** singer-pilot CLI. **Priority:** P3. **Result:** normal CLI discovery prints usage and never starts rendering or creates a packet.

**Primary edit:** [apps/seam-singer-pilot/main.cpp](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/apps/seam-singer-pilot/main.cpp:28). The current positional parser accepts `argc == 2` and resolves `argv[1]` as an output path. Add help handling before project construction and any filesystem output operation.

**Chosen behavior for this task:** `--help` and `-h` as the sole argument print the same usage text and exit 0. Unknown leading options, missing required positional arguments, or extra arguments with a help option produce a usage error with the existing failure exit 1 and no output directory. Keep all existing valid positional modes. If a user intentionally wants an output directory whose name begins with `-`, the explicit relative spelling `./-name` is sufficient; a new general option parser or command grammar is unnecessary.

Factor usage text once so success-help and error paths do not drift. Keep validation before writes and preserve the existing no-overwrite behavior. Do not touch synthesis, audio diagnostics, or mode semantics.

**Tests:** extend the existing [pilot CLI test](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_singer_pilot_cli.py) in a temporary working directory. Check exit code, usage text, and unchanged directory inventory for `--help`, `-h`, `--unknown`, and `--help extra`. Keep valid default/mode runs, determinism repeats, invalid phrase refusals, and no-overwrite assertions. This test file overlaps RS-01; integrate its additions rather than replacing the harness.

**Done when:** V02 passes with the new no-side-effect discovery assertions and existing valid modes. Commit application parser and tests together. Do not run the old binary with `--help` to explore its behavior before the repair.

## RS-07 — Classify historical scripts instead of porting every experiment

**Owner:** singing-quality / training tooling. **Priority:** P3. **Result:** developers know which commands are supported and which require an exact historical workstation/artifact snapshot.

**Minimum inventory:** [build_listener_packet_002.py](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/singing_quality/build_listener_packet_002.py:25), its [historical guide](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/implementation/LISTENING_PACKET_002_GUIDE_2026-10-04.md), the current [listener-packet README](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/singing_quality/README.md:147), and [crossover](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/voice_model_training/acoustic_encoder_denoiser_crossover.py:170), [sampler trace](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/voice_model_training/acoustic_sampler_trace.py:243), and [gradient probe](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/voice_model_training/acoustic_gradient_probe.py:201).

Default disposition: packet 002 and the fixed-checkpoint experiments are archived reproduction tools; packet 003 is the current explicit-input diagnostic route, with its documented partial/Q1 limitation. Add a short lifecycle table to the existing tool READMEs and the current engineering index. Record required fixed inputs, output behavior, source revision, and supported replacement where one exists.

Do not import packet 002 to discover its interface: it creates output and runs top-level experiment code. Mark it as not import-safe. Do not silently replace its missing corpus or checkpoint with newer data. Preserve archived packet bytes and their source snapshots; adding a lifecycle notice does not retroactively change a captured script's hash.

Only if a specific historical tool is deliberately retained as a supported command should a follow-up unit move work into `main(argv)`, add explicit input/checkpoint/output arguments, validate inputs before creating output, and refuse overwrite. That port is not necessary to close the inventory task and must have its own behavior tests. Do not build a generic experiment framework for this cleanup.

**Done when:** each named tool has an explicit lifecycle and input contract, active documentation no longer presents fixed local paths as a portable default, and current packet guidance still admits its Q1 failure. Use V00 and static source inspection. No training or listening packet generation is required.

## RS-08 — Preserve history while exposing unavailable evidence

**Owner:** engineering documentation. **Priority:** P3. **Result:** current claims can be traced to available evidence, and unavailable historical attachments are honestly identified.

Use the audit's [evidence manifest](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/reviews/REPOSITORY_STALENESS_AND_STRUCTURE_AUDIT_2026-10-11.evidence.json) as the starting inventory: six unresolved inline-link occurrences represent five unique target files. Recheck existence before editing; do not assume an artifact remains missing.

Affected reports are the [September 22 finishing plan](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/SEAM_PROGRESS_AND_FINISHING_PLAN_2026-09-22_KO.md:52), [September 12 roadmap](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/SEAM_PROGRESS_AND_REVISED_ROADMAP_2026-09-12.md:50), and [September 5 feasibility roadmap](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/VIRTUAL_SINGER_FEASIBILITY_AND_CODE_ROADMAP_2026-09-05.md:289).

Add a dated availability note or linked availability index recording original target, referring source/line, last checked date, and `AVAILABLE` or `UNAVAILABLE`. If recovered bytes have an original recorded hash, verify that hash. If no independent original hash exists, label recovered bytes unverified; their mere existence does not authenticate the old result. Keep original observations distinguishable from the availability note.

For future evidence cited as current, retain source revision, command, exit/result, artifact identity/hash, and a durable location suited to its size. Do not commit large audio/build outputs indiscriminately. A git-ignored local directory is not a pushed immutable archive. No new storage service is required by this task.

**Done when:** all five historical targets have a documented current disposition, current summaries do not rely on unavailable links without saying so, and no newly generated file is presented as the missing historical PASS. Use V00 and the same bounded inline-link scan with its limitations. `SOURCE_CLOSURE=PASS` is useful but does not prove every document link or external artifact exists.

## RS-09 — Conditional: consolidate one UI geometry boundary

**Entry condition:** a reproduced pointer/text-input/accessibility/resize mismatch or a repeated change that requires editing the same coordinate rule in multiple places. Large file size alone does not satisfy it. Record the trigger before changing production code.

**Existing structure to preserve.** [TextInputRequest / TextInputAnchor](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/include/seam/native_ui/editor_controller.hpp:104) uses controller/classic-layout bounds; [SingShell::fromLegacy / toLegacy / translateTextInput](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/design/sing_shell.cpp:872) translates them. The shell already has `SingLayout`, `prepareFrame`, `hostedGeometry`, and shared field-bound helpers. Reuse these before inventing another frame-state model.

**Bounded implementation candidate:** extract one pure presented-geometry resolver for a selected field family, taking immutable prepared layout plus the controller anchor. Return shell-logical bounds, not backing-pixel bounds. Route painting, native text-field placement, and the corresponding semantic node through that result where they describe the same field. Keep native backing-scale conversion at the platform boundary. Treat unpresented/unavailable shells and zero-size bounds explicitly.

`translateTextInput` currently also changes `lyricInputActive_`, `fieldAnchor_`, and `fieldBounds_`. Keep those composition/presentation transitions in their lifecycle owner; a pure bounds helper must not silently reset composition or replace `textInputEnded` behavior. Separate geometry calculation from those effects before sharing it with paint or accessibility.

Do not rename/remove `ClassicSurface` globally during the first unit. Keep the compatibility adapter for unconverted consumers. Preserve `prepareFrame` ordering, gesture cancellation on resize/hide, focus identity, undo behavior, and note-grid viewport mapping. Do not claim the legacy geometry contract is retired if it still exists elsewhere.

**Tests and evidence:** use existing [shell input tests](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_design_shell_input.cpp:262), [frame pipeline tests](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_design_frame_pipeline.cpp), and [CLAP shell tests](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_clap_design_shell.cpp). Cover the chosen field at 1x/2x, resized viewport, scrolling where relevant, native composition start/end, and semantic-action equivalence. Use V06 after a full rebuild. Automated semantic geometry is not VoiceOver acceptance.

**Exit:** either the bounded fix removes a demonstrated duplicate rule and preserves interaction behavior, or the investigation records why the existing adapter is sufficient and remains deferred. A new collection of files with unchanged ownership does not count as a structural improvement.

## RS-10 — Conditional: assess one runtime/dependency boundary

**Entry condition:** a concrete recurring feature/defect change crosses UI, installation policy, and rendering in a way that cannot be explained by the feature itself. The audit's acyclic graph and fan-out counts are context, not sufficient cause to refactor.

An admitted-resource abstraction already exists: [AdmittedProceduralRecipe](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-distribution/include/seam/distribution/procedural_package.hpp:276) has restricted construction and binds reference/resource identity. [TrackProceduralSource and captureProceduralSource](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-rendering/include/seam/rendering/project_renderer.hpp:35) already carry request-local admission with immutable resource bytes. The [schema-21 runtime contract](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/formats/PROJECT_JSON_V21.md) describes re-admission before cache use and portable frozen jobs.

**Investigation:** trace one request from source selection through `captureProceduralSource`, snapshot creation, render/export, and cache lookup. List actual filesystem reads, admission decisions, and identity comparisons. Identify duplicated work or a concrete policy dependency that could be replaced by an existing immutable value. Check [render_snapshot.cpp](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-rendering/src/render_snapshot.cpp) and the relevant caller rather than inferring behavior from `target_link_libraries` alone.

If a change is warranted, limit the first unit to one call chain and one repeated policy decision. Keep the existing admission type or narrow its public include surface; do not introduce a second “verified” token with public construction. Preserve the distinction between bound absolute installation, portable bound copy, authored unbound source, and explicit permissive policy. Do not trust a cache hit before current file-backed admission or reopen a frozen source merely to rebuild an identity.

**Exit:** a source-level before/after trace shows fewer repeated decisions or a narrower dependency for the selected operation, with unchanged trust/provenance/refusal behavior and focused render/export tests. If the existing abstraction already supplies the needed boundary, record `NO_CHANGE_RECOMMENDED` and leave broader graph cleanup out of scope. Test selection must follow the selected call chain; do not prescribe an unrelated entire release suite in advance.

## RS-11 — Conditional: measure verification cost before changing it

**Entry condition:** a named developer feedback loop is slow enough to investigate. Record the exact operation and observed delay; do not infer duration from file count or cached compile-command count.

**Source:** [CMake source identity logic](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/CMakeLists.txt:15), [generated version header](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/CMakeLists.txt:218), and [test_cmake_source_commit.py](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_cmake_source_commit.py). The audit's 882 compile entries and repeated test harness are cached observations, not a timing baseline.

Use a scratch checkout/build for any experiment requiring a temporary code edit or commit. Preserve the shared dirty checkout and existing artifacts. Measure one warmed no-op build, one representative leaf edit, one shared-header change, and one source-identity-only commit when those cases match the complaint. Record revision, compiler/generator, build options, parallelism, machine load, wall time, commands actually rerun, and selected test durations. Measure comparable before/after conditions; repeat only when noise prevents a conclusion.

Do not manufacture a “docs-only commit” by overriding `SEAM_SOURCE_COMMIT` with a stale value. Do not reset the user's branch to create timing cases. Use build-system explanation tools on the scratch configuration to attribute rebuilds, and retain a compact result rather than every compiler line.

Possible follow-up optimizations include reducing redundant test compilation or separating development identity work from release identity work. Choose one only after measurement. Preserve test discovery/coverage and exact release-source identity; do not inflate timeouts, cache evaluators, disable failed tests, or remove mandatory checks merely to improve the number. Any source-identity change must pass V07 and prove the built candidate's identity against its source revision.

**Exit:** either one measured optimization improves the named feedback loop without weakening invariants, or a clear measurement rules out the suspected cause. This is not a prerequisite for RS-01–RS-08.

## Verification recipes

The following commands are future implementation recipes, not test receipts for this planning turn. Run from the repository root shown above. Resolve actual Python/native dependency prerequisites from the configured build. A skipped test or missing CTest registration is not a pass. Use `ctest --test-dir build/release -N -R '...'` to confirm the expected set before execution, and record the actual commands/results.

| ID | When to use | Commands / required check |
|---|---|---|
| V00 | Every completed unit; documentation-only work | `git diff --check`; validate new local links and source anchors; `python3 -B scripts/verify_tracked_source_closure.py --root .` after new deliverables are staged. Inspect staged paths before committing. |
| V01 | RS-01 Python boundary | `python3 -B -m unittest tools.voice_model_training.test_prepare_captured_teacher tools.voice_model_training.test_prepare_corpus tools.voice_model_training.test_render_frozen_evaluation tools.voice_model_training.test_phrase_fingerprint -v`. Record skips; do not count missing optional dependencies as successful coverage. |
| V02 | RS-01 native boundary; RS-06 CLI | Rebuild the current `seam_singer_pilot` and all its dependencies with `cmake --build build/release --target seam_singer_pilot -j6`; then `ctest --test-dir build/release --output-on-failure -R '^seam_singer_pilot_cli$'`. If shared library code changed, use the full build rule below. |
| V03 | RS-02 supervisor and CLI tests | `cmake --build build/release -j6`; then `ctest --test-dir build/release --output-on-failure -R '^(seam_helper_process_tests|seam_sample_review_cli_tests|seam_neural_worker_protocol_tests|seam_neural_phrase_runner_tests)$'`. Include any additional actual caller affected by changed semantics. |
| V04 | RS-03 evidence states | `python3 -B -m unittest discover -s tests/phase13a -v`; `python3 -B -m unittest tests.external_beta.test_release_identity -v`; `python3 -B scripts/verify_phase13a_contracts.py --root .`; run `python3 -B tools/phase13a/release_gate.py check --matrix docs/phase13a/mandatory-validation-matrix.json --gate G2 --expect-blocked`, then the same command for G3, G4, and G5. |
| V05 | RS-05 only if runtime semantics change | Full dependent build, then `ctest --test-dir build/release --output-on-failure -R '^(seam_neural_worker_protocol_tests|seam_neural_phrase_runner_tests|seam_neural_render_workflow_tests)$'`. Correcting prose/comments alone does not require a runtime suite. |
| V06 | Activated RS-09 geometry unit | Full dependent build, then `ctest --test-dir build/release --output-on-failure -R '^(seam_design_shell_input_tests|seam_design_frame_pipeline_tests|seam_clap_design_shell_tests)$'`; add the specific affected workspace test if the change reaches it. |
| V07 | Activated RS-11 identity optimization | `ctest --test-dir build/release --output-on-failure -R '^seam_cmake_source_commit_identity$'` plus the measured scratch-build scenarios. The Python identity test creates/builds scratch projects; account for that work explicitly. |

Configure a missing build using the checked-in preset only after inspecting its required toolchain/options; do not silently reconfigure an existing shared build just to make a command run. When a `libs/` implementation or behavior-bearing header changes, perform `cmake --build build/release -j6` before trusting CTest. For a failure that resembles stale linkage, inspect dependent artifact timestamps before changing code. Reuse an existing fresh build when no relevant inputs changed.

Run sanitizer checks when the unit affects process lifetime, concurrency, or memory ownership and the configured lane is available; record the actual lane and scope. Do not claim sanitizer coverage from marker-rejection unit tests. After the agreed focused checks pass, broaden testing only for an actual unresolved concern or repository-required gate, not as an open-ended ritual.

## Completion record for each task

Append a short record to this task document or the integrating ledger when the task lands. The proposed manifest is a planning aid, not a new release-admission format.

```text
Task: RS-XX
State: COMPLETE | PARTIAL | BLOCKED | DEFERRED
Integrated commit and verified remote hash:
Changed symbols / behavior:
Checks run, results, and skips:
Acceptance criteria satisfied:
Remaining criteria and exact blocker, if any:
Preserved compatibility / admission boundaries:
Uncommitted or unpushed work remaining:
```

“COMPLETE” requires the task's behavioral/documentary exit criteria and a verified push. A plan, local-only patch, passing old fixture, parallel developer message, or unavailable required test cannot substitute for that record. For conditional work, `DEFERRED` or `NO_CHANGE_RECOMMENDED` is an honest investigation disposition and must not be represented as a production refactor completed.

The mandatory cleanup ends when RS-01–RS-08 have their completion records. It does not establish project-wide completion, Beta GO, human listening acceptance, screen-reader acceptance, Windows/DAW coverage, signing, source rights, or an immutable release archive. In particular, preserve [U45_RECONCILIATION_HOLD](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/external_beta/full_product_gate.py:65) and the [explicit integration boundary](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/implementation/U45_TYPED_AUDIT_RECONCILIATION.md:28); neither becomes a cleanup checkbox.
