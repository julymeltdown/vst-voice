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

These three are **not** reproduced failures. They are places where the code and its own stated
intent diverge, found by reading. D1 is a latent divergence with no reproduction available; D2 and D3
are live gaps whose effect is visible on every run, which is why those two are drawn from the owner's
design review and D1 is not. Each is listed with the test that would prove or disprove it, and each is
scheduled below rather than fixed silently, so a fix is reviewable on its own commit.

**D1 — the two surfaces decide dock presence by asking different questions.**
This one was overstated in the first draft of this section, and a review round corrected it in yet a
different direction, so the resolution is stated at length here to stop a third reader repeating it.
Two facts are easy to conflate:

- `Manifest::validate()` (`libs/seam-character/src/character.cpp:94`), `loadPackage` (`:204`) and
  `CharacterPresentation::load` (`libs/seam-native-ui/src/character_presentation.cpp:16`) between them
  require all six states to be declared *and* to decode; a package missing one is refused at load. The
  `defaultState` fallback in `Manifest::assetFor` (`:116`) and `portrait()` (`:42`) therefore does not
  make a partial turnaround renderable — with the current loader it is unreachable for any shipped
  package, which is why no surface can disagree with another about a partial one today.
- What *is* asymmetric is the predicate each surface uses.
  `libs/seam-clap-editor/src/editor_runtime_paint.cpp:29` gates dock **layout** on
  `character_.portrait(character::State::Neutral) != nullptr`, an asset lookup.
  `libs/seam-standalone/src/native_editor_app.cpp:1420` gates nothing: it publishes whatever
  `portrait()` returns and lets the dock decide from the display mode. The same question — should this
  window reserve the character dock — is answered by an asset lookup in one surface and by
  `CharacterDisplayMode` in the other. D2 below asks for mouth artwork, and the character-state work
  that follows is exactly the change that would make this divergence real.

*Proving test:* one shared predicate over the manifest and the display mode, called by both surfaces,
plus an assertion that a manifest missing any state is refused at load — the second half pins behaviour
that already holds so a later change cannot quietly weaken it.

**D2 — the character cannot open its mouth, because no shipped package declares mouth artwork.**
`libs/seam-standalone/src/native_editor_app.cpp:1418` asks for `character_.mouth(frame->mouth)`, and
for a status-only package that answer is empty: `assets/character-01/manifest.json` has no
`mouthAssets` block, `declaresPerformance()` is false, and `hasPerformanceAssets()` has no call site
anywhere in `libs/` or `apps/` — it is *tested* but never *consulted* by a shipping surface
(`tests/test_character_package_performance.cpp:107,129,195` against the declaration at
`libs/seam-native-ui/include/seam/native_ui/character_presentation.hpp:34`). The dock therefore falls
back to drawing a mouth glyph
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
product, R3 section 4.1): **85% engineering, 15% gates**, on the same rule as the table below. M1.1-M1.3
engineering is demonstrated and the tuning surface exists; what is missing is unit U1.3a-c, the
inspector applicability row, and then the unobserved creator session and every listening result. This
scope therefore looks far more complete than the full-product column and that is not a contradiction:
the first-milestone definition deliberately excludes neural singing, three languages, the DAW matrix
and production inventory, so its gate share is much smaller.

**Scope B — the full-product Beta GO** (all 20 requirements, 83 cases, 18 work packages):

The rule for the two published columns: a milestone's *engineering* share is the fraction of its named
units whose observable contract exists and passes in this checkout, and a milestone's *gate* share is
the fraction of its acceptance that needs a human or an external result. The two are complementary, so
engineering plus gates is 100% and the internally checkable figure is the product of the two shares:

