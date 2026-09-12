# SEAM needs complete singer workflows, not another foundation-only milestone

## Executive Summary

SEAM has a substantial, reusable engineering foundation. It is not a nearly finished full-scope virtual-singer product. The appropriate next move is to keep the architecture and change the unit of execution: finish a connected, audible creator outcome across several modules before declaring the next milestone complete.

- **The local baseline is healthy.** The current source commit matches `origin/master`, and a fresh local Release build followed by the complete registered CTest run passed **121/121 targets**. The core executable reported **869 passed, 0 failed**. These are engineering results, not musical or installed-release acceptance.
- **The product's critical path remains unfinished.** Production neural inference, a qualified original singer resource set, complete multilingual pronunciation, several advanced audible controls, Follow Host final rendering, synchronized character performance, and full installed qualification still require work.
- **The historical roadmap acceptance is 5/48 units, or 10.4%.** That is what the execution ledger records for U1–U5. It is neither a credible estimate of implemented code nor a claim that 89.6% of the source must still be written. Many later units contain substantial implementation.
- **Recommended revision: six outcome-focused milestones.** First complete an original voice-to-bank-to-new-song workflow. Start model/data preparation early, then deliver real neural singing, multilingual expression, complete creator UX, installed host reliability, and final independent qualification. Preserve all R1–R20 requirements and all 48 units underneath these milestones.

My assessment is **advanced engineering alpha, incomplete full product**. This is an analytical description, not a replacement release state. Full-Scope Beta GO remains **NO-GO**, while meaningful development can continue immediately.

## 1. The overall goal remains the complete original virtual singer

The agreed goal is larger than a sample player, a voicebank editor, a procedural tone generator, or a neural-model wrapper:

> A creator can sculpt an original female singing voice without supplying a recording, or start from authorized recorded/generated material; edit and turn that material into immutable reusable singer resources; write, import, tune, and finish expressive Japanese, English, and Korean songs; and use the same dependable product in the required standalone and DAW environments.

The resource can use a qualified classical, procedural, or neural implementation where the approved capability matrix permits it. That does **not** permit declaring a mandatory feature unsupported everywhere. In particular, the original neural singer and the recording-free Voice Designer are both required; one does not replace the other. Japanese-first is development order, not a reduced Beta definition. [Approved product contract](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md:23)

| Goal group | Mandatory outcomes retained | Requirement IDs |
|---|---|---|
| Musical correctness | Whole melody, sequential syllables, phoneme timing, articulation, persisted and audible expression | R1, R2 |
| Voice creation and production | Synth-style original female voice; real and generated input; reproducible editing, review, package and installation; range/style coverage | R3–R6 |
| Languages and backends | Actual Japanese/English/Korean pronunciation and resources; dependable classical rendering; qualified original neural singing on both platforms | R7–R9 |
| Performance and creation | Editable generated takes, preserved manual intent, harmonies, accessible native editing and safe USTX/SMF exchange | R10–R12 |
| Hosts and presentation | Standalone/CLAP/VST3/AUv2, nine DAW tuples, useful identity and synchronized character performance | R13, R14 |
| Reliability and evidence | Bounded/cancellable/recoverable execution; acoustic, listener and creator proof; installed release and U60 support completion | R15–R17 |
| Trust and complete workflows | Non-bypassable full-product gate; applicable rights/provenance; connected UI/CLI/source-to-bank-to-song lifecycle | R18–R20 |

The final acceptance is an exact candidate passing the amended `EXTERNAL_BETA_READY` audit. The later external cohort and `EXTERNAL_BETA_CLOSED`/`PUBLIC_ACTIVE` operations remain subsequent events. They must not be fabricated to make a pre-GO plan appear complete. [Definition of Done](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md:1144)

## 2. Current progress: separate code, workflows, and qualification

### 2.1 What can actually be measured today

| Measure | Current evidence | Correct interpretation |
|---|---|---|
| Source preservation | HEAD and remote master both `12ad16a0c7566f5ea2054fcd581c90f8dce02731`; clean tree before this report | The inspected committed source is preserved remotely. This cannot prove that no work was lost before the preserved checkpoint. |
| Historical local unit acceptance | U1–U5; **5/48 = 10.4%** | Recorded unit acceptance, not a fresh reacceptance of all 48 units or an effort-weighted completion percentage. |
| Current local regression | **121/121 CTest targets; 100% of this registered run passed** | Current configured macOS Release regression health. Some tests intentionally prove that invalid release candidates are rejected. |
| Core regression | **869 passing cases** | One test executable's cases. Do not add it to the CTest denominator or sum overlapping focused suites as independent coverage. |
| Full-product registry | 20 requirements and 83 acceptance cases | Scope size, not 83 successful real-product cases. |
| Released-resource matrix | `UNRESOLVED`; **0 registered released resources** | Development recipes, samples, candidates and technical banks exist. None is registered as a released resource in this canonical matrix. |
| Evaluation profile | 18 fixed criteria/protocols; **11 unresolved empirical criteria**; 29 total | Threshold/profile preparation is incomplete. All 11 empirical criteria still have null values. |
| Installed target coverage | Two platforms and nine required DAW tuples | No new installed-host qualification was performed in this review. Source/platform fixtures do not establish the final tuple matrix. |
| Full-product evidence | Canonical `evidenceStatus: NOT_RUN` | The full product has not earned release acceptance. This does not mean all engineering tests are unrun. |

Sources: [historical ledger acceptance](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/implementation/FULL_SCOPE_BETA_EXECUTION.md:1243), [resource matrix](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/product/full-product-beta-contract.json:13), [evaluation profile](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/product/full-product-beta-contract.json:4417), and [captured current audit](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/build/progress-review-evidence-2026-09-12/audit-snapshot.json).

