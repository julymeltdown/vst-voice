---
title: SEAM joint development plan R3 — an original singer that completes songs
date: 2026-09-15
status: jointly agreed execution plan; implementation pending
baseline_commit: adc6fc0ac50630c488d3e83e4b38ade9a0219032
supersedes_sequencing: SEAM_DEVELOPMENT_PLAN_R2_2026-09-15.md
scope_authority: VIRTUAL_SINGER_FEASIBILITY_AND_CODE_ROADMAP_2026-09-05.md
implementation_performed_by_this_document: false
language: English
---

# SEAM joint development plan R3

## 1. Product outcome and authority

Build an original virtual singer with which a creator can finish expressive songs: choose a usable voice, enter notes and lyrics, hear the result, repair pronunciation, tune pitch and expression, save/reopen, and export. The creator must also be able to sculpt an original female character voice without supplying a person's recording, save that design, and reuse the resulting singer. A successful preset player does not complete voice creation; an editable recipe does not establish a successful singer.

This plan replaces development plan revisions 1 and 2 for work selection and corrects the overbroad interpretation of the listening stop rule. It does not reduce the user-settled release scope. [R1–R20/V01–V18](VIRTUAL_SINGER_FEASIBILITY_AND_CODE_ROADMAP_2026-09-05.md), the [U1–U48 decomposition](docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md), [preserved U60 and authority amendment](docs/product/FULL_SCOPE_AUTHORITY_AMENDMENT.md), and the [machine-readable full-product contract](docs/product/full-product-beta-contract.json) remain authoritative.

**R9 remains mandatory:** a qualified deployed neural original singer is required even if procedural singing succeeds. Which architecture, dataset and vocoder satisfy R9 is an engineering choice subject to evidence. Whether neural processing is needed to improve the procedural voice is a separate research question.

Use this document for execution order and code guidance. Use the canonical contracts for acceptance. Old numerical status summaries remain historical; neither this plan nor its review awards a release PASS.

## 2. Verified starting point: reuse these components

The inspected checkout was clean at the baseline above. The preceding review reran five existing Release CTest targets successfully: Designer, expression lane, expression-on-song, procedural install journey and production neural rendering. Those results demonstrate their actual assertions, not an unaided creator session or musical quality. The earlier 911/911 core and 170/170 CTest results are retained checkpoint evidence, not a fresh full run for this document.

| Existing implementation | Concrete source to start with | What remains to demonstrate or extend |
|---|---|---|
| Score, pitch, phoneme edits and physical playback | `libs/seam-native-ui/include/seam/native_ui/editor_controller.hpp`; `libs/seam-standalone/src/native_editor_app.cpp`; `libs/seam-authoring-runtime/src/transport_controller.cpp` | Complete lyric-song tuning, responsive preview, and unaided use |
| Original voice design and deterministic procedural articulation | `libs/seam-voice-design/`; `libs/seam-native-ui/src/voice_designer_session.cpp`; `tests/test_voice_designer_workflow.cpp` | A desirable, intelligible voice and reproducible changes to its identity |
| Six timbral lanes and audio effects | `libs/seam-editor-ui/src/expression_lane.cpp`; `tests/test_expression_on_song.cpp` | Consistent route capability presentation; observed usefulness while tuning |
| Signed procedural package/install/select/copy-edit/review | `libs/seam-distribution/`; `tests/test_procedural_install_journey.cpp:234` | Extend the covered installed journey to an actual lyric song; do not rebuild D4 |
| Real/generated production and candidate workflows | `libs/seam-voicebank-production/`; `libs/seam-native-ui/src/voicebank_studio.cpp`; `tests/test_original_singer_workflow.cpp` | Qualified material and a producer-completed recording/regeneration/retake workflow |
| Neural preparation, training primitives, export, admission and worker | `tools/voice_model_training/`; `libs/seam-neural-synthesis/`; `libs/seam-authoring-runtime/src/neural_selection.cpp` | Admissible material, learned reconstruction, useful conditioning, qualified singer and both-platform evidence |
| Japanese/English/Korean pronunciation; automatic performance and harmonies | `libs/seam-phonemizer/`; `libs/seam-synthesis/src/automatic_performance.cpp`; `libs/seam-application/src/harmony_commands.cpp` | General lyric coverage, matching singer resources and successful creator use |
| SMF/USTX, character presentation, release validation | `libs/seam-interchange/`; `libs/seam-native-ui/src/character_performance_binding.cpp`; `tools/external_beta/` | Actual supported journeys and exact-candidate platform/host acceptance |

Three concrete limitations determine this plan:

1. `RendererCarrier` currently has only `SampleBank` and `SourceFilter`. The expression lane treats every non-procedural track as a sample carrier. `render_snapshot.cpp` separately rejects the six timbral curves on both sample and neural paths. Installed procedural voices retain those controls; installation is not the limitation.
2. `tests/test_neural_production_render.cpp` explicitly uses arithmetic graphs. The [neural input record](docs/implementation/NEURAL_FEASIBILITY_INPUTS_2026-09-15.json) records oscillator training diagnostics and absent admitted corpus, labels and vocoder. Existing machinery is substantial; remaining learned quality is unsized, not nearly finished by implication.
3. The [retained listening record](docs/implementation/listening/2026-09-15-d1-02/decision.md) remains `NOT_REVIEWED`. Its 16-second song is a useful diagnostic, not evidence that someone completed a song unaided.

## 3. Six substantial milestones and their dependencies

Each milestone delivers one integrated result. Its task IDs are implementation checkpoints, not a replacement percentage denominator or new product requirements.

