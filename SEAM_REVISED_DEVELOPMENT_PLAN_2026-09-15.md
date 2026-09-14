---
title: SEAM revised development plan — usable original singer and evidence-led completion
date: 2026-09-15
status: active execution sequencing plan
baseline_commit: c9aca6fdabb6d51587ab1d1e571a737d0fbb8ad9
scope: R1–R20, Full-Scope U1–U48, and preserved U60 obligations
replaces: September 13 near-term execution order, not its acceptance obligations
implementation_performed_by_this_document: false
---

# SEAM revised development plan

## 1. Decision and intended result

The next substantial outcome is **a creator making an unfamiliar song with an editable original procedural voice**, with retained listening evidence, usable expression editing, persistence and final export. Baking that voice into a sample bank is a separate required production route; it is no longer a prerequisite for discovering whether the original procedural voice is worth developing.

This changes development order in response to the [second-developer review](SEAM_SECOND_DEVELOPER_REVIEW_2026-09-15.md). It does not reduce the owner's Full-Scope Beta GO definition. Recording-free original female voice creation, real/generated source production, qualified neural singing, Japanese/English/Korean, paired styles, automatic performance, complete native workflows, required host support, character performance, and release qualification all remain mandatory.

The governing change is operational: **a retained musical experiment must influence the next large investment**. A nonzero render, another passing DSP test, or complete preparable inventory is insufficient reason to expand production.

The immediate sequence is:

1. Finish the existing growl checkpoint without absorbing unrelated documents.
2. Produce a small direct-procedural listening packet using the existing render path, before adding product capabilities.
3. Extend existing curve-editing patterns into a shared surface for the new expression channels, using the same song.
4. Repair the dominant observed acoustic or workflow defect within a bounded investigation.
5. Complete procedural singer distribution from the existing resource and installation foundations.
6. Run a separately bounded neural feasibility track alongside the listening/editor work.
7. Expand sample-bank production and paired styles when their own small comparisons support that investment; then complete the preserved multilingual, host and qualification scope.

There is no requirement to wait for a new catalogue, signing workflow, ASR model, or a full voicebank campaign before hearing the first packet.

## 2. Authority and source baseline

Read the documents in this order when intent and older status descriptions appear to conflict:

1. Owner-settled requirements and [Virtual Singer Feasibility and Code Roadmap, revision 2](VIRTUAL_SINGER_FEASIBILITY_AND_CODE_ROADMAP_2026-09-05.md).
2. [Full-Scope Beta GO plan](docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md), including R1–R20, U1–U48, verification scenarios and Definition of Done.
3. This report for current sequencing, work selection and early decision gates.
4. [September 13 implementation specification](SEAM_IMPLEMENTATION_PLAN_2026-09-13.md) for detailed package obligations not amended here.
5. [Integrated Singer Execution](docs/implementation/INTEGRATED_SINGER_EXECUTION.md), current source and actual retained evidence for implementation status.

The September 12 assessment and earlier ledgers remain historical evidence. This plan does not mark their old pending items complete merely because newer code exists. Closure still requires the relevant original acceptance evidence.

### Verified while writing this report

| Item | Observation | Consequence |
|---|---|---|
| Git baseline | Branch `codex/production-readiness-completion`; HEAD and local `origin/master` both `c9aca6fdabb6d51587ab1d1e571a737d0fbb8ad9` | This is a local remote-tracking comparison, not a fresh remote-server query. |
| Growl work | 22 modified integration/build files and three untracked growl files | Preserve and review the whole slice. It is not committed. |
| Source closure | Fresh audit reports exactly the three growl files as not indexed | Resolve by intentional staging and rerun; do not treat it as an unexplained synthesis defect. |
| Existing regression evidence | Previously inspected retained log reports 163/164 targets passed, including growl; closure failed | No full regression was rerun for this planning document. |
| Direct procedural route | Frozen recipe resources, track references, Select/Relink actions, procedural snapshots and export/package support exist | Extend the existing route; do not build a second procedural renderer. |
| Distribution boundary | `SeambankPackageInfo` owns a sample `voicebank::Manifest`; procedural selection currently accepts explicit recipe JSON paths | Signed installed procedural selection needs typed distribution integration. |
| Expression UI foundation | Pitch editing callbacks and `DynamicsLaneModel`/native dynamics workflow tests exist | “No editing surface” applies to the newer timbral channels, not to every curve in SEAM. Reuse established interaction and ownership patterns. |
| Neural/vocoder foundation | Training/export code and vocoder training/checkpoint/diagnostic files exist | Missing qualification is not evidence that all training or vocoder code must be written again. |

