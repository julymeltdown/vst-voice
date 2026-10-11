# Repository staleness remediation execution

Completed the mandatory scope of the [task document](REPOSITORY_STALENESS_REMEDIATION_TASKS_2026-10-11.md) on 2026-10-11 (Asia/Seoul): **RS-01–RS-08, 8/8**. This is engineering remediation completion, not project-wide or product/Beta acceptance. Conditional work has the dispositions below; no speculative geometry/runtime/build refactor is represented as completed production work.

## Source synchronization and publication

Before implementation, `git fetch origin` and `git pull --ff-only --no-rebase` succeeded on `master`; the pull was already up to date at `d7d0c2533f2e09057f669f2a2058230e0020750b`. Each unit was staged separately after local checks, committed and pushed to `origin/master`. After **every** push, `git rev-parse HEAD origin/master` and `git ls-remote origin refs/heads/master` agreed at the hash below. Commit bodies end with `Local only; .github untouched.`

| Order | Task / behavior | Verified pushed commit |
|---|---|---|
| 1 | RS-01: selected schema-17–21 capture metadata, receipt integrity and real native writer/reader boundary | `998def4e7185f59a6aa6a430711b12e529741f05` |
| 2 | RS-02: one bounded supervisor, typed child outcomes and exact CLI refusal assertions | `e63d5556636a23a3a8d3a1dd8b67042818518c4f` |
| 3 | RS-03: truthful local SOURCE_READY evidence, historical state compatibility and blocked release gates | `4582a7f569e8d1fb616748d23011e727cf837f53` |
| 4 | RS-05: separate neural version dimensions and production launch guidance | `bc4b69b9cc89ef4c92c84638ab79997303abe4b9` |
| 5 | RS-04: current macOS editor/build/API route and historical notices | `c7740aabe8cfd1c3b78e7e216d9bbcbae7dc898e` |
| 6 | RS-06: side-effect-free help and usage errors | `432d6bb6563fee53b298d52a86db9316d489a9b2` |
| 7 | RS-07: exact fixed-input/source inventory and archived diagnostic classification | `bc835c405f3ce10584d021e42ba30b99c299e2b5` |
| 8 | RS-08: five unavailable historical attachments, six retained references and honest availability notes | `3c1ac597da331af86b8ed7d21c5be63b520a9fa5` |

The initial commit containing this report consolidated execution metadata only. A subsequent completion audit closed the missing RS-02 sanitizer verification and landed a test-probe correction at `2d739855b10c7bb0946b9296a6fb2a29b04e2035`. The root [execution ledger](FULL_SCOPE_BETA_EXECUTION.md) contains each unit's behavior, verification and preservation record. The [task manifest](REPOSITORY_STALENESS_REMEDIATION_TASKS_2026-10-11.tasks.json) supplies machine-readable task states and integrated unit identities.

## Verification and its limits