| Milestone | Deliverable | Engineering can start | Acceptance dependency |
|---|---|---|---|
| M1 — usable original-singer session | Installed original preset, editable voice design, one 30–60-second lyric song, reliable tuning/save/reopen/export | Now | Human workflow observation is recorded separately |
| M2 — musically usable first voice | Reviewed short phrases and song excerpt; focused repairs; declared useful range | Diagnostic preparation now; perceptual repairs after an observation | Language/music listeners and usable M1 material |
| M3 — qualified neural original singer | Real acoustic model plus vocoder, admitted installation, ordinary UI-selected rendering and held-out song | Material planning and independent integration now | Each training stage needs its own valid inputs; final quality needs listeners |
| M4 — complete voice production and expressive languages | Real/generated source production, paired styles, JP/EN/KO resources, automatic performance and harmonies | Independent dictionary/producer/workflow work can proceed | Resource expansion follows a usable relevant voice and reviewed material |
| M5 — complete supported product surfaces | Standalone/DAW song work, interchange, live expression, character and recovery | Deterministic integration and defect repair can proceed | Final results require supported target machines/resources |
| M6 — Full-Scope Beta GO | Same-candidate accepted evidence across every required outcome | Maintain traceability during M1–M5 | All mandatory outcomes and empirical cells accepted |

Start M1 and the bounded M3 input/compatibility work together. Do not wait for M2 to discover whether M3 has usable material. Do not make M3 optional after M2. Start independent M4/M5 tasks when M1 work is waiting on a specific external input; retain M1 as the first integrated delivery priority.

### Three distinct evidence states

- **Engineering:** an operation is implemented and its observable contract passes. Automated application-command replay belongs here.
- **Workflow (Gate A):** a person completes the session without developer intervention. They need not certify Japanese pronunciation. Help, failures and workarounds are recorded; automation cannot impersonate this person.
- **Musical (Gate B):** listeners judge intelligibility, identity, phrasing, artifacts and expression. Native-language judgments require appropriately capable listeners.

Gate B blocks choosing a repair whose justification is perceptual. It does not block a demonstrated wrong note frequency, incorrect time mapping, missing UI action, crash, stale publication, or lost edit. Fix independently proved correctness defects without waiting for ears, then retain new audio for the affected listening comparison.

If a required observation is absent, mark that dependent task `WAITING_FOR_OBSERVATION`, name the missing observation, and take the next ready task. If every remaining executable task depends on material or people, report that exact boundary. Do not manufacture new infrastructure to remain busy, repeatedly replan, or fabricate acceptance.

## 4. M1 implementation guide — deliver the first complete session

### M1.1 Resolve and show the actual singer route

**Outcome:** before choosing a singer, the user can see its renderer, declared language/range/style, usable controls, and qualification status. The selected route gives the same capability answer in the picker, lane, preview and export.

**Existing files to change:**

- `libs/seam-synthesis/include/seam/synthesis/renderer_capabilities.hpp`
- `libs/seam-synthesis/src/renderer_capabilities.cpp`
- `libs/seam-rendering/include/seam/rendering/project_renderer.hpp`
- `libs/seam-rendering/src/render_snapshot.cpp`
- `libs/seam-editor-ui/include/seam/ui/expression_lane.hpp`
- `libs/seam-editor-ui/src/expression_lane.cpp`
- `libs/seam-standalone/include/seam/standalone/application_controller.hpp`
- `libs/seam-standalone/src/application_controller.cpp`
- `libs/seam-native-ui/include/seam/native_ui/editor_controller.hpp`
- `libs/seam-platform/include/seam/platform/application_menu.hpp`

**Proposed new files:** `libs/seam-rendering/include/seam/rendering/singer_route.hpp`, its `src/singer_route.cpp`, and `tests/test_singer_route.cpp`. Names are proposed interfaces, not existing APIs.

Implement in this order:

1. Extend the synthesis-level carrier enum to represent neural explicitly. Replace sample-versus-else-source logic with exhaustive cases: simply adding an enum value would currently risk granting procedural controls to neural. Unknown and unresolved routes do not acquire capabilities.
2. Keep lightweight capability values and validation in `seam-synthesis`. Add a validator over the resolved capability view so callers do not reconstruct a carrier from a label. Populate neural capabilities from fields the admitted execution contract actually consumes, not from metadata claiming support.
3. Derive `ResolvedSingerRoute` in `seam-rendering` from the existing `TrackSingerSource` alternatives, resource identity, selected style, actual engine/worker provenance and admitted metadata. A saved reference alone is insufficient to assert that an installed resource is available. Resolve `TrackRecipeFileSource` before reporting a ready route.
4. Distinguish code-supported controls, resource-declared coverage, resource availability and reviewed quality. Unknown language/range is displayed as undeclared; a technically runnable resource may remain unreviewed. None of these flags grants musical approval.
5. Reuse the controller's installed singer offers, sample browser and `neuralResources()` menu path to present the view. Preserve visible stored curves when a route refuses them. Show all relevant conflicts before the user changes a singer; do not erase automation or silently substitute a voice.
6. Feed the same lightweight view to lane validation and render/export preflight. Keep final lower-level validation in the renderer, including all unsupported-curve checks. Capability support must not be inferred from a nonempty recipe reference after that reference fails resolution.

Illustrative shape, to implement using the existing identity/provenance types:

```cpp
// Proposed rendering-level DTO; not project JSON or a signed package schema.
struct ResolvedSingerRoute {
  domain::TrackId trackId;
  domain::SingerResourceIdentity resource;
  synthesis::RendererCarrier carrier;
  synthesis::RendererCapabilityView capabilities;
  // Add typed engine/worker identity, declared language/range/style and status.
};
```

**Dependency rule:** `seam_editor_ui` already depends on `seam_synthesis`, not rendering or authoring. Pass a synthesis-level value into it; do not create an upward dependency to the route resolver. Rendering may consume neural admission types; synthesis must not depend on the neural library that already depends on synthesis. Native/authoring own resource resolution and UI presentation.

Do not add a project schema version for this derived view. Existing persisted resource references and render identities remain authoritative. A later new model-conditioning contract may require a separately justified version change in M3.

