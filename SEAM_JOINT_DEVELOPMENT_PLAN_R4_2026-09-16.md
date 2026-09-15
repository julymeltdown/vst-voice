# SEAM joint development plan R4 — the code-level plan to finish an original-singer product

---
title: SEAM joint development plan R4 — the code-level plan to finish an original-singer product
date: 2026-09-16
status: jointly agreed execution plan; implementation pending for everything after M1.3
baseline_commit: 3454ce9fce1a1601af5a3cf76b22994496e503c7
supersedes_sequencing: SEAM_JOINT_DEVELOPMENT_PLAN_R3_2026-09-15.md (R1, R2 remain historical)
scope_authority: VIRTUAL_SINGER_FEASIBILITY_AND_CODE_ROADMAP_2026-09-05.md (R1-R20)
contract_authority: docs/product/full-product-beta-contract.json (20 requirements, 83 cases, 18 work packages)
implementation_performed_by_this_document: false
language: English
---

# SEAM joint development plan R4

R3 decided the order of work. R4 keeps that order and turns it into something a developer can sit down
and execute: for each unit it names the file that owns the gap, the interface to add or change, the
observable that proves the unit is done, and the commands that produce that observable. It also
reconciles what actually changed in the source since R3 was written, because three of the units R3
called open are now partly or wholly done.

Nothing here reduces the user-settled full scope. R9 — a qualified deployed neural original singer —
remains mandatory. Reading this document performs no implementation, awards no acceptance and is not
evidence of anything.

## 1. Status reconciliation against R3

R3 was written against `adc6fc0a`. Seven commits landed after it. These are the only statements below
that changed, and each is a statement about source that exists now, not a plan.

| R3 said | Actual state at this baseline | Evidence |
|---|---|---|
| M1.1 route capability was a defect: neural resolved to the source-filter control set while the renderer refuses the six timbral channels | **Closed.** `RendererCarrier{SampleBank, SourceFilter, Neural}` is exhaustive, `rendererCarrierFor(track)` replaced eight sample-versus-else sites, and `resolveSingerRoute` is the one answer the picker, lane and export share | `libs/seam-synthesis/include/seam/synthesis/renderer_capabilities.hpp:57`; `libs/seam-rendering/src/singer_route.cpp`; `seam_singer_route_tests` 8/8 |
| M1.2 the song fixture did not exist | **Closed.** `assets/pilots/seam-song-01/recipe.json` is the encoder's own output, with a drift guard that fails the journey if the checked-in definition and the code recipe disagree | `tests/test_original_singer_song_journey.cpp:191` |
| M1.3 the journey covers installation but not real tuning | **Clause 2 closed at this baseline.** Lyric, pitch, vibrato, a compiled phoneme boundary, and a bipolar formant plus a unipolar breathiness curve now run through application commands | commit `3454ce9f` |
| M3 neural was blocked on an authorized corpus | **Materially changed, not unblocked.** A generated-teacher adapter now produces admissible label and score documents from the project's own renders, so a bounded teacher/student experiment is possible without an external corpus | `tools/voice_model_training/generated_teacher.py`; `seam_voice_model_training_tests` |
| M2 the retained packet is `NOT_REVIEWED` | **Unchanged.** No listener has judged anything, and this document does not change that | `docs/implementation/listening/2026-09-15-d1-02/decision.md` |

Two corrections to R3's own wording, because a reader of R3 alone would expect the wrong work:

1. R3 M1.3 clause 3 asks for cancel-and-retry while a render is pending **inside the song journey**.
   The coordinator already proves cancellation, staleness and revision ordering exhaustively
   (`tests/test_authoring_render_coordinator.cpp`, 19 cases including
   `authoring_render_coordinator_cancel_invalidates_unpublished_audio` and
   `authoring_render_coordinator_newer_revision_prevents_old_publication`). What is missing is the
   *connected* step in the song journey, and that is smaller than R3 implies.
2. R3 M1.1 asked for an explicit per-control matrix over all 13 controls. That shipped
   (`seam_renderer_capability_tests`), so the remaining M1.1 work is presentation, not capability:
   the inspector still has no applicability row.

### 1.1 Defects found by reading source during this reconciliation

These three are **not** reproduced failures. They are places where the code and its own stated intent
disagree, found by reading, and each is listed with the test that would prove or disprove it. They are
scheduled below rather than fixed silently, so each fix is reviewable on its own commit.