| Scope | Actually executed or inspected | Result |
|---|---|---|
| RS-01 Python capture and consumers | `python3 -B -m unittest tools.voice_model_training.test_prepare_captured_teacher tools.voice_model_training.test_prepare_corpus tools.voice_model_training.test_render_frozen_evaluation tools.voice_model_training.test_phrase_fingerprint -v` | **38/38 PASS, no skips**. Schema-21 full preparation retains five false admission/split flags; legacy exact verify-existing derivation passes. |
| Native writer/reader integration | Rebuilt `seam_singer_pilot`; `ctest --test-dir build/release --output-on-failure -R '^seam_singer_pilot_cli$'` | **1/1 PASS, 8.88s** on RS-01 source. Actual native schema-21 project/receipt/candidate/audio/recipe bytes cross the Python reader. |
| RS-02 shared library dependency hygiene | `cmake --build build/release -j6` | Full dependent **Release build PASS** before CTest. No stale dependent binary was used to diagnose a new-code failure. |
| RS-02 focused CTest | Helper, neural worker protocol, neural phrase runner, Japanese native reading and sample-review CLI entries | First four **PASS** (1.19/2.09/0.75/0.57s). Dirty CLI entry **FAIL**, 24 passing native cases and one pending installed-recipe audio-peak failure. This is not 5/5 PASS. |
| RS-02 exact committed CLI test source | Separately compiled committed `test_sample_review_cli.cpp` plus RS-02, using generated compile/link flags, freshly rebuilt libraries and existing target's other objects | **24/24 PASS, 11.91s**. No working source replacement or pending-code staging. Exact source SHA256 `360164fbb0dc86b2d80eb4a73df600d7a1530adbd1e729426fad9e8f639cad92`; binary SHA256 `fe15d3b87522182eb4c2ac1393d2a42f5d3f9967b9f4a9ee4cb59b3c3cc1121f`. This explicitly isolated result closes the remediation test scope while preserving the contrary dirty-suite result. |
| RS-03 | Phase13A unittest discovery; release-identity script; source-contract script; release gate `check --expect-blocked` for G2/G3/G4/G5 | **188 PASS / 4 optional native-input skips out of 192**, identity **9/9 PASS**, contracts **PASS**, all gates blocked with `errors: []`. No Linux packaging generator executed. Pure builder inputs are labeled fixtures, not receipts. |
| RS-04/05 | Source comparison with presets/CMake, public symbols, codecs, deployment/launch and runner checks; bounded local-link check | 121 inline file links available in the eight current edited documents. Fragments/reference definitions excluded. No GUI launch or runtime default change claimed. |
| RS-06 | Pilot dependency rebuild and existing pilot CTest | **1/1 PASS, 9.29s**, including six no-write help/error cases and all retained writer/capture, valid modes, repeatability, invalid-phrase and no-overwrite checks. |
| RS-07 | Static AST/path/receipt/source-hash inspection, without importing historical tools | Five source hashes and fixed checkpoint constants match inventory; 56 inline file links available. No training, packet generation or captured-byte mutation. |
| RS-08 | Exact target existence recheck; bounded inline-link scan of three historical reports | 204 inline links examined; exactly six unavailable occurrences / five unique targets retained. Referring lines/report hashes match availability JSON. No recovered original bytes or historical PASS substitute. |
| Every unit | Staged diff/status inspection, `git diff --cached --check`, tracked-source closure after new files staged | **PASS** before publication. Closure is registration hygiene, not historical artifact or product acceptance. |

No full-project CTest or sanitizer result, independent review verdict, human listening, VoiceOver, DAW, signing or Windows runtime acceptance is claimed by this remediation. The scoped RS-02 follow-up below supplies actual ASan/UBSan and TSan checks; the initial eight-unit execution did not include them. The POSIX signal outcome and legacy wrapper were executed on macOS; Windows's unsigned exit-code/source semantics were reviewed only.

Local raw build/failure/pass logs, isolated test source/binary and preservation patch are under ignored `out/evidence/repository-staleness-2026-10-11/`. That directory is local engineering retention, not a pushed immutable archive. The tracked summaries retain exact tested identities and contrary outcomes; external release retention remains a separate obligation.

## RS-02 completion-audit follow-up

The verification recipe requires sanitizer checks for process-lifetime changes when a configured lane exists. Both lanes were available. A clean managed checkout at `6cd08542b8fe50b2ac63d751e76feaec5d2fb09d` also allowed the committed CLI suite to run through its normal CTest registration without either pending file. Release used the checked-in preset defaults; no optional learned-runtime or GUI qualification is implied.