**Tests:** an explicit per-carrier/per-control matrix; missing and incompatible installations; neural not aliased to sample; stored refused curves survive selection/undo/reload; picker/lane/preview/export agree; a claimed graph control absent from actual inputs is refused. Cover all 13 existing control names, not just the six timbral lanes. Current `StyleBlend` naming is not implementation. Sample unit renderer differences and Raw limitations remain visible when a requirement depends on them.

**Exit:** choosing any existing route states what works before editing; no new supported-control claim is made merely by changing a flag. Existing procedural package/install tests continue to pass.

### M1.2 Create a reproducible lyric-song fixture and editable original preset

**Reuse:** `apps/seam-singer-pilot/main.cpp`, `assets/pilots/seam-pilot-01/`, `tests/test_expression_on_song.cpp`, `tests/test_procedural_install_journey.cpp`, and the existing procedural package producer. Do not introduce another renderer or general-purpose listening framework.

**Proposed additions:** `assets/pilots/seam-song-01/` containing a source score/recipe definition and README; `tests/test_original_singer_song_journey.cpp`, registered as `seam_original_singer_song_journey_tests` using the existing install-journey target's dependencies.

Create an original 30–60-second score at a fixed tempo, with explicit lyrics, unequal durations, rests, sustained vowels, legato/melisma, short consonants, and low/mid/high passages inside the candidate's declared test range. Use the production phonemizer and performance compiler. Do not tune the source exclusively to this song; retain different phrases for evaluation. Language-review status starts unreviewed.

Offer one prebuilt original procedural voice through the installed-resource workflow. The creator can use it without opening Designer, then copy it to a draft and make a deliberate voice change. Test two recognizable design configurations technically; a claim that listeners perceive their intended identity remains Gate B.

The current pilot's `source-declaration.txt` only asserts internal source use/transformation and explicitly does not assert redistribution or commercial use. Preserve that boundary. The internal workflow candidate is not a public release package until the actual permission evidence covers the intended delivery. Never copy fixture approval identities into production material.

Retain source project, exact recipe/package/renderer identifiers, baseline export, tuned export and edit sequence. Generate large WAVs/builds outside Git in a new named directory under `/Users/lhs/Downloads/seam-listening-artifacts/`; commit reproducible definitions and compact manifests. Never overwrite the frozen `2026-09-15-d1-02` packet.

The existing sibling `2026-09-15-d1-02-audition/` contains lossy MP3 conversions for convenient playback. It is audition-only, not numerical or final qualification evidence. Use the original bound WAVs for spectral, level, pitch and qualification comparisons; preserve both directories without regenerating either in place.

**Exit:** another developer can reproduce the internal song candidate from its checked-in definition and installed singer; every artifact names the route. No public distribution or musical qualification is implied.

### M1.3 Extend the installed journey through real tuning and recovery

Start from the existing test at `tests/test_procedural_install_journey.cpp:234`; it already removes producer sources and reopens/exports an installed voice. Extend coverage rather than rebuilding installation.

Use `StandaloneApplicationController::dispatch`, application edit commands, `ExpressionLaneModel`, `VoiceDesignerSession`, `TransportController` and `ExportService`. Reach edits through the application boundary; directly assigning automation fields is useful for a DSP test but does not prove the editor action is reachable.

Required operations in the integrated test:

1. Install/select the original preset, open or enter the lyric song, and render a neutral baseline.
2. Edit a lyric or explicit pronunciation; move a phoneme boundary; change note pitch and vibrato; draw at least one bipolar and one unipolar timbral curve using their real commands.
3. Loop a phrase, edit during pending rendering, cancel once and retry. Assert that the published revision/route matches the requested state and old results do not replace the new sound.
4. Undo the musical edits to the baseline; redo them. Compare control data, duration, route identity and PCM under the pinned deterministic renderer. Do not settle for a nonempty export or a nonzero energy assertion.
5. Save, close the session, reopen in a fresh session and export through the normal path. Compare with the pre-close edited export under identical settings. Different quality modes are compared against their declared contract, not assumed bit-identical.
6. Delete only the test-owned authoring/package source fixture and repeat reopen/export. Missing installed resources are separately tested as a visible failure.
7. Copy the installed voice to a draft, change a voice parameter, render the same song, save/reopen the draft, and verify the original installation remains unchanged. Undo restores the prior design and its deterministic sound.

Use central-half steady-note pitch measurements with voiced coverage and octave errors retained. Check requested timing edits against compiled sample boundaries. A waveform difference alone does not establish an intended formant/breathiness property; reuse the channel-specific acoustic tests for that assertion.

Add a stricter procedural regression check alongside the global release floors: before changing DSP, freeze per-note baseline errors, analysis settings and an estimator-repeatability margin on the retained diagnostic set. Record the margin before evaluating the new result; flag any newly lost voiced coverage, octave error or note exceeding that paired bound. Also compare the compiler's intended F0/timing directly against an independent score/tempo calculation. Do not rely on the broad 30-cent acceptance ceiling to detect a regression in a precisely generated oscillator. Timbral changes can affect a pitch estimator, so investigate discrepancies rather than automatically calling them audible pitch defects.

The one-rounding-sample timing requirement applies to the canonical timing-edit operation; separately measure audible phone-boundary behavior for each applicable route. Neural hop quantization must be exposed and tested. It does not authorize weakening the contract or treating an exact compiler timestamp as proof of an exact acoustic onset.

**Receipt correctness checkpoint:** `export_service.cpp:646` hashes encoded project content, whereas render identities exclude renderer provenance. Recording provenance after export can therefore change later project bytes without changing its sound. Add a regression defining what the receipt describes: preserve the captured export-input identity, explicitly distinguish later saved bytes, and compare audio by its existing hash. Do not silently redefine a signed/consumed receipt field or use a changed project-file hash as evidence that the voice changed. A receipt schema change requires a producer/consumer review first.