**D1 — the chosen character state decides whether the character appears at all.**
`libs/seam-standalone/src/native_editor_app.cpp:1420` publishes `character_.portrait()`, which is
`portrait(state)`, and `native_editor_app.cpp:1397` sets that state from the render status: Warning,
Rendering, Complete, Error. `assets/character-01/manifest.json` declares all six states, so the
character's *face* changes with render status, which is intended. The disagreement is that
`libs/seam-clap-editor/src/editor_runtime_paint.cpp:29` gates visibility on
`portrait(Neutral) != nullptr`, so a package declaring only some states would be invisible in one
surface and visible in another. The declared contract already says a package either declares the full
turnaround or is not one (`libs/seam-character/include/seam/character/character.hpp:49`), so both
surfaces must answer this question from the package rather than from whichever surface was written
last. *Proving test:* a package with one state missing is refused at load, and visibility for one
package is identical in standalone and CLAP.

**D2 — the character cannot open its mouth, because no shipped package declares mouth artwork.**
`libs/seam-standalone/src/native_editor_app.cpp:1418` asks for `character_.mouth(frame->mouth)`, and
for a status-only package that answer is empty: `assets/character-01/manifest.json` has no
`mouthAssets` block, `declaresPerformance()` is false, and `hasPerformanceAssets()` has zero call
sites anywhere in `libs/` or `apps/`. The dock therefore falls back to drawing a mouth glyph
(`libs/seam-native-ui/src/editor_scene.cpp:1144`). The engine, the six mouth shapes, the cue mapping
and the performance snapshot all exist; the artwork does not. This is the asset-utilization gap the
owner raised in design review, and it is the one item in that critique that is a *content* problem
rather than a layout problem.

**D3 — overlapping notes are folded into a stacked band instead of being shown as overlap.**
`libs/seam-editor-ui/src/note_visual_layout.cpp:78` computes up to three visible bands and folds the
rest into `hiddenByDensity`, and `libs/seam-editor-ui/src/piano_roll_model.cpp:106` publishes that as
`drawsOverlapIndicator`. The result is legible but ambiguous: a creator cannot tell *these notes
overlap in time* from *these notes are dense*, and nothing in the layout separates the two cases.
*Proving test:* two notes that overlap in time and two notes that are merely close report which case
they are.

## 2. Progress, stated on two scopes

A single percentage is misleading here because most of the remaining distance is gated on people and
material rather than on code. Both numbers below are given with the rule that produced them, so they
can be checked rather than believed.

**Scope A — the first usable original-singer milestone** (the smallest outcome worth calling a
product, R3 section 4.1): roughly **60% engineering-complete, 43% complete including its evidence
gates**. M1.1-M1.3 engineering is demonstrated, the tuning surface exists, and what is missing is the
unobserved creator session and every listening result.

**Scope B — the full-product Beta GO** (all 20 requirements, 83 cases, 18 work packages):

| Milestone | Weight | Engineering | Gates | Weighted |
|---|---|---|---|---|
| M1 usable original-singer session | 20 | 85% | 0% | 8.6 |
| M2 musically usable first voice | 15 | 20% | 0% | 2.3 |
| M3 qualified neural original singer | 20 | 65% | 0% | 4.0 |
| M4 production, languages, style | 20 | 35% | 0% | 4.0 |
| M5 supported standalone and DAW product | 15 | 60% | 0% | 6.0 |
| M6 full-scope Beta GO evidence | 10 | 15% | 0% | 1.5 |
| **Total** | **100** | | | **26.4** |

The rule: a milestone's engineering percentage is the fraction of its named units whose observable
contract exists and passes in this checkout; a gate is a human or external result and counts as zero
until it has happened. A milestone that is implemented but has never been used by a person cannot
exceed the share its gate is worth. No milestone is credited because a file exists.

That is also the honest answer to how much more a beta needs: engineering is roughly a quarter done at
full scope, and the largest remaining block of risk is not code. The concrete blocking items are in
section 9.
## 3. M1 — finish the first usable session

M1.1 and M1.2 are closed. What remains in M1 is three code units and one observation.

### U1.3a — cancel a render during a pending edit, inside the song journey

**Why it is still here.** R3 clause 3 asks the creator's own song to survive a cancel-and-retry. The
coordinator proves this in isolation; the journey never connects it, so nothing today demonstrates
that an ordinary edit performed *while a render is pending* ends in audio that matches the requested
revision through the shipped application path.