The attached review matches the root review file at this snapshot. Its strongest conclusions are accepted, with these precision corrections:

- The six new timbral controls are source-filter capabilities; sample-bank rendering refuses those requests. This is not a universal claim that sample banks have exactly six capabilities or cannot support additional controls in the future.
- “No listening” means no identified, retained listening evidence establishes current product quality. It is not proof that nobody anywhere has ever played an audio file.
- 1026/1026 means preparable assignments. The retained campaign records 498 generated, unapproved takes. Neither figure is an intelligibility or product-completion score.
- Renderer/cache revision identity prevents stale reuse; it does not retain an older renderer implementation or guarantee the same sound after an upgrade.

## 3. Product paths and completion labels

| Path | Primary retained resource | What it proves | Current next priority |
|---|---|---|---|
| Recording-free Voice Designer → song | Editable draft recipe; immutable procedural singer when distributed | The creator can shape a voice and use the source-filter controls directly | Listen, expose editing, then finish distribution |
| Recorded/generated material → Studio → bank → song | Reviewed immutable sample bank and production lineage | Material can be edited, reviewed, installed and reused through supported sample renderers | Diagnose direct/baked differences; preserve the real-input journey |
| Corpus → trained acoustic model/vocoder → song | Admitted immutable neural bundle with provenance | A qualified original learned singer works through the shipped runtime | Small real feasibility experiment before scaling training |

All three paths remain required where the full product contract specifies them. A recipe draft is not a released singer, a bank is not a recipe, and an arithmetic neural fixture is not a trained voice. Resource selection must expose the capabilities of the actual combination of resource, renderer and requested control.

Use the following completion labels instead of an invented global percentage:

- **Listening packet reproducible:** inputs and outputs are retained and rerenderable; musical judgment may still be pending.
- **First usable procedural creator loop demonstrated:** a person edits the original voice and song, hears supported expression, saves/reopens and exports without developer intervention. This can initially use the existing explicit local-recipe route.
- **First usable original singer delivered:** the same useful workflow works after procedural installation and with the producer's source directory unavailable; exact identity and review scope are visible.
- **Full-Scope Beta GO accepted:** all original requirements and exact-candidate release obligations are satisfied through the existing authority. The earlier labels cannot substitute for this one.

Historical 5/48 unit acceptance remains a dated count. This plan does not convert it into a code-completion percentage or reset later implementation to zero.

## 4. Execution packages and dependencies

The identifiers below are scheduling packages, not new U-units. A package closes with a runnable outcome and retained evidence, not a list of modified files.

| Package | Outcome | Entry dependency | Original owners |
|---|---|---|---|
| D0 | Recoverable growl integration checkpoint | Existing local diff | M3.P2; U6/expression obligations |
| D1 | Reproducible listening packet and initial diagnosis | Buildable current source | M1.P2/P3, early M6.P1/P2 inputs |
| D2 | Usable common expression editing on the packet's song | D1 artifact; existing commands and curve models | M3.P2, M4.P1 |
| D3 | Bounded acoustic/workflow repair and explicit route decision | D1 observation; D2 as useful for the defect | M1.P2/P3, M3.P2, M4.P1 |
| D4 | Installed procedural singer lifecycle | Existing resource path; useful pilot result before large distribution work | M1.P3, M4.P3, M5/M6 resource evidence |
| N1 | Measured learned-singer feasibility | Small admitted input strategy and captured runtime/profile | M2.P1–P3; U35–U37 |
| D5 | Qualified expansion of bank/style/language scope | Pilot and route-specific comparisons | M1, M3, M4 |
| D6 | Complete product and exact-candidate qualification | All original prerequisites | M4–M6, U60 and full release contract |

N1 starts alongside D1/D2, subject to available people and admitted resources. D2 can continue while human listening feedback is pending. Small paired-style algorithm experiments can begin before a full second-style inventory. Final qualification depends on real evidence, not on this scheduling flexibility.

### D0 — Finish the current checkpoint

Review the schema-17 migration, compiler revision 14, growl phase/reset behavior, neutral equivalence, bounds, carrier refusal, command undo, menu dispatch and focused tests. Verify that temporary diagnostic probes are absent. Add the missing growl execution-ledger entry, including rejected approaches, actual measurements and remaining limitations.