**Exit:** one lyric-song test connects installation, application edits, undo/redo, render publication, fresh-session reopen and export. Failures found by this journey are repaired in their owning layer before closing M1 engineering.

### M1.4 Observe and repair the actual creator session

Run the supported native app at the minimum supported window and a normal desktop size. Drive actual keyboard/mouse/IME controls, overlapping-note selection, long lyrics, pitch/phoneme lanes, loop playback, device output and export dialogs. A command-level fake dialog test is not native interaction evidence.

Record task completion, blocking interactions, interventions, first-sound time and edit-to-audible latency. Prefer existing render state/revision/transport statistics and a compact observation record. Add bounded event timestamps only where a required latency cannot already be measured; do not introduce a telemetry subsystem. Audio callbacks must not allocate, perform I/O or write logs.

A creator runs the same task without developer assistance. Save their observed outcome separately from automated results. Proposed record: `docs/implementation/listening/seam-song-01/workflow.md`, with an artifact manifest beside it. Native interaction defects are fixed in the corresponding controller/layout/transport files; they do not wait for Japanese pronunciation review.

The planned first Gate A observer is the project owner, unless another real participant is named in the record. Record the actual identity and product/development involvement. This initial workflow observation does not count toward M6's five independent pre-GO creators; M6 requires separate participation satisfying the canonical independence and task protocol. No observation is assumed to have happened.

**Exit:** M1 engineering is demonstrated and Gate A has an honest pass or actionable failure. M1 can be reported engineering-complete with Gate A pending, but not as an accepted usable singer.

## 5. M2 implementation guide — establish and improve musical usefulness

### M2.1 Obtain a short, diagnostic listening observation

Use the retained 16-second baseline and diagnostic phrases, plus an approximately eight-bar excerpt from the M1 song. Keep the full 30–60-second workflow task out of every repeated diagnostic comparison. Present dry and level-matched comparisons; retain untouched originals. Record listener language competence, resource/build identities, transcription or unclear syllables, artifact locations and expressive preference.

Evaluate intelligibility, connected articulation, useful range, character identity and whether an edit improves the intended result. Compare neutral and edited versions without equating greater loudness with improvement. Keep recipe identity judgments separate from basic usability and language judgments.

No transcript, musician vote or independent reviewer signature may be generated on behalf of a person. Existing comparison tools report differences, not quality rankings.

### M2.2 Repair the identified owner, under the existing cap

For perceptual repairs: at most two focused cycles, each capped at eight active engineering hours including targeted verification, before an explicit route decision. Each cycle records observation, exact reproducer, one main hypothesis, change, counterexample, held-out comparison and listener result.

| Observed failure | Inspect/change first | Required countercheck |
|---|---|---|
| Wrong phoneme or syllable ownership | `libs/seam-phonemizer/src/pronunciation_resolver.cpp`; language phonemizer | Original and held-out lyrics, explicit overrides, stale-context rejection |
| Correct isolated sound but bad short/connected consonant | `libs/seam-voice-design/src/articulation_plan.cpp`; `articulated_stream.cpp`; `frication_gesture_stream.cpp` | Isolated/connected, slow/short and at least low/mid/high cases |
| Wrong F0, timing, melisma or boundaries | `libs/seam-synthesis/src/performance_compiler.cpp`; `phoneme_timing_plan.cpp`; rendering segmentation | Actual unequal-duration windows; whole versus forced split; adjacent-context invalidation |
| Voice character or expression is ineffective | `libs/seam-voice-design/src/phonation_source.cpp`; `vocal_tract.cpp`; channel-specific tests | Intended property, neutral identity, pitch/timing preservation and held-out listening |
| Direct recipe works but baked bank degrades | `libs/seam-synthesis/src/unit_selection.cpp`; `seam_composer.cpp`; `phrase_renderer.cpp` | Same material direct/baked; transitions and range; do not retune the teacher to conceal a baking defect |

Bump the applicable engine/compiler/render revision when the same declared input changes output. Retain the previous build and audio where available; a hash or change notice does not preserve an old engine's behavior. Revalidate dependent reviews rather than transferring old approval.

**Exit:** an original voice has a reviewed useful range and the selected short-song/expression task succeeds. If gains remain confined to the probe, damage other phonetic classes or fail the main issue twice, stop the current perceptual repair strategy and compare a new synthesis/hybrid candidate. M3 remains mandatory either way.

## 6. M3 implementation guide — a real neural singer, using the existing toolchain

### M3.1 Select the bounded signal path and prepare admissible inputs

Default experiment: keep the current **48 kHz / 80 mel / hop 256** profile and evaluate the existing own-vocoder training path. This is a bounded engineering default, not a claim that it will reach acceptable quality. Compare an alternative admitted model/profile only when evidence justifies it; a profile change must update extraction, model, vocoder, bundle, runtime and tests together. Renaming metadata or reshaping tensors does not make incompatible signal definitions compatible.

Extend the existing neural feasibility record with candidate source, actual permission evidence, label readiness, profile, executable revision, compute bounds and a missing-input owner. Routine architecture choices do not require another blanket owner approval. Source rights, real reviews, credentials and unavailable material cannot be invented from standing development permission.

Use `tools/voice_model_training/__main__.py` operations `prepare`, `permission-report`, `admit`, `label-report`, `correct-labels`, `admit-labels`, `assemble-dataset` and `acoustic-targets`; use `split.py` for source-group separation. Reuse the existing reviewed preparation path and schemas. Different clips of one source/performance group must not leak across train and held-out sets.

**Start now:** inventory available inputs, validate schemas/configurations, record the default profile and implement any missing data-export adapter. **Stops only here:** actual admission/training waits when its required material or genuine permission/review evidence is absent. Listening on the procedural Japanese packet is not an input to this admission step.

### M3.2 Build a small generated-teacher transfer experiment

Purpose: test whether the learned path can reconstruct an original procedural voice, distinguish declared design variants, respond to conditioning, and meet measured compute bounds. This is not a shortcut to an accepted singer.