| Checkout / source scope | Executed check | Result |
|---|---|---|
| Clean committed baseline, no pending edits | Dependency build of the five RS-02 target entries, then serial CTest | **5/5 PASS, 66 native cases, 16.39s**: helper 4, worker protocol 32, phrase runner 5, Japanese reading 1, CLI 24. This is a normal complete committed CLI suite, beyond the earlier separately linked test evidence. |
| Clean baseline, initial instrumented probe | ASan/UBSan helper CTest; TSan helper CTest | Each **FAIL, 2 passing / 2 failing cases**. Fresh binaries failed exact stderr checks because the runtime warned that it could not discover a symbolizer in the intentionally empty environment. These failures remain recorded. |
| Clean baseline plus reviewed probe patch | Rebuilt helper/probe dependencies, ASan/UBSan helper CTest | **1/1 entry, 4/4 native cases PASS, 1.36s**. |
| Clean baseline plus reviewed probe patch | Rebuilt helper/probe dependencies, TSan helper CTest | **1/1 entry, 4/4 native cases PASS, 1.52s**. |
| Integrating checkout; original two dirty files excluded from helper target | Rebuilt dependencies and helper CTest in Release, ASan/UBSan and TSan | Each **1/1 entry, 4/4 native cases PASS**, respectively **1.11s / 1.48s / 1.77s**. |

The [probe](../../tests/helpers/reading_process_probe.cpp) now returns `external_symbolizer_path=/usr/bin/atos` through the macOS-only `__asan_default_options` or `__tsan_default_options` hook. The installed compiler's sanitizer headers declare these hooks, and the absolute system executable was checked. The patch leaves error reporting, symbolization, exact stream assertions and the supervisor's empty child environment intact. It adds no filtering, suppression or inherited PATH. The tested probe SHA256 `0b0aea8aab20da9d226c5ce9fe895a2ecdf0f0f54198c186ee476febc6aeb452` matches the pushed blob at `2d739855`.

Generated compile commands confirm instrumentation of the helper implementation, helper tests and child probe in both sanitizer lanes. Final native logs contain no visible sanitizer-report or symbolizer-discovery-warning markers. This is four-case helper coverage per lane, not a full-project sanitizer, leak-detection or Windows claim. The initial failures and the earlier dirty installed-recipe failure are not converted into PASS.

The [verification evidence record](../reviews/RS02_COMPLETION_VERIFICATION_2026-10-11.evidence.json) retains tested source/patch identities, exact command/result scopes and log/executable hashes. Local bytes are retained under ignored `out/evidence/repository-staleness-2026-10-11/rs02-final-verification/`. The temporary checkout is archived after retaining needed ignored files. All original pending edits and the stash remain exact; the supplemental fix is independently committed and remote-verified.

## Preserved unfinished work

- [Candidate-package commands](../../apps/seam-voicebank-cli/candidate_package_commands.cpp) retain their starting SHA256 `a136e4b172df503fe68b1fe598a4d93ce6adc57fdb62bb839245b283cde5eb70`, including the unfinished installed-recipe operation.
- [Sample-review CLI tests](../../tests/test_sample_review_cli.cpp) retain the original **73 inserted pending lines**. Exact patch payload/context matches the starting patch; Git hunk positions/index headers changed because the separately committed RS-02 changes precede them. The file's starting hash was `2ac00d98f0d9551a1c99ea34d681be31d8866fd06b14433ed09a1afeb64f461a`; its current combined working-file hash is `72d5f2914dd791bc1cdea7fd9495ca83d70127f15ae4808ee281940d4e4a4713`. The pending shell workaround remains uncommitted and is not counted as RS-02's API implementation.
- `stash@{0}: On codex/brand-fonts-tooltips: brand fonts tooltip in-flight before master merge` remains exact. No stash or unrelated edit was discarded.
- No remediation implementation remains uncommitted or unpushed. The two original dirty files remain outside the landed units. `.github` has no diff from the starting commit; Windows TODO and U45 admission holds remain.

The pending installed-recipe test failed at the non-silent master assertion (`analyzeAudio(...).peak > 1e-4F`, working test line 686 at this check). The next owner must trace its captured installed resource, reopened project/render selection and actual exported WAV before changing code or acoustic thresholds. Preserve the failing receipt and rerun against freshly rebuilt dependencies. This report does not claim that pending feature is correct or complete.