**Files.** `tests/test_original_singer_song_journey.cpp` (extend test 2; do not add a target);
`libs/seam-authoring-runtime/include/seam/authoring/render_coordinator.hpp`
(`submit`, `cancel`, `invalidateCurrent`, `progress`, `stats`, `acquireCurrent`) as the API to drive.

**Implementation.** After the tuned export and before the undo step, perform one edit, immediately
call `runtime.renderer().cancel()`, assert `progress()` reports `requestedRevision` above
`publishedRevision` with `audibleAudioStale` true, then make the edit that is wanted and assert the
published audio catches up to the requested revision with `audibleAudioStale` false. Assert the
cancellation is counted in `stats().cancelled` and is not reported as a failure, which is the
distinction `authoring_render_coordinator_cancellation_is_not_failure` already pins at the
coordinator level.

**Exit.** The journey fails if a cancelled render leaves stale audio published, or if a later edit
publishes at the wrong revision. Run
`ctest --test-dir build/release --no-tests=error -R '^seam_original_singer_song_journey_tests$'`.

### U1.3b — copy the installed singer to a draft, change it, and prove the installation is untouched

**Why.** R3 clause 7. `StandaloneApplicationController::copyInstalledSingerToDraft`
(`libs/seam-standalone/src/application_controller.cpp:2343`) and
`distribution::copyInstalledSingerToDraft` (`:2393`) already exist and are reachable through
`ApplicationCommand::CopyInstalledSingerToDraft`, so this is a journey unit, not a new feature.

**Implementation.** In the journey: copy to draft, change one voice-design parameter through
`VoiceDesignerSession`, render the same song, save and reopen the draft, and assert (a) the draft's
export differs from the installed singer's export, and (b) the installed singer's own export is
byte-identical to what it was before the copy. Then undo the design change and assert the draft's
export returns to the installed singer's sound.

**Exit.** A signed installed resource is never mutated by an edit to a copy of it. This is the
assertion that makes "the creator can change the intended voice" load-bearing rather than nominal.

### U1.3c — measure whether the timing edits actually moved sound

**Why.** The timing clause has two halves that are easy to conflate. `movePhonemeBoundary` is proven to
change the compiled phrase; R3 also requires the *audible* phone boundary to be measured per route, and
requires the neural hop quantization to be exposed rather than hidden.

**Files.** `tests/test_original_singer_song_journey.cpp` for the procedural case;
`libs/seam-rendering/src/render_snapshot.cpp` for where hop conversion lives; the pilot tool for the
neural case.

**Implementation.** Add a bounded measurement helper in the test file that locates the first
energy-transition frame in the master around the edited boundary tick, runs it on both the pre-edit and
post-edit masters, and asserts the transition moved by the requested amount within one frame plus a
documented tolerance. For the neural route, assert the requested sample offset is quantized to the hop
and that the quantized value, not the requested one, is what the snapshot carries, so a sub-hop
request is visibly rounded instead of silently honoured.

**Exit.** A timing edit that changes the plan but not the sound fails. Timestamps prove nothing.

### U1.4 — the creator workflow observation (human, blocks M1's completion)

**Why.** No result so far was produced by a person. This unit cannot be automated, and it is the gate
on M1.

**Procedure.** Run `apps/seam-editor-native` at the minimum supported window and at a normal desktop
size. One person, unaided: open the project, enter notes and lyrics, hear the result, tune pitch and
expression, loop a phrase, save, reopen, export. Record task completion, blocking interactions in
order, time to first sound, and edit-to-audible latency. The record is
`docs/implementation/listening/seam-song-01/workflow.md` and must keep the three-line status block.

**Exit.** `Creator workflow: OBSERVED_PASS | OBSERVED_FAIL`. The first observer is the project owner;
this does not count toward M6's five independent pre-GO creators.

### U1.5 — the inspector applicability row (M4.P1 item 6, scheduled here because M1 is where it bites)

**Why.** The lane exists, but a creator cannot see a channel's applicability beside its value outside
the automation band. `TrackInspectorModel::snapshot`
(`libs/seam-native-ui/src/track_inspector.cpp:6`) returns track metadata only — there is no expression
row at all today.

**Files.** `libs/seam-native-ui/include/seam/native_ui/track_inspector.hpp`,
`libs/seam-native-ui/src/track_inspector.cpp`, `libs/seam-native-ui/src/editor_controller.cpp:247`
(the one call site that builds `state.inspector`), plus where `editor_scene.cpp` paints inspector rows.