Reuse `CompiledScorePerformance`, the procedural renderer and the M1 recipe/score definitions. Proposed adapter: `tools/voice_model_training/generated_teacher.py`, with `test_generated_teacher.py`. It should capture generated PCM, recipe/engine/score hashes, planned phone/note spans, F0/voicing and source-group identity into the existing preparation inputs. Do not bypass source/label admission or create fake human approvals.

Generated span labels describe the renderer's intent. They do not prove acoustically correct pronunciation or perfect acoustic boundaries. Record their origin explicitly and keep perceptual annotation status separate. The pilot's internal source declaration is not evidence of model redistribution rights or an automatic training-policy approval.

Begin with one voice and a small train/held-out phrase set, then add controlled recipe variants only if the model contract actually represents those variations. A neutral single-voice dataset cannot prove multi-voice identity or six independent timbral controls. Hold out complete lyric/melody combinations and source groups, not random frames from training phrases.

Reuse the retained baseline, breathier and higher-formant recipe definitions as a small controlled-variation experiment; generate new source-group-separated training material instead of moving the frozen listening cases into training. After M3.4 supplies an actual variant/control input, compare each conditioned output with its corresponding and noncorresponding teacher outputs using a predefined signal-distance/property measurement. Report reconstruction fidelity, variant distinguishability, conditioning sensitivity and throughput. Success is technical transfer evidence; failure triggers localization across data, training and conditioning before a route decision. Neither result proves or disproves perceptual identity or the feasibility of all neural architectures.

Set the run specification before executing: maximum 2,000 steps, four hours, 16 GiB peak memory and 8 GiB output from the existing feasibility ceiling; begin with a much smaller throughput/reconstruction smoke run. Retain checkpoints, seeds, model/profile/data hashes, throughput, learning curves and failure reasons. Honour cancellation between updates; never publish a partial epoch as complete.

**Exit:** either a measured teacher/student reconstruction and conditioning result exists, or a specific missing input/contract failure is recorded. Neither a falling training loss nor a successful export establishes identity, intelligibility or release quality. A failed transfer changes the model/conditioning strategy; it does not silently split the user's connected voice-design promise into unrelated products.

### M3.3 Prove vocoder reconstruction before sustained acoustic training

Reuse `vocoder_batches.py`, `vocoder_optimization.py`, `vocoder_training_run.py`, `vocoder_checkpoint.py`, `export_vocoder.py` and their tests. First prove reconstruction from reference mel/F0 under the exact acoustic profile; then measure full acoustic-model-to-vocoder output. This localizes whether a bad result comes from acoustic prediction or waveform reconstruction.

`train_reviewed_vocoder_epoch()` currently calls `assemble_dataset()` and requires both source and label admission. The implementation uses F0 and phrase segmentation; it is not an unconditional mel-only trainer. Preserve the current admission boundary for the initial experiment. If real evidence shows unnecessary phone-label review is the only obstacle to an otherwise valid vocoder dataset, design a separate typed vocoder input that still validates source rights, source splits, PCM/mel/F0, segment bounds and checkpoint identity. Do not remove `labelsAdmitted` checks as a convenience patch.

**Tests:** exact hop/padding/trim lengths; sample rate/mel/profile mismatch rejection; train-only partition consumption; held-out reconstruction; invalid source/F0 handling; resume after completed checkpoint; cancellation and expired/stale data rejection. Existing random-initialized bridge evidence proves geometry only.

**Exit:** a reconstruction baseline, quality observation and compatible exported vocoder are retained before treating acoustic training as a path to useful audio.

### M3.4 Connect real conditioning through every producer and consumer

Existing `DiffSingerAcousticInputs` exposes tokens, durations, F0 and steps; dynamics are explicitly applied as output gain. It contains no automatic formant/breathiness/tension/gender/growl conditioning. Do not relabel output gain as those controls or assume a recipe is an accepted neural resource.

Choose one control the selected real acoustic model can actually learn and validate before expanding. Preserve unsupported controls visibly until supported. Reuse existing capability names; the full product must have a supported tested combination for each mandatory feature, not necessarily identical control mechanisms on every backend.

Changes for any new conditioning contract must land together:

| Producer/consumer | Existing owning files | Required change |
|---|---|---|
| Training and label conditioning | `tools/voice_model_training/conditioning.py`; `training_run.py`; `diffusion_export_wrapper.py` | Versioned values, normalization and training targets with actual variation |
| Exported graph and bundle | `tools/voice_model_training/export.py`; `export_vocoder.py`; `prepare_bundle.py`; `libs/seam-neural-synthesis/src/graph_contract.cpp` | Explicit names, types, shapes, units, defaults and supported conditioning |
| Request preparation and worker | `libs/seam-neural-synthesis/include/seam/neural_synthesis/worker_protocol.hpp`; `src/diffsinger_inputs.cpp`; `apps/seam-neural-worker/main.cpp` | Bounded frame arrays, correct sample/hop conversion, graph validation and actual consumption |
| Render/cache identity and selection | `libs/seam-rendering/src/render_snapshot.cpp`; `libs/seam-authoring-runtime/src/neural_selection.cpp`; M1 route view | Conditioning revision/data included where audio depends on them; capability claims follow admitted execution |

Use a new version only where the wire/graph/bundle contract changes. Old bundles remain supported under their original contract or produce an explicit incompatibility; never reinterpret their tensors.

**Tests:** varied input changes intended output property; zero/neutral reproduces baseline; unsupported input refuses; wrong normalization/rank/unit/length rejects; change invalidates cache; save/reopen/undo preserve intent; Python/native preparation agrees; graph accepting a name but ignoring the tensor cannot pass the acoustic-effect test. Distinguish technical timbral similarity from a listener's identity judgment.

### M3.5 Qualify the actual installed neural singer