| Milestone | Spec share | Engineering | Gates | Checkable now |
|---|---|---|---|---|
| M1 usable original-singer session | 20% | 85% | 15% | 17.0% |
| M2 musically usable first voice | 15% | 20% | 80% | 3.0% |
| M3 qualified neural original singer | 20% | 65% | 35% | 13.0% |
| M4 production, languages, style | 20% | 35% | 65% | 7.0% |
| M5 supported standalone and DAW product | 15% | 65% | 35% | 9.8% |
| M6 full-scope Beta GO evidence | 10% | 5% | 95% | 0.5% |
| **Total** | **100%** | | | **50.3% weighted engineering** |

Read the two halves apart, because neither alone is the answer. **The engineering half is about 50%
of the specification's own weight** — that is the part a developer can still close without help, and it
is why the next executable queue in section 9 is long. **The gate half is near zero**, because no
human has observed the workflow, no listener has judged any audio, no authorized source exists, no
native language has been reviewed and no Windows machine has run the product. The gate shares are what
the remaining distance actually consists of, and a completed engineering column would not move them.

Three notes on how these figures were chosen, and one on what changed, so they can be argued with
rather than trusted.

The specification shares follow the count of units each milestone owns in section 10 of R3, rounded to
the nearest five percent, with M6 kept small because it consumes the other five milestones' evidence
rather than producing its own.

Two engineering figures changed in the review round of this document, and both are recorded rather than
silently adjusted, because a number that moved without a stated reason is the one cell a reader cannot
argue with. **M6 dropped from 15% to 5%**: it was reported as mostly engineering-checkable, which was
wrong in the revealing direction, since M6's content is independent reviewers, five pre-GO creator
sessions and restored-archive audits — the most human-gated milestone in the project, not the least.
**M5 rose from 60% to 65%** on re-reading the milestone: its deterministic halves are further along than
the first draft credited, because SMF and USTX round-trips, the character binding and the recovery paths
are implemented and tested, and what M5 still lacks is concentrated in the host/platform runs recorded
against the matrix rather than evenly spread across its units. Its gate share of 35% is that host-run
evidence, which is why the engineering rise does not move M5 much.

An earlier draft of this section published a single weighted total of 26.4% and a column that was not
derivable from the rule printed beside it, which defeated the purpose of stating the rule. That column
is gone; the total now says what it is — weighted engineering, not completion.
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

A review of this unit found that "held-out reconstruction with retained numbers" names retention but
not the measurement, and a vocoder can produce plausible audio that is not a reconstruction of its
input. State the comparison instead: **the rendered output against the source audio its mel and F0 were
extracted from**, by a named spectral distance plus an F0 error, so "reconstruction" cannot be
satisfied by "it ran and sounded like singing". The same pitch measurement added to U3.5 applies here,
since a vocoder that shifts the pitch of its own input is not reconstructing it.

**Exit.** A named reconstruction measurement over held-out items, with its reference and the numbers
retained; exact hop/padding/trim lengths asserted; sample-rate and profile mismatch refused; resume
after a completed checkpoint and cancellation both tested. Retain the checkpoint and its receipt.

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
scope.

It is worse than one signature. The same `#else` at `helper_process.cpp:129` disables the *second*
production caller, `JapaneseReadingCapture::read`
(`libs/seam-authoring-runtime/src/japanese_reading_capture.cpp:97`), so Japanese reading capture is
unavailable on Windows as well. The port therefore unblocks R9 **and** part of the M4.2 Japanese
workflow — three surfaces, not two — and whoever does it has two callers to qualify, not one. That is
also why U5.1 is scheduled ahead of the milestones it appears to serve.

This is a bounded, self-contained port: `CreateProcessW` with an inherited-handle policy, a bounded
wait, a job object for the memory ceiling, and `GetProcessMemoryInfo` for the usage reading that `:207`
currently reports as unavailable. It needs a Windows machine to verify and does not need a corpus, so it
is executable now and should not wait behind U3.3.

