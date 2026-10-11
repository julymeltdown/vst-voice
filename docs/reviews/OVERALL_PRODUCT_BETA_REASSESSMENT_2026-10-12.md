# Project SEAM: overall progress, Product/Beta blockers, and next development goal

Date: 2026-10-12, Asia/Seoul. Audited source: `ea37cd34dcd879b28610d998a1a28fb9f144a2a6`, `master`. `git fetch origin` and `git pull --ff-only --no-rebase` ran first. Local HEAD, `origin/master`, and live remote master agreed. The checkout was clean; the existing brand-fonts/tooltips stash was preserved.

## Assessment

**A current overall implementation percentage is not established. Accepted canonical Alpha and External Beta evidence remains 0%; Product/Beta is BLOCKED.** The old approximately 54% planning index is historical and cannot be promoted or presented as a freshly measured current completion rate. Zero accepted release evidence does not mean zero working code, and the completed remediation task document does not mean the whole product is complete.

The strongest fresh result is a connected installed procedural singer workflow: design/publish/install, author lyrics, render, tune, undo, save/reopen, and export. After a full dependent Release rebuild, four focused CTest entries passed **4/4, 72 native cases, 36.28 seconds**. That is automated engineering evidence on this macOS checkout. It supplies neither qualified musical resources nor unaided human, DAW, signing, Windows, or release acceptance.

The dominant remaining risk is **whether a governed, installed singer makes useful music on unfamiliar material**. The project already has considerable persistence, rendering, producer, safety, and evidence infrastructure. More infrastructure completion alone cannot answer the singer-quality and creator-usability question. The evidence does not justify a wholesale architectural rewrite, but it does justify changing the order of work: establish one real singer/song observation, repair the largest observed defect, then expand qualification.

## Percentages and exact denominators

| Measure | Result at the audited source | Meaning and limit |
|---|---|---|
| Repository remediation scope | 11/11 closed dispositions = 100% | RS-01–09 complete; RS-10/11 accepted NO_CHANGE investigations. This is a separate task document. |
| Historical U1–U48 planning index | `(6 + 7×0.8 + 32×0.45)/48 = 54.17%`, approximately 54% | Retired as the execution goal's progress metric. An inherited judgment model, not a new current implementation assessment, effort, remaining time, or probability of release. |
| Recorded local U-unit acceptance labels | 6/48 = 12.5% | Historical local acceptance for U1–5/U29; U29 is POSIX scoped. This audit does not renew all six acceptance reviews or promote them to full Product/Beta acceptance. |
| Canonical Usable Alpha | **0/20 PASS = 0%; 20 NOT_RUN; zero evidence records; BLOCKED** | Current ledger count, freshly read and contract checked. It does not imply the underlying behaviors were never tested elsewhere. |
| External Beta | **0 accepted evidence records for 9 mandatory EB categories; BLOCKED** | No candidate-bound accepted proof is recorded in the canonical ledger. This is not a measured fraction of implementation. |
| Full product | 20 requirements, 18 work packages, **83 registered cases**, `evidenceStatus=NOT_RUN` | Fresh registry validation succeeds; these are definitions, not 83 passed product cases. The report does not claim to have executed all required dimensional combinations. |
| Released-resource registry | **0 resources** | `releasedResources=[]`; experiments, installed fixtures, and retained trained checkpoints are separate. |
| Target scope | 3 languages, 2 platforms, 9 commercial-host tuples | Japanese, English, Korean; macOS arm64 and Windows x86_64; five macOS and four Windows host tuples remain mandatory. |

Recent changes repair portions of already partial/implemented units; they do not establish their complete original exits. Considerable work can occur within a partial row without changing this coarse index, so unchanged arithmetic cannot establish unchanged implementation progress. Repeating it would not create a fresh 48-unit scoring census. The inventory below refreshes code ownership and unmet obligations; it is deliberately not a new acceptance ledger. Equal unit sizes and weights remain unvalidated. Varying the partial weight from 0.25 to 0.65 moves the old model from **40.83% to 67.50%**; this is sensitivity, not a statistical confidence interval. A new precise-looking implementation percentage would overstate the evidence. The [current execution plan](../plans/2026-10-08-full-scope-completion-execution-plan.md:237) explicitly uses verified exit criteria and retires 54% as its success metric.