Stage the reviewed slice by explicit paths. The three currently untracked implementation files are:

- `libs/seam-domain/include/seam/domain/growl_automation.hpp`
- `libs/seam-domain/src/growl_automation.cpp`
- `tests/test_growl_expression.cpp`

The complete checkpoint also includes the 22 modified integration files and its intended ledger change. Keep this report, the reading guide and the second-developer review out of the DSP commit. Review `git diff --cached --stat` and the staged diff before publication; a blanket `git add -A` is inappropriate in this shared dirty checkout.

Run source closure and the previously failed CTest target after staging. If no runtime source changed since the retained regression, that is sufficient to resolve the indexing defect. If review changes runtime behavior, rebuild and run the affected suites; broaden for shared DSP/schema concerns. Report the actual sequence, such as “163 passed in the retained full run; closure passed after staging,” only once that last check passes.

**Exit:** intended source is recoverable as a reviewed commit, publication status is explicit, source closure passes, and no unrelated file is silently included. Completing D0 does not accept M3.P2 or Beta GO.

### D1 — Produce the first useful listening packet

**Start with existing capability.** `apps/seam-singer-pilot/main.cpp` already supports named articulation probes and custom `phrase LYRIC:MIDI[:TICKS] ...` input. It renders through production export and retains recipes, projects and audio. Use it immediately for a first packet. Add harness support only when the existing entrypoints cannot retain a required comparison; do not build a new evaluator before generating audio.

Freeze a small development set before making acoustic changes. A practical initial scope is 8–12 short cases and one short unfamiliar melody. These are development defaults, not final corpus requirements. Include steady vowels, stop/voicing and frication contrasts, glide/nasal transitions, ordinary connected phrases, short versus long notes, melisma, and low/middle/high points of the proposed pilot range. Use MIDI 60–72 as the existing diagnostic range, with no implication that it is qualified or the final range.

Start with direct procedural audio. Attempt a matched bank version only where generated material and alignment support it. Record missing bank coverage as a route-specific limitation; do not generate the full inventory just to complete a comparison. Separate syllable/phone probes from lexical Japanese phrases: correct syllable labels alone are not a blind word-recognition task.

Retain one immutable packet directory, outside disposable build output for durable handoff, with a small manifest in source control and audio/build artifacts in a durable local artifact store. A planned layout is:

```text
docs/implementation/listening/<packet-id>/
  manifest.json             # paths, hashes, build and protocol identities
  observations.md           # absent/pending/supplied observations stated honestly
  decision.md               # failure class, next action, and investment decision
<durable-artifact-root>/<packet-id>/
  inputs/                   # exact scores, recipes, language/resource inputs
  renders/direct/           # original dry Float32 audio
  renders/bank/             # only comparisons actually produced
  measurements/             # raw measurements and settings
  audition/                 # optional identified level-matched copies
  build/                    # retained executable/dependencies or reproducible build record
```

The manifest binds source commit and any dirty-diff digest, executable digest, resource/recipe identity, score, language resource, renderer/compiler ABI, sample rate, channel layout, seed, controls, frame count and audio hashes. Record retained-build limitations rather than claiming a portable runnable environment from an executable hash alone. Never overwrite an older packet or automatically promote newly rendered audio into the reference.

Measurements include finite/clipped samples, peak/RMS, steady voiced pitch where applicable, phone/timing diagnostics, and render time divided by output duration. Record cold/warm runs and machine/build settings when measuring performance. Average faster-than-realtime rendering does not establish live-note latency, callback safety or multi-track performance.

Human observation is a separate record. Present unknown lyrics for first transcription, then reveal them for diagnosis. Collect musical usefulness/artifact comments and an observed creator correction. Batch the Japanese listener's work; the implementer can prepare packets and continue D2 while awaiting a supplied judgment. Do not invent a reviewer or claim that waveform analysis constitutes listening.

**Exit:** packet reopens and rerenders, source and audio identity are checked, and an explicit result exists: technical packet only / listening pending, or a supplied A/B/C/D result below. A missing human observation prevents a musical success claim, not preparation or independent implementation.

### D2 — Extend the expression editing surface

The initial UI design is one selectable expression lane with a channel picker, scale/unit labels, neutral line, visible point handles, playhead value and applicability explanation. Avoid six permanently stacked lanes that consume the piano roll. Reuse existing pitch and dynamics navigation, draft and semantic patterns; preserve their working behavior.