**The dossier could not tell a singer from a noise generator, and now it can.** A review of this unit
found that `qualification.py` answered six automatic criteria — bundle admission, response binding,
vocabulary coverage, determinism, finite audio, runtime budget — and **not one of them asked whether
the audio sings the requested notes.** A model a fifth flat, or producing the wrong phones, is finite,
non-silent, deterministic and fast, and reached the dossier as PASS with only the human columns
unresolved. That is the same failure this week found in the editor one layer down: the contract is
satisfied and the sound is wrong. The requested frequency was already captured as conditioning and
never compared against what came back.

`pitch-adherence` now makes that comparison. It measures the median voiced pitch of the returned audio
by sub-sample-refined autocorrelation, over the central half of the item so onset and release do not
decide the answer, and reports the error against the item's declared target with an octave error named
when it is one. The window is sized from the requested note rather than fixed, so a low note and a high
note are measured with the same reliability. It is deliberately a gross-error detector, not a tuning
judgement: the tolerance is a quarter tone, because a listener has not judged anything yet and a
candidate must not be failed for vibrato or portamento.

Two outcomes are kept distinct, because conflating them would blame the model for an unusable item. An
item whose audio contains no measurable pitch is a **FAIL**: it was measured and it does not sing. An
item too short to contain the note it names is **UNRESOLVED**: the audio cannot support a pitch claim at
all. The criterion is reported last, so a worker that crashed, disagreed with itself or overran its
budget still reports that more fundamental finding first.

**Then.** Replace the arithmetic fixtures in `tests/test_neural_production_render.cpp` with an actual
admitted learned candidate for the retained acceptance run, keeping the fixtures as routing
regressions. Use `qualify-candidate` for the measured dossier and leave the human judgment columns
unresolved rather than printing QUALIFIED.

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

### U5.1 — the Windows platform port (gated on a machine, not on code)

Beyond the helper-process work in U3.5 — which this unit inherits and must not duplicate, since the
same port also gates Japanese reading capture on Windows (`libs/seam-authoring-runtime/src/japanese_reading_capture.cpp:97`) —
the declared host matrix covers Windows x64 REAPER and Bitwig for both CLAP and VST3.
`scripts/build_windows_installer.ps1` and `package_windows_plugin.ps1` exist; what does not exist is a
measured run on that platform. This unit is one machine plus one build plus recorded host evidence, and
macOS results cannot substitute for it.

### U5.2 — interchange, character and recovery on real hosts

SMF/USTX round-trips and the character binding are implemented and tested; what is missing is the
supported-host run. `tools/external_beta` already owns the cohort and host evidence scripts.

## 8. The design critique, converted to work

The owner asked for a design-improvement plan naming overlapping notes, text overflow and underused
character assets. All three are real; they are not all the same *kind* of real, and the difference
decides the work. Overlap (8.1) is a modeling gap in one function. Text overflow (8.2) is one policy
the scene already applies in most places and omits in two recently added call sites, so it needs one
helper and not a rebuild. Character assets (8.3) is the only one that needs authored content as well
as code, and the code half of it is small.

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

**Unify the dock-presence predicate (D1).** `editor_runtime_paint.cpp:29` and
`native_editor_app.cpp:1420` answer one question — should this window reserve the character dock — with
two different inputs: an asset lookup in the plug-in and the display mode in standalone. Add one
predicate, `CharacterPresentation::dockVisible(CharacterDisplayMode)`, that reads the manifest and the
mode together, and call it from both surfaces so the question has one answer. It must not be built on
`portrait(Neutral) != nullptr`: because all six states are mandatory at load (D1 in section 1.1), that
lookup is a mere existence test whose failure has a different meaning than the layout assumes. Keep the
existing `defaultState` fallback in `Manifest::assetFor` — it is reachable for a hand-built in-memory
manifest and is not part of this question.

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
U1.3c, D1, U1.5, U2.1, D2, D3, `8.2`, `8.3`, U3.3, then the material-gated work.