Source for the model and its prior corrections: [October scope audit](../PROGRESS-AUDIT-2026-10-08.md:7), [independent completeness review](FINAL_PROGRESS_AND_COMPLETENESS_REVIEW_2026-10-08.md:23). Original unit bodies, rather than the table labels, own acceptance: [U1–U48 plan](../plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md:268). The [integrating ledger](../implementation/FULL_SCOPE_BETA_EXECUTION.md:8781) records the recent smaller units.

## Fresh execution evidence and repository integrity

All execution below preceded report edits, on clean `ea37cd34`. The generated header identifies that exact source. The report/evidence metadata commit does not change production code; it is not a new signed candidate or a repeat runtime qualification.

| Check actually run | Result | What it supports |
|---|---|---|
| `cmake --build build/release -j6` | PASS, exit 0 | All dependent targets rebuilt before trusting focused CTest; avoids stale linked binaries. |
| `seam_expression_on_song_tests` | 6/6 native; 1.44s | Selected expression changes affect actual rendered song output in the engineering fixture. |
| `seam_original_singer_song_journey_tests` | 7/7 native; 19.60s | Installed singer, tuning/undo/reopen/export, frozen installation, phoneme-boundary audio change, Designer-to-song handoff. |
| `seam_procedural_install_journey_tests` | 34/34 native; 1.90s | Installed resource and admission/refusal behavior. |
| `seam_sample_review_cli_tests` | 25/25 native; 13.32s | Current sample/recipe packaging, native project binding, installed-recipe operation and intentional model refusals. |
| `verify_usable_alpha_contract.py` | PASS, exit 0 | The BLOCKED ledger is structurally coherent; the command does not mean Alpha passed. |
| `verify_phase13a_contracts.py --root .` | PASS, exit 0 | Source contracts/pins and truthful deferred-CI state; external runtime remains NOT_RUN. |
| Full-product admission + release-status Python modules | 22/22, 1.121s | Legacy/forged proof cannot satisfy the full gate; current contract/status boundaries. This is validator testing. |
| Direct current registry/profile/Phase13A evaluation | Registry errors `[]`; profile has 13 pending entries; G2–G5 BLOCKED, evaluator errors `[]` | Missing evidence and unresolved qualification criteria are distinct from malformed configuration. |
| Tracked-file census | **3,043 tracked paths; zero missing** | No indexed file is absent. This does not establish asset completeness or recover unavailable historical evidence. |

Native journey anchors: [test definitions](../../tests/test_original_singer_song_journey.cpp:446), [exact installed export/reopen identity](../../tests/test_original_singer_song_journey.cpp:1541), [Studio handoff](../../tests/test_original_singer_song_journey.cpp:1582). These exercise production controllers with test fixtures; they are not one person's unaided operation of the installed desktop app.

The earlier branch audit named 13 added paths absent from master. **Twelve are now present and tracked**, including all named implementation/test paths for typed packaging, install transactions, and the typed full-product executor. The remaining absent path is the historical `docs/implementation/FULL_PRODUCT_TYPED_AUDIT_2026-09-28.md`; [current U45 reconciliation](../implementation/U45_TYPED_AUDIT_RECONCILIATION.md:1) documents the adopted implementation and its remaining hold. Do not report the old 13-file implementation deficit as current or merge an old branch wholesale to restore a historical document.

The audit's [evidence metadata](OVERALL_PRODUCT_BETA_REASSESSMENT_2026-10-12.evidence.json) records source/input/binary/raw-log hashes, gate outputs, counts, and limitations. Raw files are retained locally under `out/evidence/overall-readiness-2026-10-12/`. This ignored local directory is not an externally anchored immutable release archive. No full CTest matrix, new sanitizer run, neural campaign rerender, physical user session, commercial DAW, VoiceOver, Windows, signing, or notarization acceptance was performed in this assessment.

## Code-confirmed blockers: behavior, not missing paperwork alone

### 1. Qualified singer material and real musical acceptance are still absent from the release lineage

The canonical resource matrix is UNRESOLVED and its released set is empty: [contract scope](../product/full-product-beta-contract.json:49). A signed engineering recipe can render nonzero audio without being an approved female singer, intelligible across the declared vocabulary, expressive across range/styles, or lawful for every advertised use. The installed-recipe repair established a real rendering/receipt path and an unchanged non-silence threshold; [its completion report](INSTALLED_RECIPE_EXPORT_COMPLETION_2026-10-12.md) explicitly retains that boundary.