### 2.2 Why I am not assigning a new overall “65%” or “95%”

There is no measured, effort-weighted remaining-work model, and the unfinished work is highly unequal. A helper manifest decoder, a language's phonetic resource inventory, training a useful singer, and nine-host qualification are not equal-sized tasks. Lines of code, passing tests, and the number of continuation entries cannot supply that missing denominator.

The earlier narrow-roadmap percentages are also not comparable with the current 48-unit full-scope goal. Reusing them would mix different products. Conversely, interpreting 10.4% historical acceptance as “almost nothing has been implemented” would discard the large amount of later partial implementation.

The useful conclusion is: **much of the infrastructure exists, but several central product capabilities do not yet work end to end or lack the resources needed to prove them. This is substantial completion work, not a final five-percent polish pass.**

## 3. Existing code maturity, by subsystem

These are evidence-based implementation assessments, not new unit acceptances. “Substantial” means important production paths and regression coverage exist; it does not mean release-qualified.

| Subsystem | What is implemented | What still prevents completion |
|---|---|---|
| Score/domain/persistence | Typed musical intent, revisions, ownership, take/selection state, migrations, undoable edits and pronunciation reconciliation | Requalify integrated behavior as later backends and native workflows join it; retain migration and manual-intent guarantees. |
| Timing and performance | Ordered timing, independent score voices, whole-note melody, pitch/vibrato/dynamics and attack/release evaluation | Broad acoustic evidence across actual resources, transitions, languages, pitch range and tempo changes. |
| Classical rendering | Raw, PSOLA, spectral and stretch dispatch; compiled performance consumers; source alignment and explicit fallback/capability checks | A dependable qualified default, robust real-bank transitions and remaining advanced timbre/style semantics. |
| Render lifecycle | Immutable snapshots, context/owned-frame separation, cancellation, cache identity, procedural and sample dispatch, final export | No neural dispatch; complete backend-specific resource/provenance integration and installed scheduling proof. |
| Producer repository | Durable generations, writer exclusion, stale-write checks, per-source provenance, QC/review/retake mechanics, candidate publication | Full real and generated production journeys with actual qualified material and independent decisions on both platforms. |
| Voice Designer | Versioned source-filter recipes, phonation/resonance/noise/nasal/plosive controls, preview, A/B, undo, save/reopen and batch generation | Complete useful articulation, coarticulation, range and style coverage; reviewed female identity; intuitive calibrated presets and creator evidence. |
| Real input | Native recording actions, bounded recording buffer, CoreAudio/WASAPI implementations and producer import path | Current hardware-backed capture and failure/recovery qualification on both targets; no microphone was tested in this review. |
| Languages | Shared language routing; Japanese reading identity/helper path; English bootstrap lexicon and estimates; Korean decomposition and boundary rules | Larger reviewed lexical/G2P resources, exceptions, complete singer phone coverage and native-speaker judgments. |
| Interchange | Bounded SMF Type 0/1 PPQ and typed USTX 0.9 subset codecs, conversion reports and authoring service | Complete installed/native round trips, clear loss review, external-application comparison and creator acceptance. |
| Neural singing | Request/response protocol, vocabulary binding, conditioning, process runner, signed descriptor/package checks and DiffSinger input conversion | Actual runtime, model/vocoder bundle, graph admission, trained/adapted original model, pipeline dispatch and qualification. |
| Automatic takes/harmony | Revision-bound proposals and acceptance ownership; deterministic four-channel generator; undoable interval-harmony preparation | Musically useful qualified generation, complete comparison/regeneration UI, advanced expression and musical evaluation. |
| DAW runtime | CLAP live infrastructure, timeline mapping, Final-only offline readiness gate and stale-output rejection | Follow Host final rendering is explicitly unsupported; required installed host/wrapper and offline-bounce proof remains. |
| Character | Identity package, state assets and native operational status presentation | Phoneme/audio-synchronized performance and production-approved assets; current development asset manifest explicitly says it is not production. |
| Release/support | Existing support/crash foundations, payload verification, EB-009 semantic reader, restored-release audit integration and negative tests | Finish U60 acceptance, freeze resources/criteria, qualify exact signed installations and complete genuine restored acceptance. |

### 3.1 Strong existing work that should be preserved

**The musical compiler is real implementation, not a declaration.** `compileScorePerformance()` and `compileScoreVoices()` are consumed by rendering. Tests exercise a melody crossing two notes inside one acoustic unit, staccato release, continuation, unvoiced preservation, and manual-versus-generated ownership. Keep this score-first architecture. [Compiler](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-synthesis/src/performance_compiler.cpp:102), [audible regression](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_performance_compiler.cpp:350)

**The producer lifecycle has a genuine technical success path.** A regression invokes CLI review and publication, signs with a fixture key, installs a package, makes the original producer inputs unavailable, reopens a new score and exports matching master/stem audio. This is stronger than an isolated serializer test. However, its source is a synthetic single-unit fixture; it proves lifecycle independence, not unfamiliar-lyric coverage or an actual independent musician's review. The native review suite also publishes a candidate while retaining `releaseEligible=false`. [CLI-to-installed-song test](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_sample_review_cli.cpp:125), [native publication test](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_studio_sample_review.cpp:172)

**The procedural route already reaches normal rendering.** The pipeline builds an articulated or sustained stream from an immutable recipe and compiled performance; it does not merely play a demonstration oscillator. Designer tests cover audible resonance changes, exact undo, resource persistence, preview invalidation and supported consonant sources. [Pipeline](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-rendering/src/render_pipeline.cpp:83), [Designer regression](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_voice_designer_workflow.cpp:626)