**Existing code to extend:**

- `libs/seam-editor-ui/include/seam/ui/dynamics_lane_model.hpp` and `src/dynamics_lane_model.cpp`: captured context, bounded draft, conflict checking, navigation and apply/cancel patterns.
- `libs/seam-native-ui/{include/seam/native_ui/editor_controller.hpp,src/editor_controller.cpp}`: input dispatch, existing pitch callbacks and channel nudges.
- `libs/seam-native-ui/src/editor_scene.cpp`, `editor_frame_layout.cpp`, `editor_semantics.cpp`: shared painted, interactive and accessible geometry.
- `libs/seam-application/include/seam/application/performance_commands.hpp` and its implementation: typed edits and ownership.
- `libs/seam-synthesis/src/renderer_capabilities.cpp`, `performance_compiler.cpp` and `libs/seam-rendering/src/render_snapshot.cpp`: actual applicability and render consumption.

**Planned small additions:** a shared expression-lane model and channel descriptor under `libs/seam-editor-ui/`, plus focused native adapter code. Proposed type names are `ExpressionLaneModel`, `ExpressionChannelDescriptor`, and `ExpressionApplicability`; they do not exist merely because this plan names them.

The descriptor supplies channel identity, persisted unit, bounds, neutral, display formatting and edit resolution. Keep serialization typed; do not replace all domain curves with unvalidated floats. Formant semitones, bipolar gender and normalized airiness must not acquire the same numeric semantics simply because they share a graph.

Implement in this order:

1. Read-only display of existing formant, airiness and gender curves with correct units and resource applicability.
2. Insert, select, move and delete points; numeric keyboard entry; reset; zoom/pan and full accessible value text. Enforce existing point/time bounds. Reject a move onto another point consistently with the dynamics precedent instead of silently deleting data.
3. Capture a draft at gesture start. Pointer movement updates the draft; pointer release commits one typed undoable change. Escape cancels without modifying the project. A stale region/resource/context rejects commit with an actionable message.
4. Publish bounded audition work through existing worker/coordinator owners. Do not compile or render inside paint, pointer-move handling or the audio callback. Coalesce requests, cancel stale work and distinguish pending audio from current audio.
5. Retain manual/generated ownership. Clearly distinguish stored points from effective compiled behavior when selected generated takes influence the result. Editing one channel must not erase another take or acquire unrelated ownership.
6. On an unsupported resource, preserve and display the stored curve with its refusal reason. Disable new unsupported edits while allowing an explicit reset/removal where the existing contract permits it. Never silently clamp the request into an unsupported renderer or erase intent on resource selection.
7. Connect breathiness, tension and growl to the same surface after the first three channels demonstrate the interaction. Retain existing pitch/dynamics workflows; unifying their implementation is optional and must not delay usable timbral editing.

Verification must include bipolar/unipolar/unit-bearing scales, gesture-level undo/redo, no-op and cancellation, duplicate-time handling, stale context, unsupported resources, save/reopen and changed/neutral export. Add a multi-note articulated phrase comparison: sustained-pose channel tests do not establish transition/reset behavior under automation. Cover adjacent chunks and note reattack for growl in particular.

Run focused semantic/layout tests and actual native visual/keyboard checks at the enforced minimum window, 1024×768 and 1280×800 where supported. Include overlapping/short notes, long Japanese/Korean/English labels, IME focus and long resource/error text. Character artwork must yield space before musical controls. Existing native screenshots or tests from another revision do not certify the new lane.

**Exit:** a creator changes audible expression on the D1 song, cancels and undoes edits, reopens and exports the result; the selected resource really supports the edited controls. All six currently implemented timbral channels are reachable through the common surface. This does not close all M4 or all expression obligations.

### D3 — Repair according to observed failure, with a stopping rule

Use multiple labels if necessary; one global good/bad score loses important context.

| Result | Next bounded action | Investment to pause |
|---|---|---|
| A: Intelligible but weak/unwanted identity | Compare a few recipe/timbre variants across held-out phrases; keep intelligibility intact | Full range/style production until an identity direction has useful feedback |
| B: Unclear consonants/transitions | Localize isolated/connected, slow/short, pitch and direct/baked differences; test one source/timing/transition hypothesis | Multiplying the affected defect across inventory |
| C: Useful audio, unusable workflow | Observe the exact creator failure and repair command/input/layout/feedback behavior | Unrelated DSP additions |
| D: Context-dependent quality or direct/baked regression | Produce a failure matrix across range, timing, mixed expression and resource route; repair the responsible owner | Broad qualification or claiming a friendly probe generalizes |
| Invalid: missing reproducibility or useful observation | Repair the packet or collect the missing observation | Musical conclusions and arbitrary threshold changes |