**Implementation.** Extend `TrackInspectorSnapshot` with a bounded vector of
`{ui::ExpressionChannel channel; std::string label; std::string unit; float valueAtPlayhead; std::string refusal;}`,
filled from `ui::describeExpressionChannel` and the same `resolveSingerRoute` answer the lane uses, so
the row and the lane cannot disagree. Keep the refusal string identical to the lane's. Paint only the
channels that have a stored point or a nonzero playhead value, so the inspector stays compact.

**Exit.** A refused channel shows its refusal in the inspector with the same words the lane uses, and a
supported channel shows its unit and value. Extend `tests/test_native_ui.cpp` rather than adding a
target.

## 4. M2 — make the first voice musically usable (human-gated)

M2 is a two-cycle repair whose input is a listening result. Until a listener exists, only the
diagnostic preparation below is executable, and that is stated rather than filled with busywork.

### U2.1 — the retained listening reference set and an ASR-assisted triage baseline

**Why.** Regression evidence on this project is propositional. There is no versioned listening
reference that survives a change, so nothing can say a voice got worse.

**Files.** `scripts/compare_listening_packets.py` (extend); the existing packet layout under
`docs/implementation/listening/<date>-<id>/`; retained audio outside Git under
`/Users/lhs/Downloads/seam-listening-artifacts/`.

**Implementation.** Add a reference-set manifest binding, per item: score identity, recipe identity and
hash, resource identity, engine/compiler/render revision, render settings, the WAV digest, and the
measurements the pilot tool already writes. Add a promotion rule: a new reference is written beside the
old one and requires an explicit reason; nothing is regenerated in place. Add the ASR triage runner
with model identity, decoding settings and negative controls pinned, and make its output carry the
label `triage` — never `PASS`.

**Exit.** `python3 scripts/compare_listening_packets.py --help` shows the reference set and the
promotion path; a comparison of two builds reports per-item differences and names the reference it
used. This is preparation and can land before any listener exists.

### U2.2 — the repair, when the observation arrives

Cap: two focused cycles of at most eight active engineering hours each, then an explicit route
decision. Use the R3 table for the mapping from observation to owning file. Each cycle must name one
failure class, one hypothesis, the change, and a held-out observation able to disprove it.

**Route decision.** If improvement generalizes, continue. If gains appear only on the probe, damage
other classes, or leave the principal failure unchanged twice, stop and compare alternatives: a better
procedural model, authorized recorded excitation or transients, or the learned path. The
recording-free voice-creation requirement survives any comparison; it cannot be dropped as a shortcut.

## 5. M3 — a real learned singer

M3.1 and M3.2 landed. M3.3 through M3.5 are the remaining engineering, and all three are gated on
material that does not exist yet.

### U3.3 — the local vocoder path, before any sustained training

**Why.** The published OpenVPI vocoders are 44.1 kHz / 128-bin / hop-512 with non-commercial weights,
against SEAM's 48 kHz / 80-bin / hop-256 target, so no published checkpoint is a drop-in.
Reconstruction must be proven locally before acoustic quality is blamed on anything.

**Files.** `tools/voice_model_training/vocoder_batches.py`, `vocoder_optimization.py`,
`vocoder_training_run.py`, `vocoder_checkpoint.py`, `export_vocoder.py` and their tests.
`train_reviewed_vocoder_epoch()` calls `assemble_dataset()` and requires both source and label
admission; that boundary stays.

**Implementation.** Train a reconstruction-only vocoder against the profile in the neural input
record. Note the real shortcut now available: the generated-teacher adapter supplies mel/F0 targets and
aligned spans from a render, so a reconstruction baseline is reachable without an external corpus.
Keep `labelOrigin` recorded and keep `releaseEligible` false — a vocoder trained on renderer-intent
labels cannot inherit a release approval.

**Exit.** Held-out reconstruction with retained numbers; exact hop/padding/trim lengths asserted;
sample-rate and profile mismatch refused; resume after a completed checkpoint and cancellation both
tested. Retain the checkpoint and its receipt.

### U3.4 — connect one real conditioning control end to end

**Why.** `DiffSingerAcousticInputs` carries tokens, durations, F0 and steps with dynamics applied as
output gain. It carries no formant, breathiness, tension, gender or growl. Choose one control the
selected model can learn and wire it through every producer and consumer at once, or the surface will
claim a control the worker ignores.