## Conditional structural dispositions

**RS-09 — DEFERRED.** This remediation changed no presentation geometry and demonstrated no pointer/text-input/accessibility/resize mismatch or repeated coordinate-rule edit. The existing `TextInputAnchor` contract, `SingShell::fromLegacy/toLegacy`, `shellFieldBounds`, `translateTextInput`, `SingLayout` and `prepareFrame` already separate controller coordinates from presentation. No new geometry tests or VoiceOver result are claimed. A future reproduced mismatch must supply field/viewport/scale/composition evidence before a bounded refactor is activated.

**RS-10 — NO_CHANGE_RECOMMENDED for the inspected recipe export/render chain.** Static trace:

| Step | Current filesystem/admission/identity boundary |
|---|---|
| `TrackRecipeFileSource` → `captureProceduralSource` | Checks selected track and exact saved reference/pins; calls `admitProceduralRecipe`. Relative paths resolve below the canonical saved-project directory with real component/leaf checks, then undergo installed-location classification. |
| `admitProceduralRecipe` | Installed route reads typed manifest/receipt and bounded recipe through `loadProceduralCandidate`; checks selected resource, saved installation provenance, compiled engine revision, style and trust/development policy. Authored/project-copy route loads byte-bound recipe identity. Restricted-construction `AdmittedProceduralRecipe` owns the immutable captured resource/origin. |
| `ExportService` recipe packaging | Captures each selected recipe into `TrackProceduralSource`, decodes/serializes those held bytes, verifies canonical content hash, and packages by hash. Preserves installation pins with a portable recipe path and replaces render inputs with the same frozen sources. These serialization/hash checks bind archive bytes rather than duplicating installed trust policy. |
| `ProductionProjectRenderer` source resolution | File-backed inputs capture before rendering/cache. Frozen inputs compare selected resource/style/engine and existing admission `matches`; an absolute bound source without an admission value re-admits. Existing admitted frozen inputs do not reopen the installed tree. |
| `RenderSnapshotFactory::createProcedural` → PCM cache | Snapshot derives from held recipe resource and project/performance/pronunciation identity. Cache lookup follows admission/snapshot construction; changed selection/bytes cannot bypass current file-backed admission by a cache hit. |

Sources: [capture/render](../../libs/seam-rendering/src/project_renderer.cpp), [admission](../../libs/seam-distribution/src/procedural_package.cpp), [export packaging](../../libs/seam-authoring-runtime/src/export_service.cpp), and [snapshot](../../libs/seam-rendering/src/render_snapshot.cpp). This chain already provides the requested immutable boundary. No recurring unnecessary policy crossing was demonstrated, so no second token or broader dependency refactor is introduced. This is a scoped source conclusion, not proof that every runtime call chain is optimal.

**RS-11 — DEFERRED pending comparable timing.** The full RS-02 rebuild and later focused pilot rebuild are different operations, with shared-header, generated-identity and previously stale target effects. Their command counts do not establish an attributable timing regression. No warmed no-op/leaf/shared-header/identity-only four-case benchmark was performed in a scratch checkout, and no performance benefit is claimed. The existing source-identity regression script was executed during source inspection (**17/17 PASS, 7.75s**); this verifies identity semantics, not build economics. A future investigation must measure the named loop under comparable compiler/options/load and preserve exact release identity, test discovery and coverage. No timeout/caching/identity shortcut is introduced.

## Boundaries remaining outside this goal

The canonical Usable Alpha and full Beta criteria still require their corresponding unaided human/resource/rights/model/platform/host/signing/qualification/archive evidence and accepted decisions. `U45_RECONCILIATION_HOLD` remains in place. Historical attachments remain unavailable; packet 003 remains partial/Q1-refusing. The preserved installed-recipe operation is unfinished and has the explicit failing test above. None of these boundaries is silently converted into PASS by completing repository cleanup.