Reuse `NeuralSelectionService::select`, `NeuralResourceRegistry`, `TrackNeuralSource`, bundle/deployment admission, the shipped worker and the ordinary export path. Do not invent a separate demo-only inference command as the product endpoint.

Replace arithmetic fixtures with an actual admitted learned candidate for the retained acceptance run, while retaining the fast fixture tests as routing regressions. Exercise UI selection, edit, preview, cancellation, worker failure, resource removal, fresh-session reopening and export. Inspect and close platform-specific process execution/handle cleanup gaps in `libs/seam-platform/src/helper_process.cpp`; POSIX results do not qualify Windows.

Use existing `qualify-candidate`/`qualification.py` for measured held-out dossier output; it intentionally leaves human judgments unresolved. Capture those real reviews through the existing review/evidence process, not by changing the command to always print QUALIFIED.

**Exit:** a learned original singer, applicable permissions, compatible vocoder, installed native inference and a reviewed held-out song are demonstrated on macOS arm64 and Windows x64. If the teacher/student route is insufficient, another lawful real/generated corpus and model strategy remains work under R9.

## 7. M4 implementation guide — production, languages and expression coverage

### M4.1 Complete real-input and generated-input producer sessions

Reuse `libs/seam-platform/src/recording_session.cpp`, `libs/seam-native-ui/src/voicebank_studio_production_edits.cpp`, `voicebank_studio_sample_review.cpp`, `voicebank_studio_campaign.cpp`, `libs/seam-voicebank-production/src/repository_operations.cpp`, `candidate_markers.cpp`, and `repository_export.cpp`.

Drive two concrete sources through the same validated production actions: one authorized recording/import, one generated source. For each: capture/import, inspect markers, edit, reject a take, record/regenerate a replacement, invalidate old review, obtain fresh review, publish, install and use in a held-out song. Preserve per-source permissions and lineage. Existing synthetic approvals in workflow tests remain fixtures.

Fix discovered action reachability, stale revision and recovery defects at the shared domain/application operation. Extend `tests/test_original_singer_workflow.cpp` and `tests/test_original_singer_campaign_workflow.cpp`; do not create a parallel producer repository. Expand large inventory only after the relevant voice and direct/baked comparison justify it.

### M4.2 Deliver actual JP/EN/KO lyric workflows

Reuse the existing language phonemizers and pronunciation resolver. English needs dependable dictionary coverage beyond its bootstrap lexicon; Korean needs reviewed lexical/context exceptions and matching phones; Japanese needs reviewed reading/context coverage. Existing code presence is retained, not counted as successful songs.

Use a versioned dictionary resource with actual permissions and a content identity that enters pronunciation/cache identity. Preserve explicit user phone hints and warnings for estimates. A singer's declared phone inventory must match what the language front end emits; a phonemizer with no corresponding sung resource is incomplete.

Expand `tests/test_english_phonemizer.cpp`, `test_korean_phonemizer.cpp`, `test_phonemizer.cpp` and existing language tests using reviewed examples. Include stress/syllables, liaison/codas, rests, continuations, mixed/unsupported input, context changes and manual override preservation. Complete a native-speaker-reviewed song for each required language using the corresponding declared route.

### M4.3 Finish paired styles and musical performance controls

Reuse `libs/seam-voicebank/src/style_resolution.cpp`, `libs/seam-synthesis/src/unit_selection.cpp`, `renderer_capabilities.cpp`, `automatic_performance.cpp`, `libs/seam-domain/src/region_performance_state.cpp`, `libs/seam-application/src/harmony_commands.cpp` and existing performance commands.

`StyleBlend` enum/vocabulary presence is not interpolation. First select two reviewed aligned styles of one singer with a common usable range and matching phone coverage. Implement a renderer-specific blend against that material; test endpoints, a midpoint, pitch/timing continuity, missing-pair refusal, undo/reload/export and cache identity. Define the sound of the blend through listening, not merely the ability to interpolate a scalar.

Complete automatic pitch/timing/energy proposals, alternate takes, partial regeneration, manual locks and editable harmony using the existing commands. Demonstrate that generated changes reduce manual work and do not overwrite locked intent. Extend `tests/test_automatic_performance.cpp` and `test_harmony_workflow.cpp`; repair actual gaps rather than replacing the existing rule-based generator just because it is not neural.

**M4 exit:** two reviewed usable styles, supported paired blend, both source-production routes and all three language workflows have accepted evidence for their advertised combinations.

## 8. M5 implementation guide — supported standalone and DAW product

### M5.1 Interchange and live/host authoring

Use existing `libs/seam-interchange/src/smf_codec.cpp`, `smf_project_conversion.cpp`, `ustx_codec.cpp`, `ustx_project_conversion.cpp`, and `libs/seam-authoring-runtime/src/interchange_service.cpp`. Verify import/export through native dialogs, PPQ/tempo/meter, pronunciation/expression conversion losses, malformed-input bounds and round trips. SMF is required; the historical deferral is superseded.

Follow the existing host adapters into `libs/seam-live-voice/` and rendering/transport. Drive note-targeted pitch, pan, vibrato, timbre, pedal/panic, host-follow versus score timing, seek, loop, reload and offline bounce. Do not map different controls to the same gain scalar. A bounce with missing/stale/failed required vocals must fail visibly.

Use the nine tuples in `docs/product/external-beta-host-matrix.json` and the canonical full-product contract. Maintain the same sample/recipe/neural capability declarations across standalone and wrappers. Close Windows helper execution and packaging on an actual Windows target, rather than claiming portability from compile-time guards.

### M5.2 Character, layout and recovery

Reuse `libs/seam-native-ui/src/character_presentation.cpp`, `character_performance_binding.cpp`, `libs/seam-character/`, native layout/accessibility and the existing support/recovery code. Bind mouth/performance cues to the audio that was actually published; stop/failure/stale selection must not animate an unrelated singer. Keep long text and overlapping notes selectable, the minimum window usable, and reduced-motion/accessibility behavior intact.