**Files (must change together).** `tools/voice_model_training/conditioning.py`, `training_run.py`,
`diffusion_export_wrapper.py`, `export.py`, `prepare_bundle.py`,
`libs/seam-neural-synthesis/src/graph_contract.cpp`,
`libs/seam-neural-synthesis/include/seam/neural_synthesis/worker_protocol.hpp`,
`libs/seam-neural-synthesis/src/diffsinger_inputs.cpp`, `apps/seam-neural-worker/main.cpp`,
`libs/seam-rendering/src/render_snapshot.cpp` (`createNeural` at line 753), and the M1 route view.

**Implementation.** Declare name, type, shape, unit, default and supported flag in the graph contract;
validate them at admission; carry bounded frame arrays through the worker protocol with the sample/hop
conversion done once; refuse at `createNeural` any control the admitted graph does not declare. The
twelve existing refusal guards at `render_snapshot.cpp:773-790` are the pattern, and the new control's
guard replaces its entry in that list rather than being added beside it. Bump the conditioning revision
so cache identity changes; never reinterpret old tensors.

**Exit.** A varied input changes the intended output property; neutral reproduces the baseline;
unsupported input refuses; wrong rank, unit, length or normalization rejects; a graph that accepts a
name and ignores the tensor fails the acoustic-effect test.

### U3.5 — qualify an installed neural singer, and close the Windows process gap

**Blocking non-code item first.** `libs/seam-platform/src/helper_process.cpp:260` returns
`Unsupported` for anything that is not Apple or Linux. The neural worker runs through
`runBoundedHelperProcess` (`libs/seam-neural-synthesis/src/neural_phrase_backend.cpp:269`), so
**neural inference cannot run on Windows at all today**, and Windows x64 is half the declared platform
scope. This is a bounded, self-contained port: `CreateProcessW` with an inherited-handle policy, a
bounded wait, a job object for the memory ceiling, and `GetProcessMemoryInfo` for the usage reading
that `:207` currently reports as unavailable. It needs a Windows machine to verify and does not need a
corpus, so it is executable now and should not wait behind U3.3.

**Then.** Replace the arithmetic fixtures in `tests/test_neural_production_render.cpp` with an actual
admitted learned candidate for the retained acceptance run, keeping the fixtures as routing
regressions. Use `qualify-candidate` / `qualification.py` for the measured dossier and leave the human
judgment columns unresolved rather than printing QUALIFIED.

**Exit.** A learned original singer renders a held-out song through ordinary UI selection on macOS
arm64 **and** Windows x64, with permissions, a compatible vocoder and a reviewed result recorded.

## 6. M4 — production, languages and paired styles

### U4.1 — two producer sessions, one recorded and one generated

Reuse `libs/seam-platform/src/recording_session.cpp`,
`libs/seam-native-ui/src/voicebank_studio_production_edits.cpp`,
`voicebank_studio_sample_review.cpp`, `voicebank_studio_campaign.cpp` and
`libs/seam-voicebank-production/src/repository_operations.cpp`. For each of one authorized recording
and one generated source: capture or import, inspect markers, edit, reject a take, regenerate a
replacement, invalidate the stale review, obtain a fresh one, publish, install, and sing a held-out
song. Repair reachability and stale-revision defects at the shared operation.
`libs/seam-voicebank-production/src/repository_candidate.cpp:474` currently rejects multiple styles by
design; that stays until U4.3 provides a real pair.

**Blocker.** An authorized recording does not exist. The generated half is executable now.

### U4.2 — JP / EN / KO lyric workflows

Japanese is the pilot language and has the most coverage; English needs dictionary breadth beyond its
bootstrap lexicon; Korean needs reviewed lexical and context exceptions. The dictionary becomes a
versioned resource whose identity enters pronunciation and cache identity. A singer's declared phone
inventory must match what the language front end emits.

**Files.** `libs/seam-phonemizer/src/pronunciation_resolver.cpp`, the three language phonemizers,
`tests/test_english_phonemizer.cpp`, `test_korean_phonemizer.cpp`, `test_phonemizer.cpp`.

**Blocker.** Native-speaker review. Expansion into the three languages is not gated on it, but no
language may be *claimed* without it.

### U4.3 — paired styles