Allocate **at most two focused acoustic repair cycles before a route decision**. As an initial work-allocation default, cap each cycle at eight active engineering hours, including targeted verification; this is a budget, not a predicted completion time. Waiting for human feedback is tracked separately and does not justify unlimited implementation. A larger investigation requires a written revised hypothesis and budget, not a silent third cycle.

Each cycle records the failure class, reproduction case, single main hypothesis, intended code change, counterexample, held-out comparison and result. Once development uses a held-out phrase to tune the implementation, mark it development material and preserve a separate untouched set. Final M6 material remains independent.

If improvement generalizes without unacceptable regressions, choose the next bounded repair. If it only improves the tuned case, damages other classes or leaves the primary failure unchanged, stop expanding that route and compare alternatives: improved procedural sources, authorized hybrid excitation/transients, or an actually demonstrated learned acoustic/vocoder path. Preserve recording-free creation and the original scope when evaluating alternatives. Do not infer that neural synthesis is the only viable answer from two unsuccessful patches.

**Exit:** evidence-backed repair outcome and explicit continue/redirect decision. A poor result is a useful deliverable if it prevents an expensive unproductive production run.

### D4 — Deliver an installed procedural singer

Preserve `ProceduralSingerResource`, recipe decoding, track selection, frozen snapshots and export. This package adds the missing distribution connections rather than a new synthesis system.

| Substep | Concrete implementation | Primary existing owner | Exit evidence |
|---|---|---|---|
| D4.1 Manifest/admission | Typed procedural distribution identity, recipe digest, producer/release version, styles, declared coverage/range/language and engine compatibility; reviewed status separately bound | `seam-voice-design/recipe_resource`, synthesis/domain resource identity, `seam-distribution` | Valid candidate admitted; missing, oversized, inconsistent and incompatible input rejected |
| D4.2 Review candidate | Freeze recipe plus representative score/audio/build/settings; record supplied decisions and invalidation | Existing production review/provenance owners and candidate services | Changed recipe/render evidence cannot inherit a prior approval |
| D4.3 Pack/verify | Typed manifest dispatch over reusable bounded archive, signature, digest and path checks | `seam-distribution/seambank`, signing/trust primitives | Valid procedural package verifies; tampering and wrong family reject; existing bank packages remain valid |
| D4.4 Install/receipt | Immutable content installation, pre-publication verification, interrupted-install recovery, duplicate/conflict/side-by-side handling | `seam-distribution/installer`, authoring installer services | Transaction/recovery tests; no overwrite of installed content or editable drafts |
| D4.5 Catalogue/resolve | Discover installed procedural resources; exact identity resolution; separate missing/changed/untrusted/incompatible statuses | Catalogue pattern in `seam-voicebank/catalog`; typed procedural resolver | A saved song resolves only the intended resource; useful mismatch diagnostics |
| D4.6 Native selection/copy | Installed resource selection, trust/qualification/capability details, copy-to-edit and version replacement | Standalone Select/Relink actions, existing commands and Voice Designer session | Creator selects an installed singer and edits a copy without mutating signed content |
| D4.7 Compatibility | Define admissible engine revisions and mismatch behavior; retain qualified build/reference; version migration explicitly | Render ABI/resource manifest/packaging | No silent sound migration or automatic transfer of acoustic qualification |
| D4.8 Connected journey | Create/edit → candidate/review → package/install → select → tune → reopen/export with source directory unavailable | Native authoring, installer, render/export coordinator | Automated fixture journey plus separately identified real review and installed observations |

Before implementing D4.1/D4.3, write one short format decision with producer/consumer impact. Default direction: a procedural-specific typed manifest and service entrypoints sharing proven archive primitives, preserving `.seambank` v1 interpretation. Do not weaken sample-manifest validation or create fake sample units to fit a recipe. Exact envelope/version is an implementation decision to resolve once, with internal review, not a reason to reopen product scope.

Distribution release version, recipe schema version, recipe content identity and renderer compatibility are distinct fields. Inspect current identity semantics before migrating them. Continue to load existing user-selected and project-relative recipes with their existing meaning; add installed resolution without converting every legacy path into an invented trusted installation.