**The full-product validator now has both rejection and success tests.** A complete synthetic 83-case/175-cell report is accepted under its synthetic frozen contract, while changed audio or measurements are rejected. The same report does not pass the unresolved canonical contract. The September 9 concern that a validator might only have rejection paths must not be repeated as an unchanged finding. Synthetic success still does not approve the real singer. [Semantic validator regression](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/external_beta/test_full_product_report.py:126)

## 4. The most important remaining problems

### 4.1 Neural infrastructure is ahead of neural capability

This is the largest clearly missing software capability, not merely a missing review signature.

- `PhraseRenderPipeline::render()` admits procedural and sample resources, then rejects other resource families. There is no production neural branch.
- `NeuralSingerResource` currently owns one opaque frozen model blob. It is not an admitted acoustic-model/vocoder/vocabulary/configuration bundle.
- CMake registers the neural library and a **test probe**, not a first-party production inference worker with an ONNX runtime.
- The probe explicitly allocates zero-filled PCM. Passing its protocol tests proves transport, correlation and bounds—not singing.
- The planned `tools/voice_model_training/` production workflow is absent from the tracked tool tree. The pinned DiffSinger entry is a development interface reference, not a shipped runtime or trained singer.

[Dispatch rejection](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-rendering/src/render_pipeline.cpp:150), [resource type](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-synthesis/include/seam/synthesis/singer_resource.hpp:39), [build registration](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/CMakeLists.txt:338), [zero-PCM probe](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/helpers/neural_worker_probe.cpp:46), [dependency reference](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/third_party/manifest.yml:44)

**Required code change:** add a validated model bundle and a controlled first-party inference worker; join it to frozen score snapshots, preview/final rendering, cancellation, cache identity, project persistence and installed payloads. Build the dataset/train/export/qualification path alongside that integration. Do not count another protocol guard as delivery of the neural singer.

### 4.2 A configurable voice generator is not yet a complete singer

The current recipe supports useful building blocks: phonation, formants, aspiration/modulation, nasal resonance, frication and mixed voiced frication. The explicit plosive binding allows `p`, `t`, and `k`. The articulation compiler still requires supported explicit bindings and rejects gestures outside their owning note. These limits matter for connected speech and consonant transitions. [Recipe](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voice-design/include/seam/voice_design/voice_recipe.hpp:12), [articulation admission](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voice-design/src/articulation_plan.cpp:10)

Remaining work includes voiced stops, unreleased stops where needed, affricates, liquids/glides and other required phone classes, context-sensitive transitions, and complete pitch/style coverage. The exact implementation should follow the chosen language inventory; it should not become a list of arbitrary new knobs.

Most importantly, “female” must be established by the produced voice and the agreed identity rubric, not by a preset name or formant setting. A synthetic voice can be an original designed instrument; this review does not establish that the present source-filter method will meet every required quality target without further method changes.

**Required code/resource change:** expand the articulation representation and renderers around a coherent phonetic inventory, then bake/edit/review/install that inventory and sing held-out lyrics. Use acoustic and listener results to choose the next DSP repair. Preserve explicit unsupported cases instead of relabeling noise as a missing consonant.

### 4.3 Persisted expression is ahead of audible expression

The classical capability constructor enables six controls: pitch, timing, dynamics, vibrato, attack and release. Formant, breathiness, tension, airiness, gender, style blend and growl remain false in that table. A stored value or visible selector does not establish its sound. [Capability construction](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-synthesis/src/renderer_capabilities.cpp:9)

The automatic-performance implementation is explicitly a deterministic contract backend. It produces pitch/dynamics/attack/release proposals and rejects other channels; small seeded pitch offsets are not a trained expressive performance model. Interval-based harmony commands are useful groundwork, not complete musical arrangement intelligence. [Generator](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-synthesis/src/automatic_performance.cpp:44), [harmony tests](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_harmony_workflow.cpp:32)

**Required change:** define each control's unit, neutral value, modulation range, backend/resource applicability, persistence, ownership and acoustic test. Implement at least one qualified combination for every mandatory expression. For style blend, selecting one style is not enough: paired compatible material and audible interpolation must be demonstrated.

### 4.4 Multilingual routing is not multilingual product completeness

The English source contains **26 bootstrap dictionary entries** plus a warned spelling-estimate path. Korean source includes Hangul decomposition and useful surface-boundary rules, but explicitly states that lexical exceptions, suffix/compound distinctions and irregular stems require a dictionary. Japanese reading has bounded identity/projection machinery, but that alone does not qualify pronunciation or matching singer coverage. [English lexicon](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-phonemizer/src/english_phonemizer.cpp:25), [Korean limits](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-phonemizer/src/korean_phonemizer.cpp:101), [Japanese projection](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-phonemizer/src/japanese_reading.cpp:26)

**Required change:** finish the reviewed language resources and map their phones/stress/context to actual singer inventories. Exercise ordinary unfamiliar lyrics, lexical exceptions, cross-note syllables and explicit user corrections. Preserve warning and override ownership when resource versions change.

### 4.5 Follow Host final rendering is explicitly unfinished

`EditorRuntime::prepareOfflineRender()` fails the Follow Host branch with a diagnostic requiring a complete authoritative tempo map. Fixed Audio preparation and stale-publication checks are valuable, but they do not satisfy the full host timing requirement. [Explicit unsupported branch](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-clap-editor/src/editor_runtime_project.cpp:181)

**Required change:** capture and version the required host timeline/range information outside the realtime callback, invalidate affected audio correctly, prepare the exact final range, and reject a bounce if required current vocals are unavailable. Qualify this through actual host transport, loop, seek, tempo-change and offline-bounce behavior. An instantaneous BPM must not masquerade as a complete tempo map.