`StyleBlend` is vocabulary and a capability name; there is no interpolation. First select two reviewed
aligned styles of one singer with a common usable range and matching phone coverage, then implement a
renderer-specific blend against that material. Test endpoints, a midpoint, pitch and timing
continuity, missing-pair refusal, undo/reload/export and cache identity. A raw crossfade invites phase
cancellation and double-voice artifacts and is not assumed to be the answer.

**Files.** `libs/seam-voicebank/src/style_resolution.cpp`, `libs/seam-synthesis/src/unit_selection.cpp`,
`libs/seam-synthesis/src/renderer_capabilities.cpp` (`StyleBlend`), `render_snapshot.cpp`.

**Blocker.** Two compatible aligned styles do not exist. Producing a second style is necessary and
insufficient: the pair needs matching coverage, a usable common range, and alignment or conditioning
suited to the algorithm.

## 7. M5 — supported standalone and DAW product

### U5.1 — the Windows platform port (scheduled early because it blocks two milestones)

Beyond the helper-process work in U3.5, the declared host matrix covers Windows x64 REAPER and Bitwig
for both CLAP and VST3. `scripts/build_windows_installer.ps1` and `package_windows_plugin.ps1` exist;
what does not exist is a measured run on that platform. This unit is one machine plus one build plus
recorded host evidence, and macOS results cannot substitute for it.

### U5.2 — interchange, character and recovery on real hosts

SMF/USTX round-trips and the character binding are implemented and tested; what is missing is the
supported-host run. `tools/external_beta` already owns the cohort and host evidence scripts.

## 8. The design critique, converted to work

The owner asked for a design-improvement plan naming overlapping notes, text overflow and underused
character assets. Two of the three are real and localized; the third is partly handled already and
needs a stated rule rather than a rebuild.

### 8.1 Overlapping notes (real — D3)

**Change `libs/seam-editor-ui/src/note_visual_layout.cpp`.** Keep the band stacking, which is the
correct legibility answer for a dense chord, and add to `NoteVisualLayout` a typed reason:
`enum class NoteVisualCrowding { none, timeOverlap, densityOnly }`. A note belongs to `timeOverlap`
when another note of the same pitch sounds during any part of its span, and to `densityOnly` when the
group exceeded three bands without any same-pitch collision. Publish `crowding` through
`PianoRollNoteVisual` (`libs/seam-editor-ui/src/piano_roll_model.cpp:106` replaces
`drawsOverlapIndicator`) and paint a distinct marker: a hatch for `timeOverlap`, a count badge for
`densityOnly`. The inspector then gets one row naming the actual cause, because printing OVERLAP for a
dense chord is a false statement.

**Proving test.** Extend `tests/test_ui.cpp` with a four-note same-pitch collision and a four-note
dense non-colliding run; assert the two report different `crowding` values and counts, and that a
two-note collision reports overlap rather than density.

### 8.2 Text overflow (partly handled — one rule to finish)

The scene already clips most strings and ellipsizes long character names
(`libs/seam-native-ui/src/editor_scene.cpp:340` and `:1114`), so this is narrower than it looks. The
unhandled cases are the two added most recently: the expression lane's refusal text and unit hint
(`:846`, `:867`), and the singer capability summary in the picker. Both are variable-length strings
drawn into fixed-width regions.

**Change.** Add one helper in `editor_scene.cpp` — `ellipsizeToWidth(canvas, text, rect, font)` — and
route those call sites through it, so every variable-length string in the editor shares one policy.
Then extend `tests/test_expression_lane.cpp` (which already has a minimum-window case) with a
maximum-length refusal and unit string and assert the drawn text stays inside its rectangle.

### 8.3 Character assets (real — D2)

Two distinct problems, and they should land as two commits.

**Fix the visibility predicate (D1).** `editor_runtime_paint.cpp:29` and
`native_editor_app.cpp:1420` must both ask the package whether it declares the full turnaround, not
whether one particular state decoded. Add `CharacterPresentation::declaresTurnaround()` over the
existing all-six-state rule in `libs/seam-character/include/seam/character/character.hpp:49` and use
it in both surfaces.

**Use the artwork that exists, then extend it.** `assets/character-01/runtime/*.ppm` is 320x480 and
the standalone header draws it into a 48x48 square
(`libs/seam-native-ui/include/seam/native_ui/editor_scene.hpp:446`), which discards the aspect ratio
and wastes the detail; `production-development/portrait-512.png` (512x768) and `key-art-1024.png` are
referenced by their own asset manifest and used by nothing in `libs/` or `apps/`. So: (1) give the
header portrait an aspect-correct fit and use the 512x768 portrait, (2) declare `mouthAssets` for the
six `MouthShape` values so the dock can stop drawing a glyph, and (3) wire `hasPerformanceAssets()` so
a performance-capable package is reported. The artwork itself is a content task; the code change is
bounding-box fit plus reading the declared map.