Production approval is an actual distinct-role action. [Sample review preparation](../../libs/seam-voicebank-production/src/repository_review.cpp:83) requires current reviewed units and checks reviewer independence. [Beta bank requirements](../voicebank/BETA_VOICEBANK_ACCEPTANCE.md:3) additionally bind rights and exact installed identity. Creating oscillator fixtures or changing a status field cannot supply those obligations.

The checked-in [Beta bank dossier](../voicebank/beta-voicebank-01-dossier.json:4) remains BLOCKED, with permissions false and source/derived asset arrays empty. This is a concrete empty release intake, not evidence that development fixtures or private experiments do not exist.

Required work: select a concrete production-bank route, establish asset-scoped permissions and two distinct producer/reviewer people, review the material, publish/package/install its exact identity, then obtain listening/creator judgments on unfamiliar lyrics and songs. Preserve all three languages and final resource kinds; a Japanese starter slice is an ordering choice, not a scope reduction.

### 2. The current listening packet cannot yet ask its intended Q1 honestly

[Packet 003 settings](../../tools/singing_quality/listener_packet_003.py:25) select `dry-procedural-candidate`. [Its Q1 guard](../../tools/singing_quality/listener_packet_003.py:160) rejects absent onset-lead properties. Sample-bank preutterance is a different production timing path. A valid procedure-only export therefore does not repair the packet's sample-path question.

Required work: render the unchanged question from the appropriate reviewed sample-bank production path, retain exact project/resource/renderer/settings/output hashes, preserve the controlled Q3 time slot and named verdicts, and publish a consistent packet with answers stored separately and bound to its manifest. An integrity PASS or a deliberate refusal is not a valid listener packet or a listening verdict. [Current tool lifecycle](../implementation/CURRENT_ENGINEERING_ENTRYPOINTS.md:41) retains that distinction.

### 3. Learned singing has real experiments, but production installation and qualification remain incomplete

The code explicitly returns `MODEL_INSTALL_UNSUPPORTED` for typed model candidates: [installer switch](../../libs/seam-candidate-packaging/src/candidate_package.cpp:415). Model packaging success and the [neural bundle registry](../../libs/seam-authoring-runtime/src/neural_resource_registry.cpp:30) are separate paths. This is a gap in the typed installed-candidate bridge, not evidence that no neural model can execute anywhere.

This audit freshly read and rehashed a retained five-song learned campaign at `/Users/lhs/seam-corpus-pauses-2026-09-19-r1/campaign-e13-v512-e1-r1/campaign.json`. It records execution success for all five, strict `MISMATCH` for all five, `singerQualified=false`, and `releaseEligible=false`; all five referenced comparison digests still match. This is verification of historical receipt bytes, **not fresh inference or a current model-quality verdict**. The [campaign report](../implementation/SINGER_VALIDATION_CAMPAIGN_2026-09-20.md:549) also records vocoder training overlap in three regression songs. Keep those diagnostics; qualify on a frozen cohort independent of both acoustic and vocoder training ancestry. Do not assert that trained assets do not exist, that a historical failure still characterizes every newer candidate, or that unrelated pitch-diagnostic fixes qualify learned singing.