### 4.6 Character presentation currently represents application state, not singing performance

The character contract exposes Neutral, Focused, Rendering, Complete, Warning and Error assets. The native controller derives these from playback/readiness/error state. That is useful operational feedback, but it is not phoneme- or audio-synchronized character performance. The inspected production-development asset manifest is explicitly `developmentOnly=true` and `NOT_A_PRODUCTION_TURNAROUND`. [Character contract](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-character/include/seam/character/character.hpp:14), [state selection](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/editor_controller.cpp:220), [asset status](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/assets/character-01/production-development/asset-manifest.json:10)

**Required change:** add a lightweight performance presentation snapshot bound to the exact singer, rendered phrase, playhead and pronunciation. Drive mouth/energy/expression states from that snapshot; preserve sensible stop/seek/missing-audio behavior and reduced-motion accessibility. Use approved portrait/thumbnail/state assets at appropriate sizes. This does not require inventing an unlimited 3D animation platform.

### 4.7 Qualification inputs are incomplete, even though gate code exists

The canonical resource matrix is empty and unresolved. Eleven empirical criteria remain unresolved: acoustic boundaries, expression tolerances, pronunciation scoring, identity rubric, generation budgets, neural budgets, resource limits, cancellation budgets, reference machines, source volume and reproducibility tolerances.

These are not all “write another validator” tasks. They require real source/model pilots, chosen reference machines, calibrated measurements and independent reviewers. The public-domain human demo bank explicitly says it is a technical fixture, not Official Voicebank 01. [Fixture disclosure](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/assets/demo-human-voicebank-public-domain/README.md:1)

There is also reporting drift: the canonical contract still contains `semanticValidation.status: UNAVAILABLE`, while the implementation and its positive-path tests now exist. Reconcile that metadata through the proper contract revision and acceptance process; simply flipping it to available or frozen would not finish U45 or create missing evidence. [Canonical metadata](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/product/full-product-beta-contract.json:7303)

### 4.8 Verification boundaries and maintenance risks need clearer ownership

- The neural descriptor uses `windows-x64`, while the full-product matrix uses `windows-x86_64`. An explicit, tested deployment-to-product-platform mapping is needed when these systems join. This is an integration risk, not a reproduced current launch failure. [Descriptor admission](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-neural-synthesis/src/deployment_descriptor.cpp:53)
- The license auditor invokes a master-only/local-branch policy. That policy is distinct from dependency-license correctness and clashes with this preserved development checkout. Use a policy-compliant release verification checkout; do not delete the user's branches or disguise the failure as a licensing defect. [Branch policy](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/scripts/verify_master_branch.py:50)
- Large shared files concentrate integration risk: the editor controller is 5,403 lines and the Studio executable is 1,854 lines at this snapshot. Extract responsibilities when implementing the relevant workflow, with characterization tests. Do not launch an unrelated whole-application rewrite.
- The execution ledger is 2,247 lines and mixes historical checkpoints, later continuations, and unit identifiers from older plans. A “U35” Phase 12C evidence entry is not completion of Full-Scope U35 training. Use plan-qualified IDs and one current status summary.

## 5. Why the development cadence needs to change

The previous incremental work repaired real problems: stale publication, unsafe bounds, source identity, ownership loss and reproducibility. Those safeguards should remain. The problem is that many recent increments stop at an internal boundary, with the associated user capability still absent.

The revised execution rule should be:

> Plan and review a complete user outcome as the milestone. Implement it through small, recoverable commits. Close the milestone only when its happy path, principal failure paths, and retained artifact work together.

This changes the **reporting and integration unit**, not the safety standard. A large step must not mean a giant untested diff, skipped review, or synthetic approval. Conversely, adding one more hash check should usually be reported as progress inside the active milestone rather than as a new completion event.

Use a **one-to-three-week outcome window as a planning cadence**, not a delivery forecast. Review an audible/runnable checkpoint every few working days. A research-heavy model or resource milestone may span several windows; it should still have one clear user-facing objective. After the first singer and neural pilots, estimate remaining work from measured iteration time, source retake yield and model behavior.

Do not promise a Beta date before those pilots. There is currently no defensible conversion from the historical 10.4% acceptance figure to calendar time.

## 6. Revised roadmap: six larger outcome-focused milestones

The following is a proposed execution revision. It preserves the original requirements, 48-unit checklist, and acceptance thresholds. Primary ownership below is bookkeeping: dependencies and final qualification may cross milestone boundaries. The milestone number is not a strict serial scheduling rule.

| Milestone | User-visible result | Primary remaining unit ownership |
|---|---|---|
| M1 — Complete one original singer loop | Sculpt an original voice, bake/edit/review/install reusable material, and export a new short song from it | U6–U16, U18–U22 |
| M2 — Real neural singing | A qualified candidate model/vocoder runs through the native worker and normal song rendering | U35–U37 |
| M3 — Multilingual expressive singing | Required language inventories, style blending, advanced expression and useful editable automatic performance work audibly | U17, U26–U28, U38–U39 |
| M4 — Complete creator workflows and singer presentation | Native tuning, interchange, takes/harmonies, character performance and final singer resource assembly form a usable product | U23–U25, U29–U32, U40–U42 |
| M5 — Installed host and recovery reliability | Exact installed surfaces behave correctly in the required DAWs, including final audio and recovery | U33–U34, U44, U47 |
| M6 — Independent qualification and Beta GO | All required evidence passes against the frozen candidate and restored archive | U43, U45–U46, U48 |