**Note the license boundary.** `assets/character-01` is `developmentOnly: true` with
`productionStatus: NOT_A_PRODUCTION_TURNAROUND`, so this improves the development presentation. A
production turnaround is separate work and must not inherit that approval.

## 9. What is not executable now, and who or what unblocks it

| Unit | Blocking input | Who provides it |
|---|---|---|
| U1.4 creator observation | one person, unaided session | project owner |
| U2.1 listening result | a Japanese-capable listener for triage; a musician for phrasing | owner + a recruited listener |
| U2.2 repair | the observation above | follows from U2.1 |
| U3.3 vocoder | compute time; the generated-teacher path removes the corpus dependency | none external |
| U3.5 Windows neural | a Windows x64 machine | owner |
| U4.1 recorded source | an authorized recording with permissions | owner, external |
| U4.2 language claims | native-speaker review per language | owner, external |
| U4.3 style pair | two compatible aligned styles | follows from U4.1 |
| M6 five independent creators | five participants meeting the canonical independence protocol | owner, external |

Nothing above is unblocked by more code. The next executable engineering, in order, is: U1.3a, U1.3b,
U1.3c, U1.5, D1/D2/D3, U3.5's Windows helper port and U5.1, U2.1, U3.3, then the material-gated work.

## 10. Suggested commit boundaries

One reviewable commit per unit, in this order, each with its own tests:

1. `U1.3a` cancel during a pending edit, in the song journey.
2. `U1.3b` copy-to-draft leaves the installation untouched.
3. `U1.3c` a timing edit is measured, not inferred.
4. `D1 + D2` one visibility predicate for the character in both surfaces.
5. `D3` overlap and density are different claims.
6. `U1.5` the inspector states a channel's applicability.
7. `8.2` one ellipsis policy for variable-length editor strings.
8. `8.3` aspect-correct portrait use, then declared mouth artwork.
9. `U3.5` the Windows helper-process port.

Update `docs/implementation/INTEGRATED_SINGER_EXECUTION.md` once per landed slice, insert-only at the
top, and keep the three-line status block. A documentation-only change is not a unit.

## 11. Runbook

Run from the repository root. If the desktop terminal hangs with the repository as `workdir`, launch
with `workdir: "/"` and `cd` here explicitly.

```sh
cmake --build build/release --parallel 8 --target <target>
ctest --test-dir build/release --output-on-failure --no-tests=error -j 8 -R '^<target>$'
ctest --test-dir build/release --output-on-failure --no-tests=error -j 8
git status --short
git add <explicit paths>
python3 scripts/verify_tracked_source_closure.py      # must print SOURCE_CLOSURE=PASS
git commit
git push origin HEAD && git push origin HEAD:master
git rev-parse HEAD origin/master                       # must match
```

Registration reminder: C++ test files are not automatically separate CTest targets. `seam_tests` is
built from the explicit `SEAM_TEST_SOURCES` list in `CMakeLists.txt:1596`, so a new suite needs both a
target and a registration. Run `./build/release/seam_tests` from the repository root when it uses
asset-relative fixtures.

## 12. Evidence labels for every completed unit

```text
Engineering:       DEMONSTRATED | PARTIAL | NOT_RUN
Creator workflow:  OBSERVED_PASS | OBSERVED_FAIL | NOT_OBSERVED
Musical review:    ACCEPTED | REJECTED | NOT_REVIEWED
```

These are reporting labels. Final qualification uses the existing canonical typed records and
independent reviews; do not invent a second evidence schema for them.

## 13. Eligibility boundary

Prepared by the primary implementer at `3454ce9f`. `SEAM_JOINT_DEVELOPMENT_PLAN_R3_2026-09-15.md`
remains the agreed document until the second developer (`01a0a066`) has read this one and recorded
agreement or blocking corrections. Section 1's reconciliation, section 2's percentages and the three
defects in section 1.1 are reviewable against the cited source; everything from section 3 onward is a
proposal.

No implementation, training, musical approval, release acceptance or commit/push is performed by
publishing this document.