Two reorderings from the first draft of this section, both because an item was smaller than it looked.
**U3.5's Windows helper-process port and U5.1's Windows host run moved out of the early queue**: they
are real and they block three surfaces, but they need a Windows machine that is not present, so they
belong in the blocked table's neighbourhood rather than at the head of an executable list. **D1** moved
ahead of D2 rather than riding with it, because D1 is one predicate with two call sites, while D2 needs
authored artwork; separating them keeps one of the pair in the executable queue.

## 10. Suggested commit boundaries

One reviewable commit per unit, in this order, each with its own tests:

1. `U1.3a` cancel during a pending edit, in the song journey.
2. `U1.3b` copy-to-draft leaves the installation untouched.
3. `U1.3c` a timing edit is measured, not inferred.
4. `D1` one dock-presence predicate for the character, called by both surfaces.
5. `U1.5` the inspector states a channel's applicability.
6. `U2.1` the listening reference set and the ASR triage runner.
7. `8.2` one ellipsis policy for variable-length editor strings.
8. `D3` overlap and density are different claims.
9. `8.3` aspect-correct portrait use, then declared mouth artwork.
10. `U3.3` the local vocoder reconstruction baseline.

Deferred until the machine exists: `U3.5`'s Windows helper-process port and `U5.1`'s Windows host run,
as one reviewable change, since they share one port and one verification environment.

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

## 13. Joint review record

Participants: the primary implementer, and the second developer (task `01a0a066`, the author of
`SEAM_SECOND_DEVELOPER_REVIEW_2026-09-15.md`). The second developer reviewed R4 read-only at
`f82b0e8c` and answered with agreement plus two blocking corrections; both are landed above.

| Raised against | Correction | Landed as | Deciding source |
|---|---|---|---|
| `1.1` D1 mechanism | The `defaultState` fallback means a partial package is not invisible in one surface; the real asymmetry is that one surface gates layout on a portrait lookup and the other on display mode | D1 rewritten as a latent divergence, with the proving test changed | `libs/seam-character/src/character.cpp:94` and `:204` require all six states; `libs/seam-clap-editor/src/editor_runtime_paint.cpp:29` vs `libs/seam-standalone/src/native_editor_app.cpp:1420` |
| `2` percentage table | The weighted column was not derivable from the stated rule, and M6 was undiscounted in the human-gated direction it should be most discounted in | Weighted column and the 26.4% total dropped; engineering and gate shares published separately, each statement above rederived | The section's own rule, applied to `docs/product/full-product-beta-contract.json` and R3 section 9 |
| `1.1` D2 | `hasPerformanceAssets()` is tested but never consulted by a shipping surface | Added to D2 | `tests/test_character_package_performance.cpp:107,129,195` |
| `3` U3.5 | A second production caller, Japanese reading capture, is disabled by the same `#else` | Added to U3.5 and cross-referenced from U5.1 | `libs/seam-authoring-runtime/src/japanese_reading_capture.cpp:97`; `libs/seam-platform/src/helper_process.cpp:129` |
| `13` (withdrawn) | The reviewer proposed that `validate()` only checks declared states, so a one-state-missing package loads | Withdrawn by agreement: `character.cpp:94`, `:204` and `character_presentation.cpp:16` all require all six states | `libs/seam-character/src/character.cpp:94` |

This document's first draft contained an overstated D1 and an underivable total. Both were found by
review rather than by the author, which is the argument for keeping that review in the loop rather than
stamping a plan the implementer wrote alone.

### Eligibility boundary

`SEAM_JOINT_DEVELOPMENT_PLAN_R3_2026-09-15.md` is superseded for work selection by this revision, which
the second developer has now reviewed. Section 1's reconciliation and section 2's shares are checkable
against the cited source; everything from section 3 onward remains a proposal for the owner's
direction, not an accepted unit.

No implementation, training, musical approval, release acceptance or commit/push is performed by
publishing this document.