Complete the preserved U60 obligations through the existing support/crash/recovery paths, including `libs/seam-authoring-runtime/src/support_bundle.cpp`. Run save/recovery, interrupted generation/install, unavailable resources, device change and long-session workflows on the actual application.

**M5 exit:** supported product surfaces have real target-machine results for the frozen singer/capability matrix. A Linux/macOS unit suite does not stand in for Windows/DAW operation or native visual observation.

## 9. M6 implementation guide — close the existing full-product gate

Do not build another gate. Use `docs/product/full-product-beta-contract.json`, its evidence schema, `tools/external_beta/full_product_report.py`, `scripts/verify_full_product_report.py`, and the existing candidate/archive/release scripts.

Freeze the actual resource/language/range/style/backend/platform/host matrix. Keep every mandatory feature represented by a supported tested combination; a refusal is truthful but cannot satisfy a mandatory feature everywhere. Bind real evidence and genuine independent review to the same candidate. Changed assets/models/contracts require new identities and affected evidence.

The canonical floors include **60 phrases per language, three complete songs across the languages, five independent pre-GO creators, median pitch error at most 30 cents, at least 90% of designated steady frames within 50 cents, and a 30 ms timing edit within one rounding sample**. Retain voiced coverage and octave errors. The **500 ms p95 classical small-edit** and **150 ms median / 400 ms p95 two-second preview** workloads are separate requirements. These are existing floors, not thresholds invented for M1.

Resolve all currently unresolved empirical criteria/result cells through their named owners and measured target profiles. Read the complete contract for additional requirements; this paragraph is not a replacement checklist. A short M1 excerpt and one friendly tester cannot satisfy the full gate.

Extend existing external-beta tests only for discovered validation defects. Validate the restored archive, signed installations and all promotion paths. Pre-GO creator acceptance, readiness to begin Beta and the subsequent external cohort are separate events; do not fabricate future cohort completion or make it circularly required to start the cohort.

**Exit:** the existing full-scope evaluator and exact-candidate release audit accept complete actual evidence. Otherwise Beta remains NO-GO with named missing outcomes.

## 10. Scope traceability: nothing disappears between milestones

Milestones own delivery integration; existing U/V/R IDs still own acceptance. Reuse already accepted implementation evidence only when its identity and applicability remain valid.

| Requirements | Delivery owners | Remaining emphasis |
|---|---|---|
| R1, R11 | M1, M2, M5 | Musical edits, complete timing/pitch, usable native interaction |
| R2, R6, R10 | M1, M3, M4 | Applicable expressive controls, reviewed styles/blend, proposals/takes/harmonies |
| R3 | M1, M2, M3 | Recording-free original design, useful identity, explicit transfer limits |
| R4, R5, R20 | M1, M4 | Connected real/generated production and installed-song workflow |
| R7 | M4 | JP/EN/KO code, resources and native-speaker-reviewed songs |
| R8 | M1, M2, M4 | Classical processing and honest route capabilities |
| R9 | M3 | Qualified deployed neural singer, mandatory |
| R12, R13, R14 | M5 | SMF/USTX, all supported hosts, character performance |
| R15 | M1, M3, M4, M5 | Bounds, cancellation, identity, recovery and realtime behavior |
| R16, R17, R18, R19 | M2–M6 | Musical, platform, gate and permission evidence for delivered assets |

| Existing U-unit group | Integration ownership |
|---|---|
| U1–U8 | M1/M2: preserve baseline/compiler/resource work, close actual route and timing defects |
| U9–U17 | M4: production, classical quality, contextual selection and styles |
| U18–U25 | M1/M2/M4: design, generation, producer and editor workflows |
| U26–U28 | M4: three language workflows |
| U29–U34 | M5: interchange, live expression and host timing |
| U35–U37 | M3: material, model and deployed inference |
| U38–U40 | M3/M4: automatic expression, takes and harmonies |
| U41 | M5: synchronized character |
| U42–U43 | M2/M4/M6: released resources and musical/creator qualification |
| U44 / preserved U60 | M5/M6: support, crash/recovery and production closure |
| U45–U48 | M6: semantic evidence, restored audit, installed targets and GO |

## 11. Concrete next implementation batch

The next implementation turn should execute **M1.1–M1.3 as one coherent delivery**, with M1.4 preparation and M3.1 input reconciliation alongside it. Do not start with another plan or inventory campaign.

1. Reconcile current Git state and read the current canonical contracts and relevant source. Keep pre-existing work intact.
2. Implement the minimal route capability refactor and tests; surface it in existing picker/lane paths. Build affected targets.
3. Add the lyric-song fixture and extend the installed/application journey, including fresh-session reopen, undo/redo, cached publication and export comparisons.
4. Fix concrete failures the journey exposes in the owning layer. Avoid wholesale editor rewrites or new distribution formats.
5. Run the native session, retain compact workflow evidence and audio, and prepare the short listening subset. Record any human-dependent result as pending when absent.
6. Publish one integrated result with engineering/workflow/musical status stated separately. Advance the neural input work without waiting for unrelated listening.

Suggested commit boundaries are route consistency, song/application integration, and evidence/documentation. They are review boundaries, not three separate product milestones. Update the execution ledger once per meaningful landed slice; do not count a documentation fix as a new capability.

## 12. Build, test and evidence runbook

Run commands from the repository root. If the desktop terminal hangs when its `workdir` is the repository, launch with `workdir: "/"` and explicitly `cd` here. Current builds use the configured `build/release`; do not regenerate them with a different CMake generator casually.