U1–U5 remain the historically accepted baseline to preserve and regress. The ownership table assigns every remaining unit once: **5 baseline + 16 + 3 + 6 + 10 + 4 + 4 = 48**. It does not count the same unit as completed twice when it contributes to multiple demonstrations.

### M1 — Complete one original singer loop

**Outcome:** a creator opens Designer, sculpts a reproducible original voice, generates reusable material, edits it in Studio, obtains genuine review, installs the resulting bank and finishes a new 16-bar song whose lyrics were not copied from a generation fixture.

**Implementation bundle:**

1. Select one original singer identity, one initial language and a deliberately declared initial range/style for the pilot. This is an internal pilot, not a reduced Beta scope.
2. Build its actual inventory from the language's required phone classes and transitions. Use coverage failures to drive articulation work; extend closure/burst/voicing/transition gestures where the pilot requires them.
3. Preserve the existing `VoiceRecipe` → `ArticulationPlan`/stream → generation → canonical producer repository path. Complete any missing marker, conditioning, review and package steps using those owners.
4. Exercise recording/import through the same production lifecycle with a separate authorized real-source take. Do not relabel a procedural sample as human source or treat a declared permission as legal verification.
5. Use one chosen classical backend as the reference for the installed sample-bank demonstration. Measure actual pitch, timing edits, joins and range; fix the dominant audible defects.
6. Complete the native happy path and cancellation/retake recovery needed for this outcome. Reopening an old song must retain its original bank/recipe identity after new voice edits.

**Key code owners:** `libs/seam-voice-design/`, `libs/seam-voicebank-production/`, `libs/seam-synthesis/`, `libs/seam-rendering/`, Studio native actions and the voicebank CLI.

**Exit evidence:** recipe and inventory identities; generated/edited takes; review and publication receipts; installed-resource identity; reopened song; dry vocal/master/stem audio; coverage and pitch/timing results; one interruption/retake replay. Automated fixture decisions remain labelled fixtures; they cannot stand in for the genuine review.

**Do not stop at:** a new phone generator, a valid candidate JSON, a one-vowel technical bank, or a green transport test. If independent review is unavailable, finish the internal technical loop and report that exact acceptance tail while continuing M2 or other independent work.

### M2 — Deliver real neural singing, including its data/model workflow

**Outcome:** an admitted original model and compatible vocoder synthesize held-out lyrics through SEAM's normal rendering pipeline, not a separate demonstration script.

**Implementation bundle:**

1. Introduce a typed, immutable neural bundle binding acoustic graph, optional required variance graphs, vocoder, token vocabulary, configuration and exact hashes. Validate sample rate, hop size, mel representation, shapes and optional controls as a compatible set.
2. Add the first-party worker executable and pinned native inference runtime through dependency intake. Admit graph/schema/operators and external tensor locations under explicit bounds before execution; qualify the supported graph family instead of claiming arbitrary model compatibility.
3. Reuse the existing request builder and DiffSinger acoustic adapter. Complete acoustic → mel → vocoder → PCM execution, trim acoustic padding, apply the declared sample-domain gain semantics once, and validate output rate/channel/frame/finite-value constraints.
4. Add neural snapshot creation and pipeline dispatch; resolve model identity from the project/resource selection; integrate preview, final export, multi-voice scheduling and cache invalidation outside the realtime callback.
5. Add reproducible dataset preparation, segmentation/alignment, duplicate/leakage-resistant train/validation/test splits, training/adaptation, export and qualification commands under the planned model-production tools. Bind source rights, preprocessing revisions, seeds/configuration and resulting assets. Do not assume a permissive code license also approves model weights, training data or voice identity.
6. Finish signed descriptor materialization, platform-ID mapping, runtime dependency resolution and installed surface packaging. Verify signing/hash ordering against final installed bytes, not unsigned staging files.

**Proposed new owners, not existing completed files:** `tools/voice_model_training/`, a production neural-worker app, and typed neural bundle/graph-admission code. Extend existing neural, rendering, domain/formats and payload owners instead of creating a parallel song engine.

**Exit evidence:** actual nonzero intelligible held-out vocal audio; exact model/vocoder/vocabulary identities; training/export reproduction; measured CPU latency/memory/cancellation on both targets; corrupted model and worker crash/deadline rejection; save/reopen/preview/final parity.

**Scheduling:** start authorized source preparation, pilot alignment and model feasibility during M1. Do not wait for every UI detail before discovering whether the intended model/data approach produces a useful singer. A small diagnostic model may unblock runtime integration, but it is not the qualified original singer required by U36.

### M3 — Complete multilingual expression and useful automatic performance

**Outcome:** the required Japanese, English and Korean workflows produce understandable singing, and every mandatory expression has a supported, audibly effective resource/backend combination.

**Implementation bundle:**

1. Replace bootstrap-only coverage with reviewed language resources and tested lexical/contextual rules. Bind dictionary/phonemizer/model phone vocabularies and preserve explicit user corrections across revisions.
2. Complete contextual source choice, compatible paired styles and style blending. Keep style selection and continuous blending as distinct behaviors.
3. Implement advanced controls with explicit units, neutral behavior, automation interpolation, source/model applicability and failure reporting. Update capability reporting only after the selected path actually applies the control.
4. Replace the contract-only automatic performance experience with a musically evaluated generator. Preserve proposed-versus-accepted state, channel/range locks, stale-revision rejection and partial regeneration.
5. Validate manual vibrato and generated F0 ownership together; do not double-apply pitch effects or erase hand tuning when accepting a new take.

**Exit evidence:** unseen phrases for each language; reviewed phone coverage; neutral-versus-changed dry-audio comparisons for every required control; paired-style evidence; a complete propose/compare/accept/correct/regenerate round trip with exact undo.