Signing establishes authenticity, not musical quality. Draft, installed/trusted and reviewed/qualified must remain distinguishable. Procedural resources carry data for the application's renderer, never arbitrary package-selected helper executables. Do not reimplement neural deployment under D4.

Deliver the eight substeps as three integrated checkpoints: admit/review/package; install/resolve; native creator journey and reproducibility. A checkpoint can demonstrate fixture behavior without asserting real qualification.

**Exit:** a useful procedural singer can be reviewed, packaged, installed and selected by exact identity, then used without the producer workspace. The real recorded/generated bank path remains separately open until its own obligations pass.

## 5. N1 — Independent, bounded neural feasibility

The question is whether an admitted original-voice data/model strategy can produce useful held-out singing through the shipped worker, and at what measured cost. It is not whether another fixture graph can produce samples.

Existing starting points include `tools/voice_model_training/train.py`, `epochs.py`, `checkpoint.py`, `export.py`, vocoder training/checkpoint/diagnostic files, `VOCODER_INTAKE.md`, `tools/neural_runtime/`, `apps/seam-neural-worker/main.cpp` and `libs/seam-neural-synthesis/`. Audit these before implementing missing orchestration. The existing vocoder intake also documents profile mismatch concerns; do not assume a downloaded vocoder is compatible or commercially admissible from its code license.

Proceed through four checkpoints:

1. **Feasibility input record:** choose one available source strategy; identify actual source/model rights, usable labels, source-group train/development/test splits, acoustic/vocoder feature profiles and hardware. Separate evidence of code, weight and source-material permissions. If material is unavailable, report exactly which input is missing and continue bounded integration diagnostics; no qualified learning claim follows.
2. **Small complete signal path:** prove reconstruction through a compatible vocoder before a costly acoustic training run, then train/adapt a small actual acoustic candidate. Preserve physical sample rate, mel definition, hop, F0, duration and padding semantics. Metadata reshaping does not fix incompatible features. Compare the vocoder's reconstruction of reference features with its output from predicted features to localize failures.
3. **Shipped-worker held-out song:** export the captured acoustic/vocoder pair, admit it, select it in ordinary rendering, render unfamiliar lyrics, reopen/export, and check teardown/cancellation as well as finite audio. A diagnostic subprocess alone is not this checkpoint.
4. **Measured route decision:** retain training/validation curves, held-out observations, throughput, peak memory, storage, render factor and failures. Report whether learning and output quality improve; estimate scale only after these observations.

Before any sustained run, write a run specification with exact inputs, maximum steps/epochs, wall-clock timeout, memory/storage ceilings, cancellation/checkpoint behavior and machine. Start with a short throughput smoke run and size the next run from it. The plan itself does not approve new expenditure, external uploads or unavailable corpus rights. Missing inputs may stop that experiment while D1/D2 continue.

If the experiment succeeds, N1 supplies the next M2 production estimate and resource plan. If it fails, distinguish unusable supervision, profile/export defects, vocoder reconstruction failure, learning failure and deployment failure. Do not solve all five by proposing a larger model or longer training.

## 6. Listening and ASR verification policy

ASR is optional diagnostic triage, never an intelligibility or identity acceptance authority. Use a pinned recognizer/configuration without lyric hints, preserve raw output and text normalization rules, and calibrate it with intelligible reference singing, negatives and known degraded comparisons. Human observers must initially sample both strong and weak ASR results. An empty transcript is ambiguous; a plausible transcript may reflect language priors rather than audible consonants.

Do not turn ASR setup into a dependency for the first packet. Run it at relevant audio checkpoints once it shows diagnostic value. Japanese lexical transcription, phonetic confusion and orthographic differences need distinct treatment; syllable probes are not assessed as ordinary prose. Final language, musical and intended-identity judgments require appropriate real evaluators.

Across versions, retain raw reference audio and render new output alongside it. Hash equality demonstrates repeatability for an unchanged deterministic configuration. Hash inequality identifies a change, not whether the change is good. Perceptual reference promotion records an explicit reason and actual observations. Unreviewed material can be a technical baseline but must be labeled unreviewed.

## 7. D5/D6 — Preserve the route to the complete product

After the first usable original-singer milestone, complete remaining scope in integrated outcomes rather than isolated flags:

| Remaining outcome | Development sequence | Evidence needed before closure |
|---|---|---|
| Recorded/generated bank production | Repair direct/baked defects; complete marker edit → rejection → retake → fresh review; observe an authorized real-input session; then expand approved inventory | Reusable installed bank, source-lineage continuity and unfamiliar-song quality; hardware capture observed where required |
| Paired styles and all expression | Small aligned pair first; implement pair identity/validation/interpolation/endpoints; extend coverage only after the blend works | Two reviewed compatible styles; meaningful intermediate values; all mandatory expression has a candidate-supported audible path |
| Japanese/English/Korean | Keep Japanese first for pilot continuity; complete real dictionary/rule inputs and matching singer coverage for English/Korean | Native-language songs and retained pronunciation/coverage evidence, not only parser tests |
| Neural production | Scale successful N1 strategy; finish deployed original acoustic/vocoder candidates and required runtime behavior | Actual learned singing and qualified source/model lineage on both target platforms |
| Native creator product | Finish takes, manual locks, partial regeneration, harmonies, USTX/SMF, dense-note selection/text/IME/accessibility and required character presentation | Complete observed creator journeys and fresh visual/semantic evidence |
| Installed host/recovery | Reuse existing Follow Host implementation; finish required live-expression semantics, wrappers, machines, nine-host matrix, recovery/support and soaks | Exact installed artifacts, actual host observations, preserved U60 obligations |
| Final quality/release | Freeze criteria from pilot evidence before final scoring; qualify full resource/corpus matrix; restore candidate independently; run canonical release audit | All U1–U48 obligations reconciled and authorized `EXTERNAL_BETA_READY` |

Retain the original final-corpus requirements, including at least 60 short phrases per required language, at least 180 total, and complete Japanese/English/Korean songs. Retain fixed pitch thresholds and all other normative criteria from the existing contract. Development packet size, two-cycle budget and pilot range do not amend those requirements. `EXTERNAL_BETA_CLOSED` and public activation remain later operations, not fabricated pre-GO evidence.

## 8. Requirement preservation map

This map changes execution grouping, not requirement content or acceptance ownership.

| Requirement | New execution emphasis | Existing specification retained |
|---|---|---|
| R1 musical timing/articulation | D1–D3 failure localization and held-out phrases | M1.P2/P3, M3.P1/P2, M4.P1, M6.P2 |
| R2 audible persisted expression | D0/D2, articulated multi-channel checks, D5 styles | M3.P2/P3, M4.P1/P2, M6.P2 |
| R3 recording-free original female voice | Direct procedural creator loop and D4, then actual identity judgment | M1, M4.P3, M6.P2 |
| R4 real and generated input | Distinct D5 production journeys; no removal of recording/import | M1.P3, M5.P2/P3, M6.P2 |
| R5 editable/reproducible bank lifecycle | D5 review/retake/install, with D4 as an additional procedural delivery path | M1, M5.P3 |
| R6 coverage/range/two styles/blend | Small paired experiment followed by qualified expansion | M1.P1/P2, M3.P1/P2, M4.P3, M6.P2 |
| R7 three languages | Japanese pilot first; all three remain in D5/D6 | M3.P1, M2.P3, M4.P1/P3, M6.P2 |
| R8 dependable classical rendering | Direct/baked comparisons and bank-specific repairs | M1.P2/P3, M3.P2, M6.P1/P2 |
| R9 qualified original neural singer | N1 proves feasibility; production qualification follows | M2.P1–P3, M5.P2, M6.P2 |
| R10 automatic performance/takes/harmony | Reuse implemented proposals; finish useful integrated editing | M3.P3, M4.P2, M6.P2 |
| R11 native usability/accessibility | D2 first; remaining dense-layout/creator workflow in D5/D6 | M1.P3, M4.P1–P3, M5.P2, M6.P2 |
| R12 USTX/SMF | Preserve implemented exchange and finish declared subset evidence | M4.P2, M5.P2 |
| R13 standalone/hosts/live timing | Early smoke where available; complete exact installed matrix | M5.P1/P2, M6.P2 |
| R14 identity/character performance | Preserve existing phrase presentation; finish final assets/usability | M4.P3, M5.P2, M6.P2 |
| R15 bounded/recoverable execution | All packages; preserve worker, repository and audio ownership | M5.P3 plus all implementation packages |
| R16 acoustic/listener/creator proof | Move development feedback forward; keep independent final qualification | M6.P1/P2 |
| R17 installed release/U60 | D6 retains all recovery/support/platform obligations | M5.P2/P3, M6.P3 |
| R18 full-product gate | No new release authority or reduced GO | M6.P1–P3 |
| R19 rights/provenance | Bound actual candidate, corpus and resource evidence | M1–M6 as originally assigned |
| R20 connected creation/production/song workflows | Direct loop early, bank loop separately, neural integrated after N1 | M1, M2.P2, M4.P1/P2, M5.P2, M6.P2 |