Its captured corpus digest was also rehashed and matches the campaign: `ec77cc91df61718f171b83114a26bbe7df0e28d09f52357b2443844c3a9216ca`. The corpus is `com.project-seam.captured-teacher-corpus`, contains `procedural-song-*` sources from one recipe, and records `PREPARED_UNAPPROVED`, `sourceRightsAdmitted=false`, `labelsAdmitted=false`, and `trainingAdmitted=false`. This is a generated-teacher learning diagnostic under [W09's stated boundary](../plans/2026-10-08-full-scope-completion-execution-plan.md:131). The selected receipt uses acoustic epoch 13/vocoder epoch 1; the campaign document additionally describes later epoch-21 experiments. It is not a census of the newest private candidates. No W09 execution/closure entry was found in the integrating ledger since the October execution plan; that statement does not establish that no private experiment occurred.

Required work: complete atomic model installation/catalog/runtime admission with graph/dependency/helper/build identities; choose the actual candidate and lawful training ancestry; inspect current checkpoint availability; run score-to-acoustic-to-vocoder native output on a genuinely independent cohort; retain failures, audible-quality/linguistic review and measured budgets. The original-singer tests above use procedural engineering resources and cannot close this work.

U14 also retains dictionary/character candidate production and installation, full typed post-publication/catalog recovery, and named packaging follow-ups. [The packaging worklist](../formats/RESOURCE_CANDIDATE_PACKAGING.md:175) includes large-bank duplicate copy/hash/cancellation costs, prior-content identity/recovery for explicit replacement, generic temporary-file hardening and same-owner payload reopen races. These are concrete remaining engineering obligations, rather than only musical approval or model installation.

### 4. Complete expression, language and interchange behavior is still unqualified and contains explicit gaps

[Procedural snapshot admission](../../libs/seam-rendering/src/render_snapshot.cpp:733) and [neural admission](../../libs/seam-rendering/src/render_snapshot.cpp:809) refuse StyleBlend PCM pairs; the implemented paired-sample path is different. The contract permits backend-specific incompatibilities only in a frozen matrix with a tested positive combination for every mandatory feature. Therefore these refusals require a coverage decision and positive proof; they do not automatically require enabling StyleBlend on every backend.

[USTX conversion](../../libs/seam-interchange/src/ustx_project_conversion.cpp:561) explicitly leaves the OpenUtau color palette inert without a trusted bank/style or phoneme-level `clr` mapping. The [U30 acceptance audit](../implementation/U30_ACCEPTANCE_AUDIT_2026-09-23.md:289) defines the safe bridge obligations. Codec round trips and honest loss reporting do not prove the complete external workflow. The compiler's [target-unvoicing status](../implementation/U6_PERFORMANCE_COMPILER_STATUS_2026-09-06.md:3) likewise retains an unsupported production conversion; do not generalize this to the absence of all unvoiced articulation.

Required work: close original compiler/chunk/capability exits, construct a tested resource/backend matrix for required controls, implement any mandatory coverage holes, finish safe interchange semantics, and obtain Japanese/English/Korean resource-specific native-language review. Use current source; older sustained-vowel-only procedural notes were superseded by later articulation/installation work and are not the current global capability ceiling.

### 5. The U45 bridge is engineering-only and incomplete

The new export operation [requires a verified recipe and refuses sample/model capture](../../apps/seam-voicebank-cli/candidate_package_commands.cpp:228), and separately refuses backing media. These are limitations of the **audit operation**, not a claim that normal sample audio export is absent. Project binding validates references; it does not by itself prove resource use or output measurements.

The [typed executor](../../tools/external_beta/full_product_gate.py:76) keeps `U45_RECONCILIATION_HOLD`; the canonical [semantic status](../product/full-product-beta-contract.json:7303) is UNAVAILABLE/BLOCKED. [READY evaluation](../../tools/external_beta/release_gate.py:194) still uses the existing full report reader. The hold intentionally prevents component declarations from authorizing release.

Required work: capture/admit all applicable native resources and dependencies, bind actual operations and continuity/measurements to their exact project/build/platform/host lineage, validate actual installed receipts and external review authority, and integrate the completed adapter through READY/CLOSED/public-predecessor audits while preserving shared soak replay refusals. Do not remove the hold or flip metadata before those obligations are met. [Current reconciliation worklist](../implementation/U45_TYPED_AUDIT_RECONCILIATION.md:58) remains authoritative.

### 6. Installed user, host, support, accessibility and release evidence remains open

The [Alpha ledger](../product/usable-alpha-acceptance.json:1) remains 20 NOT_RUN rows. A procedural automated journey cannot satisfy its production sample-concatenative hearing row. The required human session also includes external audible export inspection and 30 minutes. Native IME geometry checks are useful, but do not establish VoiceOver, narrow/scaled accessibility, or unaided workflow acceptance.

Full Beta additionally needs both target platforms, all nine required REAPER/Bitwig/Logic tuples, actual installed live/offline/state/recovery behavior, 30/120-minute workloads, final U60 privacy/support/crash behavior, exact signed/notarized descendants, clean install/update/uninstall, independent review, and an externally anchored restored raw archive. [External Beta acceptance](../product/EXTERNAL_BETA_ACCEPTANCE.md:15) makes these separate from source readiness. [Windows remains TODO](../../README.md:28) by user direction; useful macOS development continues while full cross-platform Beta remains incomplete.

The resource/evaluation profile has **13 pending entries**: acoustic-boundaries, expression-tolerances, pronunciation-scoring, identity-rubric, generation-budgets, neural-budgets, resource-limits, cancellation-budgets, reference-machines, source-volume, reproducibility-tolerances, evaluation-profile-freeze, and resource matrix. They require governed measurements/decisions before final acceptance. Existing fixed floors remain: 60 phrases per language, three songs, five independent creators, 30-cent median pitch, 90% steady frames within 50 cents, and separate 500ms/400ms edit/preview workloads. Changing unresolved metadata without evidence would not close them.

## Full-scope residual inventory

This maps all 48 original units to present code and the original exit still needing closure or retention. “Retained local” is not a newly rerun acceptance verdict. Rows with code present are not automatically complete. The original plan and root ledger remain the sole completion authorities.

| Unit | Present implementation anchor | Residual exit / evidence boundary |
|---|---|---|
| U1 | `CMakeLists.txt`; fresh full Release build | Retain reproducible baseline; this check is not full final-candidate qualification. |
| U2 | `docs/product/full-product-beta-contract.json`; fresh registry checks | Retain fail-closed definition; freeze remaining measured profile/matrix for final acceptance. |
| U3 | `libs/seam-formats/src/project_json.cpp` | Retained local acceptance; current schema repair is covered, not downstream acoustic qualification. |
| U4 | `libs/seam-phonemizer/src/pronunciation_resolver.cpp` | Retained bounded Japanese local acceptance; later language/resource review remains separate. |
| U5 | `libs/seam-synthesis/src/timing_solver.cpp` | Retained local timing acceptance; qualify full singing/renderer behavior downstream. |
| U6 | `libs/seam-synthesis/src/performance_compiler.cpp` | Complete original cross-family performance and target-voicing obligations with measured coverage. |
| U7 | `libs/seam-rendering/src/render_snapshot.cpp` | Complete context/chunk/resource integration exits, including applicable non-sample production cases. |
| U8 | `libs/seam-synthesis/src/renderer_capabilities.cpp` | Prove requested Final performance, truthful Raw/refusal/cache identity across qualified resources. |
| U9 | `libs/seam-voicebank-production/src/repository.cpp` | Complete governed mixed-source producer workflows and asset-scoped lineage acceptance. |
| U10 | `tools/voicebank_script_generator/inventory.py` | Implementation credit retained; actual language/style coverage requires reviewed production material. |
| U11 | `libs/seam-voicebank-production/src/repository.cpp` | Implementation credit retained; full producer/recovery acceptance remains unpromoted. |
| U12 | `libs/seam-voicebank/src/validator.cpp` | Current stored-byte inspection/QC must be reviewed for actual applicable production material. |
| U13 | `libs/seam-voicebank-production/src/repository_review.cpp` | Complete revision-bound marker/review/retake obligations; real independent approvals, not fixtures. |
| U14 | `libs/seam-candidate-packaging/src/candidate_package.cpp` | Sample/recipe implemented; typed model/dictionary/character candidates/install, runtime dependencies, packaging/recovery follow-ups and full resource acceptance open. |
| U15 | `libs/seam-voicebank/src/acoustic_analysis.cpp` | Versioned analysis/conditioning implemented; retain declared limits and qualify actual material. |
| U16 | `libs/seam-rendering/src/render_pipeline.cpp` | Fixed-corpus numerical/listening qualification for exposed classical modes and voiced/unvoiced behavior. |
| U17 | `libs/seam-synthesis/src/unit_selection.cpp` | Contextual selection/paired styles implemented; production range/blend listening and rationale acceptance. |
| U18 | `libs/seam-voice-design/src/voice_recipe.cpp` | Recipe persistence/admission exists; complete original capability/provenance acceptance. |
| U19 | `libs/seam-voice-design/src/phonation_source.cpp` | Coherent intended voice/vowels/range need independent musical judgment, not oscillator tests alone. |
| U20 | `libs/seam-voice-design/src/articulation_plan.cpp` | Unseen-phrase intelligibility, complete declared inventory, production baking and quality qualification. |
| U21 | `libs/seam-authoring-runtime/src/generation_campaign_advance.cpp` | Resume/budget fixes exist; complete producer lifecycle/legacy campaign disposition and actual reviews. |
| U22 | `libs/seam-native-ui/src/voicebank_studio_production_support.cpp` | Native generation/import/recording routes exist; independently exercise original complete producer workflows. |
| U23 | `libs/seam-application/src/note_commands.cpp` | Musical-editing implementation credit retained; creator rhythm/tempo/repetition acceptance. |
| U24 | `libs/seam-application/src/lyric_commands.cpp` | Hint/IME/geometry improvements exist; ordinary native workflow in each required language. |
| U25 | `libs/seam-authoring-runtime/src/technical_edit_controller.cpp` | Complete inspector/expression workflows and dense/narrow/scaled accessibility acceptance. |
| U26 | `libs/seam-phonemizer/src/japanese_phonemizer.cpp` | Native Japanese reviewer and matching actual resource-vocabulary/song acceptance. |
| U27 | `libs/seam-phonemizer/src/english_phonemizer.cpp` | Native English review plus usable actual English singing material. |
| U28 | `libs/seam-phonemizer/src/korean_phonemizer.cpp` | Native Korean review plus usable actual Korean singing material. |
| U29 | `libs/seam-interchange/src/ustx_codec.cpp` | Retained local POSIX bounded-boundary acceptance; Windows/full-product proof remains separate. |
| U30 | `libs/seam-interchange/src/ustx_project_conversion.cpp` | Trusted color/style/`clr` mapping and current real external USTX subset exchange. |
| U31 | `libs/seam-interchange/src/smf_project_conversion.cpp` | SMF implementation credit retained; current real DAW exchange after later fixes. |
| U32 | `libs/seam-native-ui/src/conversion_review_model.cpp` | Actual musician completes native DAW/OpenUtau→SEAM→DAW without JSON/study-script repair. |
| U33 | `libs/seam-clap-editor/src/plugin_entry.cpp` | Installed-host per-note/expression mappings and advertised live behavior on required tuples. |
| U34 | `libs/seam-clap-editor/src/host_timeline.cpp` | Every required host's timing authority and complete current-ready offline bounce workflow. |
| U35 | `tools/voice_model_training/prepare_captured_teacher.py` | Reproducible authorized dataset/preparation/training, reviewed ancestry and genuinely independent partitions. |
| U36 | `tools/voice_model_training/validation_campaign.py` | Actual selected acoustic/vocoder candidate with native held-out quality and provenance acceptance. |
| U37 | `libs/seam-authoring-runtime/src/neural_phrase_runner.cpp` | Bounded inference exists; exact installed model/helper/dependency lineage and production recovery/platform proof. |
| U38 | `libs/seam-application/src/performance_commands.cpp` | Learned proposal quality plus creator accept/correct/regenerate/undo without losing manual ownership. |
| U39 | `libs/seam-synthesis/src/renderer_capabilities.cpp` | Complete mandatory expression coverage in frozen matrix; actual acoustic/perceptual results. |
| U40 | `libs/seam-application/src/harmony_commands.cpp` | Actual creator variation/harmony UI→save/reopen/export of intended selected results. |
| U41 | `libs/seam-native-ui/src/character_performance_binding.cpp` | Exact installed rights-cleared character asset, singer association and accessible meaningful performance. |
| U42 | `docs/product/full-product-beta-contract.json` | Final complete governed singer/resource set absent from released registry; precursors exist. |
| U43 | `tools/singing_quality/`; fixed R16 criteria | Full corpus, three songs, five creators, linguistic/listening/assisted-benefit acceptance absent. |
| U44 | `libs/seam-authoring-runtime/src/support_bundle.cpp` | Complete original U60 crash/recovery/support/consent/delete/privacy acceptance on installed targets. |
| U45 | `tools/external_beta/full_product_gate.py` | Native resource/operation/measurement/reviewer replay and deliberate canonical integration still held. |
| U46 | `tools/external_beta/release_gate.py` | Same restored candidate-bound full predicate at promotion/resume/close/public predecessor. |
| U47 | `scripts/run_external_beta_host_evidence.py` | Signed-installed macOS/Windows, all nine hosts, signing/install/workload/accessibility evidence. |
| U48 | `scripts/run_external_beta_release_audit.py` | Restored externally anchored archive, no Blocker/Critical defects, authorized full-scope READY decision. |

## Proposed next development goal

**Prepare and observe one song on today's macOS app using an exact development singer, then repair the largest observed musical or workflow blocker. Prepare the production-bank decision dossier in parallel.** Map it to the existing **W04 observation/rehearsal and W05a decision handoff**, then the applicable U19/U20/U22/UI obligation. W01's valid full sample-bank packet follows when its bank dependency is supplied. Preserve the original full-scope finish line; do not restart RS-01–11 or declare this slice Beta complete.

The goal is proposed for the user's subsequent execution instruction. It has not been activated or implemented by this assessment. It separates an agent-executable engineering exit from acceptance requiring the user/other people, so an agent can finish useful development without inventing human judgments.

| Order | Concrete unit and code ownership | Reviewable exit and dependencies |
|---|---|---|
| **P0.1** | Prepare a usable observation kit: rebuilt HEAD-identified Studio/editor, one exact installed development procedural singer, a 30-second Japanese lyric/melody task, saved project, preview/master/stem outputs and five-field observation form. Rehearse the native workflow. | The kit opens and exercises the actual app, with source/build/singer/project/export hashes and explicit engineering-resource status. Controller tests are supporting checks, not a substitute for this native rehearsal. W05a production approval is not a prerequisite. |
| **P0.2, parallel** | Inventory available candidates and lawful input/reviewer records; prepare a concrete generated-sample-bank versus recorded/imported-bank decision dossier using existing producer/Studio/package paths. | User can choose a specific route and identify two distinct producer/reviewer people from concrete inputs, expected coverage and missing approvals. Do not self-register people or approve rights. The immediate development-singer observation can proceed while this decision is pending. |
| **P0.3** | Hand the user the kit and conduct the 45–60-minute observation: unfamiliar song, voice choice, listening, one expressive edit/undo, save/reopen, master/stem export and external playback. | Hash-bound session receipt, each step marked unaided/help-needed/blocked/not-observed, three ranked problems, and the user's decision whether sound quality or workflow is the larger gap. This procedural observation is not canonical Alpha. Run destructive recovery/missing-resource probes only on disposable copies in engineering rehearsal. |
| **P0.4** | Map the dominant observed failure to the smallest U19/U20/U22/UI repair; declare the expected before/after change before editing. | Same-material comparison, focused regression, repeat observation and independent technical review. If no repair-worthy defect is observed, record the result and choose the next original unmet capability explicitly. Do not make subjective DSP changes before user input or lower criteria. |
| **Follow-up after bank approval** | Complete W01's appropriate sample-path adapter/material and W04's canonical production-bank checkpoint. Preserve the unchanged Q1–Q3 questions and controlled variables/time slots. | Manifest-bound reviewed sample packet and separate named answers; then a real unaided UA-001–020 session with valid evidence, sample hearing, external audible export and 30 minutes. Current Q1 refusal remains honest until its sample-path properties exist; do not auto-drop Q1 to claim the full packet complete. |

Suggested engineering goal wording for the next execution run:

> Fetch/pull safely and preserve existing work. Prepare and rehearse a native macOS observation kit from the current rebuilt apps and one exact installed development procedural singer, with a new 30-second Japanese lyric/melody task, saved project, preview/master/stem outputs, hashes and observation form. Prepare the concrete W05a production-bank route/rights/producer-reviewer decision dossier in parallel. Present the kit for the user's 45–60-minute observation before subjective sound changes. Record the ranked problems and whether quality or workflow is the larger gap; repair the largest reproducible defect against a stated same-material before/after criterion, with focused checks, independent technical review and prompt commit/push. Keep the procedural session engineering-only. Preserve W01's unchanged full sample-path Q1–Q3 and all canonical UA/U/R/V/Windows/host/rights/qualification/release obligations; their bank/person dependencies remain explicit follow-up acceptance work.

**Binary engineering preparation exit:** usable native kit, exact identities and retained outputs, native rehearsal receipt, concrete bank decision dossier, focused checks and pushed units. **Observe-and-fix goal exit:** actual user observation and ranked decision, then the selected repair/re-observation or a reviewed explicit no-repair finding. If user input is unavailable, preparation can finish but the full observe-and-fix goal remains pending. **Later product checkpoint exit:** actual approved sample bank and unaided 20/20 evidence. **Full Beta exit:** remains the original complete contract.

## Priorities after that slice

| Priority | Remaining package | Why / exact acceptance obligation |
|---|---|---|
| **P1 — neural product delivery** | U14/U35–38/W09: typed model installation and dependency/helper admission; actual lawful selected learned candidate; independent combined-model cohort; learned editable performance. | Current typed installation explicitly refuses. Training experiments and ONNX fixtures do not close installed singing, model quality or automatic assistance. |
| **P1 — complete musical coverage** | U6–8/U16–17/U25–28/U30–34/U39–41: close positive capability cells, needed compiler/expression/interchange/native workflows and language/character review. | Select increments from observed failures and original exits. Refusal is correct safety behavior but must coexist with every mandatory tested positive feature in the frozen matrix. |
| **P1 — governed qualification** | U42/U43: final asset/source rights, 13 pending profile/matrix entries, 60 phrases per language, three songs, five creators and real independent reviews. | Product usefulness and frozen criteria remain the critical path. Engineering evidence can prepare inputs, not approve people/rights or listening on their behalf. |
| **P1 — semantic/recovery closure** | U14/U44–46: typed dictionary/character candidates/install and packaging/recovery follow-ups; actual sample/model/backing capture where applicable, native operation/measurement replay, external role proof, U60 installed privacy/recovery, shared promotion predicate. | Complete the code gaps and current reconciliation obligations; preserve soak-context and authority refusals. No premature hold removal. |
| **P2 — installed release qualification** | U47: clean signed/notarized install/update/uninstall, real accessibility, all nine hosts and 30/120-minute workloads on both platforms. | Prepare macOS artifacts now; Windows stays TODO until its environment is available. Final full Beta cannot waive Windows or host tuples. Priority denotes sequence, not optional scope. |
| **P2 — final decision** | U48: restore/rehash externally anchored raw archive; EB-001–009; independent defect/release-role decisions. | EXTERNAL_BETA_READY only from the actual complete lineage. External cohort CLOSED and public activation are later distinct operations. |

Verification should match changed risk: dependent build plus focused behavior/refusal tests for each unit; sanitizer scope when concurrency/memory ownership changes; broad matrix at justified integration milestones. RS-11's measured loop had 0.039s no-op, 2.424s leaf, 54.705s shared-header and 5.656s documentation-commit builds. It did not support a general build-identity rewrite. The present evidence points to missing product observations and qualified inputs as the central sequencing problem, while retaining strict verification for data loss, trust, stale audio and publication races.

One lower-priority reporting defect should be repaired before any accepted UA row is displayed: [release_status.py](../../tools/external_beta/release_status.py:106) counts `COMPLETED`, while [the canonical verifier](../../scripts/verify_usable_alpha_contract.py:13) accepts `PASS`. Current 0/20 is independently derived directly from the ledger and is unaffected today. The seven-row status page is a selected-claims summary, not a project denominator, and its zero TARGET_PASS count must not override the actual scoped runtime passes above.

## Review and publication

Critical technical review completed with the existing **Review in English** chat, `01a11bad-88f2-7521-849e-c38111dc2197`, under the user's prior review/discussion authorization. **APPROVED after required corrections:** the reviewer explicitly authorized publication once U14 dictionary/character/packaging residuals and the neural campaign's generated-teacher source/boundary were added, with no further review round required. Both corrections and the two optional link-line fixes were applied and checked.

The discussion also retired 54% as a current progress claim and moved the native development-singer observation before W05a production approval. The reviewer independently verified the historical campaign/compare hashes and withdrew its earlier overbroad “no trained candidate/run” claim. It also withdrew a suggestion to omit Q1; the full sample-path question set stays unchanged. The reviewer checked links, all 48 anchor paths, branch-path availability, installation refusal and fixed timing floors; the fresh build/tests/evaluator runs belong to the lead and were not rerun by the reviewer. AI technical review does not constitute musical, legal, accessibility, DAW, producer or release-role acceptance.

Only this assessment and its evidence metadata are part of this documentation unit. Product/source implementation, canonical acceptance states, `.github`, Windows TODO and the unrelated stash remain unchanged. Publication follows the repository's staged-diff, commit/push and three-way remote-identity checkpoint.