**Dependency:** use M4's minimum inspector/take controls as needed. This milestone and M4 are coordinated integration work, not isolated sequential silos.

### M4 — Finish creator UX, interchange, character, and resource assembly

**Outcome:** a creator can complete normal song and voice-production work without developer-only commands, missing panels or ambiguous states.

**Implementation bundle:**

1. Finish note/lyric/tempo/meter editing, expression inspection and take/harmony workflows using shared commands. Surface unsupported/resource-missing states without silently discarding intent.
2. Complete native USTX/SMF import/export review: show retained versus lossy fields, preserve cancellation/stale-document safety and verify actual external-application round trips for the declared subset.
3. Conduct a focused native layout pass. Treat overlapping notes as selectable musical objects, with explicit overlap inspection; use measured text, wrapping/ellipsis with full accessible text, scrollable detail and shared paint/hit-test/accessibility geometry. Test dense chords, tiny notes, long three-language lyrics, IME composition, large style IDs, long paths and error messages at minimum and common window sizes.
4. Add the character performance snapshot and approved resource set. Keep status feedback distinct from performance; match portraits, thumbnails and performance visuals to their spatial role. Respect reduced motion and preserve editor space.
5. Assemble the final resource matrix across the original recipe, real/procedural samples, neural model, dictionaries and character. Each advertised range/style/language claim must have a supported tested combination.

**Exit evidence:** complete native song and producer journeys on both platforms; readable layouts and keyboard/accessibility behavior; score/audio round trips; take/harmony editing; correct stop/seek character behavior; immutable versioned resource candidates.

This review did **not** perform a fresh full SEAM UI inspection. Note overlap and text overflow are explicit verification targets from the user's requirements, not newly reproduced bugs asserted by this report.

### M5 — Qualify exact installed host and recovery behavior

**Outcome:** the same singer and song behave dependably in standalone and all nine required DAW tuples.

**Implementation bundle:**

1. Complete Follow Host authority and offline range preparation; preserve Fixed Audio as an explicit alternative. Never bounce stale, incomplete or old-tempo vocals as successful final output.
2. Finish live event addressing/expression semantics and exercise wrapper-specific behavior through the installed CLAP/VST3/AUv2 surfaces.
3. Finish U60's crash/recovery/support acceptance: failed startup, cancellation, partial production, resource replacement, safe recovery and privacy-controlled diagnostics. Keep user attachments separate from automatically generated support material.
4. Materialize and sign exact packages, install them, and exercise helper/dictionary/model resolution with unrelated working directories and no development PATH assumptions.
5. Run required host/device/recovery/soak workloads and collect effective binary/resource identity. A Linux fixture, macOS unit test or short smoke soak cannot substitute for the specified Windows/DAW/full-soak workload.

**Exit evidence:** nine explicit tuple results, standalone results on both platforms, exact installed hashes, current final exports, observed transport and recovery behavior, applicable signing/installation proof and required full-duration soaks.

Start host smoke testing earlier as an integration signal. Final M5 acceptance must use the candidate whose identity M6 will audit; changed effective binaries/resources invalidate affected evidence.

### M6 — Finish independent qualification and issue Beta GO

**Outcome:** the frozen product passes the real full-scope acceptance, not a synthetic report shaped like a passing candidate.

1. Resolve and freeze all empirical criteria before final scoring. Keep pilot calibration separate from the held-out final material; do not relax thresholds retroactively after a failure.
2. Execute at least **60 short phrases per language**—at least 180 in total—and **three complete songs** spanning the required languages. Cover every mandatory capability and all advertised resource/range/style combinations.
3. Obtain the required native-language and listening judgments plus **at least five independent pre-GO creators**, including the real/generated producer journeys and counterbalanced unfamiliar manual-versus-assisted tasks.
4. Preserve the fixed pitch/timing criteria: median steady-voiced pitch error at most 30 cents; at least 90% of designated steady frames within 50 cents; deterministic 30 ms timing edits within one sample of the intended displacement. Retain octave-error, voiced-coverage and predeclared exclusion evidence separately.
5. Complete semantic validation, restored archive audit and promotion-path regression with the exact effective candidate. Resolve remaining gate metadata through a deliberate versioned contract change where required.
6. Authorized release roles issue `EXTERNAL_BETA_READY` only when all required conditions pass. The later cohort remains later.

**Exit evidence:** a reproducible, independently accepted full-product dossier and restored EB-001–EB-009 decision tied to the signed-installed candidate. The existing machine gate remains the authority; this report does not create another competing GO system. [Verification contract](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md:1067)

## 7. Recommended next development batch

**Primary batch: M1, “original voice to reusable bank to unfamiliar song.”** This makes the largest use of existing implementation and produces the most informative audible checkpoint.

Before adding more framework code, select the pilot's singer identity, language/inventory, intended range, source route and evaluation material. Then build the remaining articulation/resource pieces and execute the connected workflow. Use the existing CLI installed-song regression as a structural starting point, but replace the one-unit synthetic assumptions with a coherent pilot inventory and retain honest review boundaries.

**Parallel dependency track: early M2 data/model feasibility.** Prepare authorized source manifests and held-out splits, inspect the intended acoustic/vocoder export compatibility, and bring up the first real model through the worker. This may run alongside M1 when staffing/resources permit; it is not authorization to fabricate sources, approvals or model quality.

Do not begin another broad documentation program, general dependency-wrapper layer, animation platform or editor rewrite before these pilots answer the central question: **can this architecture produce a useful original singer and preserve a creator's work through the complete lifecycle?**

If the procedural pilot fails quality, change its acoustic method within the same goal. If the intended neural family fails export, runtime or quality constraints, select a compatible alternative through the existing intake and qualification process. Neither result authorizes reducing the product scope.