U1–U48 retain their original identifiers, requirements and acceptance tests. No package here creates additional unit credit or removes U60. Claiming D1 or D2 complete does not automatically close every associated original unit.

## 9. Ownership, verification cost and communication

The primary implementer owns integration, D0–D4 and the shared editor/resource contracts. The second developer is the prospective N1 owner; their completed report does not mean a training run is assigned, started or funded. Until implementation is actually assigned, their role remains report/read-only feasibility work.

Keep one integration owner for project schema, resource identity, capability resolution, render ABI, CMake and packaging contracts. A second implementation track uses an isolated checkout of an identified commit and exchanges explicit patches/contract proposals. Do not let two tasks commit unrelated changes from the same dirty working tree. Additional agents are unnecessary for this plan; no new task or agent is launched by writing it.

Verification cadence:

- During a focused change, run the relevant existing tests and the new behavior checks that can reveal a real regression.
- At a shared DSP/schema integration checkpoint, run the appropriate broader regression and retain the exact source/build/log identity.
- Do not rerun the entire suite for every ledger edit or solely because Git indexing changed. Conversely, do not reuse a pre-change result for a new executable.
- Reuse `test_dynamics_lane_workflow.cpp`, the expression suites, pilot CLI test and original-singer workflow tests as foundations. New tests should verify interactions, persistence, audio consequences and failure recovery, not mirror trivial implementation details.
- Actual native layout/keyboard/IME changes require native observation; human quality claims require real supplied observations. Keep those evidence types distinct.

A concise checkpoint record should state: package, source revision, implemented behavior, artifact location, fresh versus retained verification, observation status, dominant failure, next decision and original acceptance still open. Do not write another broad audit after every small patch. Use the existing execution ledger plus the packet decision record.

The immediate checkpoint checklist is:

- [ ] D0 reviewed and intentionally staged; closure resolved and reported truthfully.
- [ ] D1 original dry audio and exact inputs retained before new capability work.
- [ ] Listening status explicitly recorded; missing feedback does not become a success.
- [ ] D2 first three distinct unit shapes tested through one interaction, then all six timbral channels connected.
- [ ] D3 observation selects the repair; two-cycle/effort cap enforced.
- [ ] D4 development begins from existing owners and does not delay D1.
- [ ] N1 owner, input availability and run bounds recorded before sustained execution.
- [ ] D5 expansion decisions cite route-specific audio evidence.
- [ ] Full Beta obligations remain tracked through the original contract.

## 10. Initial execution commands and handoff

These are instructions for the next implementation turn, not commands executed by this document. Run from the repository root after checking status. Output directories must be new; keep failed or partial artifacts for diagnosis rather than overwriting them.

```sh
git status --short
git diff --stat
git diff --check
cmake --build build/release --target seam_growl_expression_tests seam_singer_pilot --parallel 8
ctest --test-dir build/release -R '^seam_growl_expression_tests$' --output-on-failure
```

After explicit checkpoint staging:

```sh
python3 scripts/verify_tracked_source_closure.py
ctest --test-dir build/release -R '^seam_tracked_source_closure$' --output-on-failure
git diff --cached --stat
```

An existing first-packet entrypoint is:

```sh
build/release/seam_singer_pilot NEW_OUTPUT_DIRECTORY articulation
```

Other existing modes include `boundaries`, `nasals`, `stops`, `affricates`, `glides`, `events`, and custom `phrase` input. The custom mode accepts 1–64 bounded note arguments. Use the created score and recipe files to construct the small unfamiliar melody; do not present the built-in ladder alone as a song-quality demonstration. The native direct-recipe journey and final export are part of the following creator checkpoint.

**Handoff instruction:** execute D0, then produce D1 with the existing tools. Advance D2 while collecting actual observations. Choose D3 from the result, not from the number of available backlog items. Keep N1 independent and bounded. This is the active development order until evidence justifies a recorded revision; it is not another request to redesign the project from scratch.