```sh
cd /Users/lhs/Downloads/project-seam-usable-alpha-u3-master
git status --short
git log -5 --oneline
cmake --build build/release --parallel 8

# During M1 development: existing focused targets. --no-tests=error is intentional.
ctest --test-dir build/release --output-on-failure --no-tests=error -j 8 \
  -R '^(seam_voice_designer_tests|seam_expression_lane_tests|seam_expression_on_song_tests|seam_procedural_install_journey_tests)$'

# After registering the proposed M1 targets, require each to exist and pass.
ctest --test-dir build/release --output-on-failure --no-tests=error \
  -R '^seam_singer_route_tests$'
ctest --test-dir build/release --output-on-failure --no-tests=error \
  -R '^seam_original_singer_song_journey_tests$'

# One full regression at the meaningful implementation checkpoint.
ctest --test-dir build/release --output-on-failure --no-tests=error -j 8
git diff --check
```

Use the existing `seam_language_phonemizer_tests`, `seam_automatic_performance_tests`, `seam_harmony_workflow_tests`, `seam_smf_interchange_tests`, `seam_ustx_interchange_tests`, `seam_interchange_service_tests`, `seam_voice_model_training_tests` and neural worker/selection/production targets when their owners change. Confirm exact registration with `ctest --test-dir build/release -N`; C++ files are not always separate CTest targets. If running `seam_tests` directly, run `./build/release/seam_tests` from the repository root because fixtures use asset-relative paths.

For protocol/cancellation/thread changes add the relevant sanitizer/process-lifecycle verification. For native layout changes add actual native observation. Once checks pass, rerun them only after new changes, failures or unresolved concerns. Do not run the full suite after every line edit.

Available preparation and evidence entrypoints, verified by their help output during planning:

```sh
python3 -m tools.voice_model_training --help
python3 -m tools.voice_model_training assemble-dataset --help
python3 -m tools.voice_model_training qualify-candidate --help
python3 scripts/compare_listening_packets.py --help
python3 scripts/verify_full_product_report.py --help
```

Training is a separate module, not a subcommand of the preparation CLI. The following is a **template**, not a ready-to-run command or training authorization. Replace each `REPLACE_...` value with a validated captured artifact/path/hash and invoke it with the configured environment that supplies the pinned ML dependencies:

```sh
python3 -m tools.voice_model_training.train \
  --training-config REPLACE_TRAINING_JSON --training-sha256 REPLACE_SHA256 \
  --dataset-config REPLACE_DATASET_JSON --dataset-sha256 REPLACE_SHA256 \
  --targets REPLACE_TARGETS_JSON --targets-sha256 REPLACE_SHA256 \
  --source-root REPLACE_SOURCE_ROOT --conditioning REPLACE_CONDITIONING_DIRECTORY \
  --trusted-checkout REPLACE_PINNED_UPSTREAM_CHECKOUT --output REPLACE_NEW_OUTPUT_DIRECTORY \
  --rights-policy-sha256 REPLACE_RIGHTS_POLICY_SHA256 \
  --label-policy-sha256 REPLACE_LABEL_POLICY_SHA256 \
  --epochs 1 --maximum-run-seconds 600 --maximum-total-checkpoint-bytes 2147483648
```

The run config must additionally enforce its step/model/memory bounds. A short command timeout is not a memory limiter. Use fresh output directories and recorded seeds; resume only an admitted completed checkpoint with its independent receipt hash. Do not launch this template as part of implementing the plan document.

At a commit/push checkpoint: stage **explicit intended paths**, run `python3 scripts/verify_tracked_source_closure.py` and require `SOURCE_CLOSURE=PASS`, then commit the verified slice. Under the existing publication workflow, push the branch and `HEAD:master` only after reconciling remote state and without force-pushing. Fetch/inspect a concurrent remote change rather than overwriting it. Verify resulting commit identities. Do not use blanket staging or rewrite user changes.

## 13. Completion records and working rules

For each delivered milestone, retain one compact record containing source/build identity, exact singer route, commands/interaction script, expected and observed results, failures, artifact hashes and these three lines:

```text
Engineering: DEMONSTRATED | PARTIAL | NOT_RUN
Creator workflow: OBSERVED_PASS | OBSERVED_FAIL | NOT_OBSERVED
Musical review: ACCEPTED | REJECTED | NOT_REVIEWED
```

These are reporting labels; do not add another release-evidence schema solely for them. For final qualification use the existing canonical typed records and independent reviews. New named M1/M3 artifact paths in this plan are proposed locations, not files or evidence already created.

Keep documentation and code claims separate. A finite PCM signal is not a song-quality verdict, an arithmetic model is not a learned singer, an installed fixture is not an approved release resource, and a declared compatibility flag must match actual rendering.

Use English for future development updates and documents. Keep the other developer on bounded reviews of concrete contracts/diffs; one implementer owns shared edits. Do not start more agents or additional tasks merely because multiple milestones are described here.

## 14. Joint decision record

Participants: the primary development task and the user-designated task `01a0a066-1eba-71f2-8c0d-e21f9419cbcc` (title at review: `대기`). The second developer reviews read-only; this document's author owns edits.

Agreements carried from the completed direction review: preserve the existing code foundations; deliver a whole original-singer session first; separate workflow and musical observations; scope listening-dependent stops to dependent work; make route capabilities explicit; keep R9 mandatory; do not promise recipe-to-neural identity/control preservation without evidence.

**Final review: agreed on September 15, 2026, with no blocking corrections.** The second developer read the complete draft and independently checked cited implementation paths and existing test registrations. The final nonblocking notes were incorporated: the initial owner workflow observation is separate from M6 independence, and MP3 audition conversions are excluded from measurements and final qualification evidence.

The exchange also corrected specific claims before agreement: the current lane treats neural as sample and refuses the six controls; adding a neural enum without exhaustive handling would introduce a false grant. Generated phone timelines are intent labels, not acoustic truth. Ordinary profile selection needs an engineering decision, not another blanket owner approval. Existing neural tooling does not establish trained quality or a completion estimate.

Both developers agreed that the next development turn should implement the M1 batch in section 11, with independent M3 input work alongside it. No implementation, training, musical approval, release acceptance or commit/push is performed merely by publishing this plan.