## 8. Execution rules that make the larger steps manageable

### 8.1 Keep small commits inside larger milestones

Commit verified implementation slices frequently and preserve source checkpoints. Report them as increments within M1/M2/etc. A milestone closes on an observable workflow and its retained evidence, not on a commit count. Publish only when authorized; this analysis turn does not push changes.

Use one current status table with plan-qualified unit IDs, implementation state, integration state, qualification tail, evidence identity and next owner. Keep the historical ledger as history. Do not append another ambiguous “U35 complete” without the controlling plan name.

### 8.2 Limit active integration work

Prefer one primary capability milestone plus one independent resource/model track. Follow the existing maximum of three implementation tracks if more people are involved. Serialize changes to shared schema, CMake, producer mutation owners and native controller/scene/semantics.

Extract a controller or producer responsibility only where the active feature exposes a clear boundary. Pair the extraction with existing behavior tests. Avoid a separate refactoring milestone that delays the audible outcome.

### 8.3 Use verification at the right level

| When | Verification |
|---|---|
| While changing one behavior | Build affected targets; focused happy/error/ownership regression; preserve the failing example that motivated the change. |
| When a connected workflow changes | Run its actual CLI/native/library path, compare PCM and persistence where appropriate, and verify cancellation/retake behavior. |
| At an integration checkpoint | Rebuild and run the full registered suite; run relevant sanitizer/race checks and source closure. |
| At a milestone exit | Retain the runnable/audible deliverable, measured budgets and required human/resource evidence; identify any unearned acceptance separately. |
| At final release qualification | Use frozen signed-installed artifacts, required hosts/machines and independently reviewed evidence; restore and re-evaluate the archive. |

Preserve safety boundaries. Optimize repeated verification around the affected behavior; do not eliminate negative tests or substitute a green fixture for a real outcome. A release rejection should stop promotion, not unrelated development.

### 8.4 Report progress with four separate fields

1. **Implementation:** which production code paths exist, and which are still missing.
2. **Workflow:** which named user journey was completed on the current build and resource set.
3. **Qualification:** which resource, listener, machine and installed-host evidence is accepted, pending, or invalidated.
4. **Regression:** exact current build/test result and source identity.

Do not average these fields into one percentage. If a percentage is shown, name its numerator and denominator directly beside it.

## 9. Complete 48-unit disposition and no-drop map

Legend: **L** = historical local acceptance recorded in the ledger; **P** = substantial or partial implementation, with unfinished unit scope; **F** = foundation/reference only for the unit's defining outcome; **Q** = primarily resource/qualification/final-acceptance work, not accepted. These labels describe this review's evidence and do not amend the ledger's acceptance.

| Unit | Current disposition | Main remaining work | Revised owner |
|---|---|---|---|
| U1 Baseline | L; current Release regression green | Preserve reproducibility and run later qualification on exact effective artifacts | Baseline |
| U2 Authority/matrix | L for contract implementation; actual matrix/profile unresolved | Freeze actual resources and empirical criteria through their owning units | Baseline/M6 |
| U3 Musical vocabulary | L | Preserve migrations, coupled ownership and exact undo through later integration | Baseline |
| U4 Pronunciation identity | L | Preserve shared reconciliation as full language resources are introduced | Baseline |
| U5 Ordered timing | L | Preserve authoritative source/target timing in all new renderers | Baseline |
| U6 Performance compiler | P, strong core/audio coverage | Integrated expression and full real-resource musical qualification | M1 |
| U7 Chunks/resources | P | Neural snapshots, admitted bundle types and complete backend/context identity | M1, with M2 dependency |
| U8 Capabilities/cache | P | Complete qualified capability matrix and all new-backend provenance | M1, with M3 dependency |
| U9 Production provenance | P, substantial | Finish actual source routes and complete producer acceptance | M1 |
| U10 Inventory identity | P | Complete reviewed language/style/range inventory, not fixture coverage | M1 |
| U11 Canonical writes | P, substantial | Cross-platform writer/recovery qualification across real jobs | M1 |
| U12 QC | P | Calibrated applicable QC on actual source material | M1 |
| U13 Reviews/retakes | P, connected native/CLI mechanics | Genuine independent current-material review and retake acceptance | M1 |
| U14 Candidate publication | P, successful synthetic install/export lifecycle | Complete real resource candidates and release-bound publication | M1 |
| U15 Acoustic analysis | P | Calibrated conditioning/alignment and reviewed real-corpus performance | M1 |
| U16 Classical processing | P | Dependable default quality over full supported workload/range | M1 |
| U17 Selection/style blend | P | Contextual quality and genuine compatible paired-style blending | M3 |
| U18 Recipe persistence | P, strong mechanics | Final recipe capability/provenance/resource acceptance | M1 |
| U19 Phonation/tract | P, working synthesis | Calibrated original identity, range and quality | M1 |
| U20 Articulation/baking | P | Missing required phone classes, transitions and full-inventory bake | M1 |
| U21 Generation/CLI | P, connected batches and producer path | Full resource-scale resume/retake/cancellation and creator proof | M1 |
| U22 Native Designer/input | P | Complete native generation and real-input journeys on both targets | M1 |
| U23 Musical editing | P | Complete tempo/meter/native workflow and current musical acceptance | M4 |
| U24 Lyric productivity | P | Full language/hint/IME/search and installed native workflow acceptance | M4 |
| U25 Expression inspector | P | Complete controls, applicability, take ownership and native usability | M4 |
| U26 Japanese | P | Qualified reading/resources, context coverage and native review | M3 |
| U27 English | P, bootstrap lexical coverage | Complete practical pronunciation/resource coverage and native review | M3 |
| U28 Korean | P, rule-based groundwork | Lexical/morphological exceptions, resource coverage and native review | M3 |
| U29 Interchange boundary | P, bounded codecs/service | Complete user-facing conversion/recovery qualification | M4 |
| U30 USTX | P, typed subset implemented | Real declared-subset round trips and explicit loss acceptance | M4 |
| U31 SMF | P, Type 0/1 PPQ implemented | Full native musical workflow and interchange acceptance | M4 |
| U32 Native conversion | P | Installed dialogs/review/cancel/save workflows and creator proof | M4 |
| U33 Live expression | P | Complete addressed semantics and actual host qualification | M5 |
| U34 Host/offline | P; Follow Host final path unsupported | Authoritative host map, final range preparation and bounce proof | M5 |
| U35 Dataset/training | F; planned production workflow absent | Implement and execute authorized reproducible preparation/training pipeline | M2 |
| U36 Model/export | F; no qualified original model registered | Train/adapt, export and qualify actual model/vocoder candidate | M2 |
| U37 Neural deployment | P in infrastructure; defining inference outcome missing | Real worker/runtime, bundle admission, pipeline and installed deployment | M2 |
| U38 Automatic ownership | P, proposal/acceptance foundations | Qualified generator and complete manual-preserving integration | M3 |
| U39 Advanced expression | F/P | Implement the remaining audible algorithms/backend controls | M3 |
| U40 Takes/harmonies | P, command groundwork | Full comparison/regeneration/harmony UI and musical acceptance | M4 |
| U41 Character | F/P, operational identity/status | Synchronized performance and approved assets | M4 |
| U42 Resource set | Q; released matrix empty | Complete original recipe/sample/model/dictionary/character candidates | M4 |
| U43 Musical/creator qualification | Q, tooling exists | Actual fixed corpus, songs, independent language/listener/creator studies | M6 |
| U44 Preserved U60 | P | Finish exact installed recovery/support/privacy acceptance | M5 |
| U45 Semantic audit | P, positive and negative synthetic tests | Complete canonical candidate evidence/semantics and reconcile availability metadata | M6 |
| U46 Restored promotion audit | P, restored evaluator integration | Exact archive/promotion-path success and tamper/missing-evidence proof | M6 |
| U47 Installed acceptance | Q, packaging/test infrastructure exists | Signed-installed two-platform/nine-host/current-resource qualification | M5 |
| U48 Final GO | Q, not earned | Restore final candidate, pass every required gate and obtain authorized READY | M6 |

The original per-unit tests and requirements remain normative. This table identifies the remaining completion path; it does not silently reaccept units merely because code or fixtures exist. [Full unit index and acceptance scenarios](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md:213)

## 10. Review evidence and limits

**Source snapshot:** `12ad16a0c7566f5ea2054fcd581c90f8dce02731`, branch `codex/production-readiness-completion`, verified equal to remote `origin/master`. The working tree was clean before report authoring. The historical memory was used only to locate the controlling context; current claims above were checked against the present checkout.

**Fresh execution:** `cmake --build build/release -j 2 && ctest --test-dir build/release --output-on-failure -j 2`. The configured local macOS Release suite ran on September 12, 2026, approximately 23:35–23:37 KST, with 121 passed targets and no failed targets. The old `LastTestsFailed.log` still contained an earlier run's entries; it was not treated as a current failure report. The complete current `LastTest.log` was parsed target-by-target and preserved separately.

- [Immutable captured CTest log](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/build/progress-review-evidence-2026-09-12/ctest-2026-09-12.txt)
- [Machine-readable scope, tests, units and inspected source digests](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/build/progress-review-evidence-2026-09-12/audit-snapshot.json)
- Captured log SHA-256: `7b172f3bfee81cf629d27b5ce82f35bf4a5c1149ae11d5cbbf621cf0179a59c5`.

**Inspection scope:** controlling plan and contract; compiler/render/resource entrypoints; producer/native/CLI review and recording flows; language/interchange implementations; neural request/deployment/dispatch boundaries; host offline behavior; character presentation; release validators; and supporting regression fixtures. This is a cross-subsystem code-level assessment, not an exhaustive line-by-line security audit of every file.

**Not newly verified:** a clean-toolchain rebuild, fresh ASan/UBSan/TSan run, Windows runtime, all nine installed DAW tuples, real microphone capture, actual neural inference, trained-model quality, complete SEAM visual/accessibility workflows, independent listening or final release signing/restoration. No numerical listening-quality or overall completion score is inferred from unit tests.

**Changes in this turn:** this new report and its local preview/evidence artifacts only. Production code, approved requirements, release state, existing source history and user branches were not changed. The implementation goal remains paused; this report does not resume development or authorize release.

## 11. Revised goal statement for the next implementation phase

> Complete SEAM as an original-voice creation and expressive virtual-singer product by delivering six integrated capability milestones. Reuse the existing score compiler, immutable rendering, Voice Designer and producer lifecycle. Prioritize a complete original voice-to-bank-to-unfamiliar-song pilot and early real neural model/data feasibility; then finish multilingual expression, native creator and character workflows, exact installed host reliability, and independent full-product qualification. Preserve all R1–R20 requirements, all U1–U48 acceptance obligations, manual-edit ownership, applicable rights/provenance, and the existing non-bypassable release gate. Use small verified commits within larger outcome windows. Do not equate fixtures, control declarations or passing regression counts with a completed singer, and do not stop independent engineering solely because a release-dependent resource or review remains unavailable.

**Bottom line:** the project should continue, but the next checkpoint should be a substantially more complete singer that a person can actually use and hear—not simply a longer list of individually verified internal boundaries.
