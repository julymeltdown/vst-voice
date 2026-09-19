# SEAM Detailed Development Plan — Code-Level Execution Guide

---
title: SEAM Detailed Development Plan — Code-Level Execution Guide
date: 2026-09-19
baseline_commit: c31f8c17
branch: codex/production-readiness-completion
scope_authority: VIRTUAL_SINGER_FEASIBILITY_AND_CODE_ROADMAP_2026-09-05.md (R1-R20, V01-V18)
plan_authority: SEAM_JOINT_DEVELOPMENT_PLAN_R4_2026-09-16.md
contract_authority: docs/product/full-product-beta-contract.json (20 requirements, 83 cases, 18 work packages)
status: execution plan; no implementation performed by this document
---

## 1. Overall Goal

Build an original virtual singer product (Hatsune Miku-class capability) where creators can design or
acquire an original voice, produce and edit its reusable singer resources, and finish expressive songs
through SEAM's supported standalone and DAW workflows, across macOS arm64 and Windows x64.

Beta GO requires all 20 requirements (R1-R20), all 18 work packages (V01-V18), and the existing
External Beta release prerequisites to have accepted evidence for the same release candidate.

## 2. Current State Summary

### 2.1 Codebase metrics

- C++ source files (libs): 617 (.cpp/.hpp)
- C++ application files (apps): 38
- Test files: 172
- Python tools: 230
- Registered CTest targets: 172 (local release build)
- Source closure: PASS
- Branch: codex/production-readiness-completion, 34 commits ahead of origin/master

### 2.2 CI status

Latest run (35362687374): 165/167 tests pass. Two failures, both in the same test case
authoring_render_coordinator_orders_same_revision_publications:

- seam_authoring_render_coordinator_tests (#57)
- seam_tests (#106, which includes the same file)

The failure is a genuine stall in the GCC -O3 Release build on Linux: the second (Final-quality)
render never returns within 120 seconds. This is pre-existing and intermittent, not introduced by
recent commits. Local macOS builds pass 12/12 runs.

### 2.3 Landed R4 units

U1.3a-c, D1, U1.5, 8.2, D2, 8.3, U2.1, U3.3, U3.4, U3.5 (bounded Windows process), U4.2a (Windows
private reading staging). D3 retired (overlap indicator is truthful; premise was false).

### 2.4 Progress by milestone (from R4 section 2)

| Milestone | Weight | Engineering | Gates | Weighted checkable |
|---|---|---|---|---|
| M1 usable original-singer session | 20% | 85% | 15% | 17.0% |
| M2 musically usable first voice | 15% | 20% | 80% | 3.0% |
| M3 qualified neural original singer | 20% | 65% | 35% | 13.0% |
| M4 production, languages, style | 20% | 35% | 65% | 7.0% |
| M5 supported standalone and DAW product | 15% | 65% | 35% | 9.8% |
| M6 full-scope Beta GO evidence | 10% | 5% | 95% | 0.5% |
| Total | 100% | -- | -- | 50.3% weighted engineering |

The engineering half is about 50% of the specification weight. The gate half (human observation,
listening judgments, authorized recordings, native-speaker reviews, Windows host evidence, independent
creators) is near zero.

## 3. Blocking CI Issue — Render Coordinator -O3 Stall

### 3.1 Problem

Test authoring_render_coordinator_orders_same_revision_publications stalls on Ubuntu GCC -O3.
The diagnostic shows: pubQuality=0 (Preview), progState=2 (Rendering), pubCalls=1 (second
beforePublication hook never invoked), stale=1.

### 3.2 Root cause hypothesis

The wrappedPhaseDifference function at spectral_classic.cpp:78 uses unbounded while loops:

    while (difference > pi) difference -= 2*pi;
    while (difference < -pi) difference += 2*pi;

If target or source is NaN or infinity, these loops spin forever. GCC -O3 may optimize floating
point differently, producing non-finite values that do not occur under Clang.

### 3.2b Root cause hypothesis is disproven as written (September 19)

Section 3.2 proposed that `wrappedPhaseDifference`'s unbounded `while` loops spin forever on non-finite
input under GCC -O3. That guard is already in the tree at commit `add7332f` ("guard
wrappedPhaseDifference against non-finite inputs and use bounded remainder"), and the failure persists,
so the hypothesis is disproven as the explanation for this test.

Two further observations narrow it, both from CI logs rather than from reading:

- The two failing tests are `seam_authoring_render_coordinator_tests` and `seam_tests`, the same test
  case in both, and the diagnostic differs between runs (`progState=2, stale=1` in one, `progState=1,
  stale=0` in the next). A deterministic compiler misoptimization would not change its own diagnostic
  between runs of the same commit.
- Commit `24db1b55` failed this job while changing only `BETA_READINESS_ISSUES.md`, and `529f006e`
  passed `project-seam-ci` while failing only `phase11-plugin-formats`. No C++ changed in either
  direction, so this is an intermittent scheduling failure in the coordinator, not a code regression.

The reliable reproduction and fix now require either an Ubuntu GCC environment with the coordinator
under instrumentation, or a host-level investigation of the 20-millisecond coalescing window in
`workerLoop` and `submitWithSources`, where a second submitted request can be consumed as stale before
the first publication completes. Neither is available on this machine, which builds with Clang and
passes 19 of 19 locally on every run. **Phase 1's exit criterion is therefore not reachable by editing
the file named in 3.4.** The two CI-blocking defects that were reachable, the uncompilable USTX oracle
and the stale phase11 source verifier, are fixed and pushed.

### 3.3 Fix plan

1. Guard wrappedPhaseDifference against non-finite inputs: return 0 for NaN/inf.
2. Add instrumentation to workerLoop to report which stage the second render stalls in.
3. Push and verify against CI.

### 3.4 Files to change

- libs/seam-synthesis/src/spectral_classic.cpp:78 -- guard the while loops
- tests/test_authoring_render_coordinator.cpp -- optional: add stage-reporting hooks

## 4. Development Phases

### Phase 1: CI Green + Merge to Master (Priority: IMMEDIATE)

Goal: Get all CI jobs green, merge to master.

Tasks:
1. Fix wrappedPhaseDifference infinite loop vulnerability
2. Verify fix passes CI isolated-release-candidate job
3. Once all 5 CI jobs pass, fast-forward merge to master
4. Push master

Files: spectral_classic.cpp, possibly render_coordinator.cpp
Duration estimate: 1-2 hours engineering, 30 min CI verification

### Phase 2: Score Correctness Foundation (V02-V05)

Goal: Phoneme timing, complete phrase pitch, persisted expression, truthful capabilities.

#### 2a. V02 -- Make phoneme boundaries authoritative

Target files:
- libs/seam-synthesis/include/seam/synthesis/timing_solver.hpp
- libs/seam-synthesis/src/timing_solver.cpp
- libs/seam-domain/include/seam/domain/phoneme.hpp
- libs/seam-authoring-runtime/src/technical_edit_controller.cpp

Implementation:
- Define how microsecond timing overrides combine with note anchors
- Extend unit source mapping for multiple phonemes within one unit
- Keep source markers and musical target timing distinguishable
- Create focused phoneme_timing_plan module if needed

Exit criterion: phoneme timing edits audibly move the intended boundary; same-note multi-syllable
lyrics remain sequential.

#### 2b. V03 -- Compile complete phrase pitch and articulation

Target files:
- libs/seam-synthesis/src/unit_selection.cpp
- libs/seam-synthesis/include/seam/synthesis/pitch_curve.hpp
- libs/seam-synthesis/src/phrase_renderer.cpp
- libs/seam-rendering/src/render_snapshot.cpp
- Create libs/seam-synthesis/include/seam/synthesis/performance_compiler.hpp + impl

Implementation:
- Compile entire melody, slur/reattack intent, manual offsets, and voiced masks into frame-aligned data
- Give each unit a view of the performance rather than a single target note
- Resolve pronunciation and canonical note/syllable relationships before dividing into renderer chunks
- Distinguish musical phrase boundary from processing chunk in phrase_segmenter.cpp
- Hash chunk dependencies; invalidate affected neighbors on context change

Exit criterion: cross-note unit and melisma follow every note; articulation changes sound as
specified; tempo changes remain correct.

#### 2c. V04 -- Add persisted vibrato and dynamics with migration

Target files:
- libs/seam-domain/include/seam/domain/note.hpp
- libs/seam-domain/include/seam/domain/project.hpp
- libs/seam-domain/include/seam/domain/render_controls.hpp
- libs/seam-formats/src/project_json.cpp
- libs/seam-clap-editor/src/editor_runtime_state.cpp

Implementation:
- Create typed dynamics and note vibrato model
- Use cents, time/fraction units, finite/range checks, deterministic interpolation, zero-effect defaults
- Single schema upgrade for the agreed vocabulary
- Preserve schema 1-7 musical data through migration
- Version render identity/algorithm revisions; invalidate affected caches

Exit criterion: vibrato and gain edits survive undo/redo, save/reopen, autosave/recovery, plugin
state, preview, and final export.

#### 2d. V05 -- Make capabilities and fallback truthful

Target files:
- libs/seam-synthesis/include/seam/synthesis/renderer_dispatcher.hpp
- libs/seam-synthesis/src/renderer_dispatcher.cpp
- libs/seam-synthesis/src/raw_renderer.cpp
- libs/seam-rendering/include/seam/rendering/pcm_cache.hpp
- libs/seam-rendering/src/region_renderer.cpp

Implementation:
- Create compiled capability table for implemented controls
- Repair Raw pitch evaluation or reject unsupported requests
- Preserve actual backend/fallback/capability information in cache
- Version persisted cache representation

Exit criterion: no control silently disappears after a fallback or cache hit; final export truthfully
states degraded rendering.

Phase 2 duration estimate: 40-60 hours engineering

### Phase 3: Voice Construction (V07, V15, V16)

Goal: Procedural voice recipe, real recording/import, editable generation, pilot bank.

#### 3a. V15 -- Build procedural voice recipes and synthesis

Create files:
- libs/seam-voice-design/include/seam/voice_design/voice_recipe.hpp
- libs/seam-voice-design/src/voice_recipe.cpp
- libs/seam-voice-design/src/phonation_source.cpp
- libs/seam-voice-design/src/vocal_tract.cpp
- libs/seam-voice-design/src/articulation_plan.cpp
- libs/seam-voice-design/src/procedural_renderer.cpp
- tests/test_voice_design.cpp
- tests/test_procedural_voice.cpp

Implementation:
- Recipe and synthesis responsibilities per roadmap section 15
- Voiced source, resonances, aspiration/frication, consonant closures/bursts, transitions
- Style poses and controlled modulation
- Preview phrases and unit-baking requests with declared bounds and revision identity

Exit criterion: vowel identity survives pitch changes; CV/VC transitions distinguishable; parameter
edits affect named property; same recipe/seed/runtime reproduces result.

#### 3b. V07 -- Produce the first coherent voicebank

Target files:
- libs/seam-voicebank-production/ (multiple files)
- Native Voicebank Studio modules
- Inventory data, source/retake/quality records

Implementation:
- Define performer/source, language, range, phonetic inventory, pitch layers, styles, source rights
- Record/review neutral pilot; fix renderer against it; expand coverage
- Implement five production repairs: kind-aware QC, review state transitions, real-candidate builder,
  retake/inventory alignment, versioned conditioning
- Version production assignment identity to include resource/language, style, coverage key, pitch layer

Exit criterion: a complete import-to-review-to-retake-to-candidate workflow produces an installable bank.

#### 3c. V16 -- Connect Voice Designer, recording, and bank production

Create files:
- libs/seam-native-ui/include/seam/native_ui/voice_designer_model.hpp
- libs/seam-native-ui/src/voice_designer_model.cpp
- libs/seam-voicebank-production/src/repository_generation.cpp
- tests/test_voice_designer_workflow.cpp

Implementation:
- Expose saved recipes, sliders/envelopes, vowel/word/phrase auditions, A/B comparison
- Undo, range/style settings, selected-unit batch generation
- Import generated WAVs as immutable takes with per-source recipe lineage
- Share domain commands with batch/CLI operations

Exit criterion: create voice, save/reopen, generate units, edit, retake/regenerate, review,
install, sing unseen phrase. Cancel and restart batch without duplicate approval.

Phase 3 duration estimate: 80-120 hours engineering

### Phase 4: Classical Creator Instrument (V06, V08-V11)

Goal: Qualified sample rendering, styles/blending, complete tuning UI, language interfaces, exchange.

#### 4a. V06 -- Improve voiced/unvoiced and transition processing

Target files:
- libs/seam-synthesis/src/classic_psola.cpp
- libs/seam-synthesis/src/spectral_classic.cpp
- libs/seam-synthesis/src/stretch_renderer.cpp
- libs/seam-synthesis/src/seam_composer.cpp

Implementation:
- Frame-level voicing/F0 confidence
- Pitch treatment of voiced transitions/releases
- Independent duration mapping and appropriate resampling
- Trial WORLD behind build option only if baseline comparison justifies it

#### 4b. V08 -- Persist style and improve unit selection

Target files:
- libs/seam-synthesis/src/unit_selection.cpp
- libs/seam-voicebank/src/style_resolution.cpp
- libs/seam-synthesis/src/renderer_capabilities.cpp

Implementation:
- Style-aware selection and blending interface
- Two reviewed aligned styles with supported paired-style blend
- Missing-pair refusal, undo/reload/export and cache identity

#### 4c. V09 -- Make expression usable in the editor

Target files: native UI expression lane modules, editor inspection models

Implementation:
- All expression lanes visible and editable
- Unequal-note duplication, tempo/meter editing, lyric distribution
- Full-text access and keyboard operation
- Overlap selection and narrow-layout behavior

#### 4d. V10 -- Complete pronunciation and lyric productivity

Target files:
- libs/seam-phonemizer/src/pronunciation_resolver.cpp
- Three language phonemizers
- tests/test_english_phonemizer.cpp, test_korean_phonemizer.cpp, test_phonemizer.cpp

Implementation:
- Language-specific dictionaries/rules
- Editable pronunciation per language
- Dictionary as versioned resource with identity entering pronunciation and cache

#### 4e. V11 -- Complete score exchange and DAW authoring

Target files: interchange service, CLAP/VST3/AU editor state, host timing

Implementation:
- Safe MIDI and USTX import/export with tempo/PPQ fidelity
- Explicit conversion losses and hostile-input rejection
- Round-trip tests for all nine host tuples
- Per-note targeting, correct pan/vibrato/timbre, pedal/panic

Phase 4 duration estimate: 60-100 hours engineering

### Phase 5: Modern Singer and Character (V12, V13, V17)

Goal: Qualified neural resources, automatic performance, advanced expressions, character presentation.

#### 5a. V12 -- Deliver neural phrase synthesis and a qualified model

This is the largest single work package. Two internal phases:

Phase A -- Prepare and train:
- Appropriate corpus and labels (language-specific, rights-cleared)
- Compatible vocoder (validated reconstruction quality from U3.3)
- Complete candidate bundle
- Learned-model song inference

Phase B -- Integrate and qualify:
- Replace arithmetic fixtures in tests/test_neural_production_render.cpp with actual learned candidate
- Use qualify-candidate for measured dossier
- Verify rendering on both macOS arm64 and Windows x64

Target files:
- tools/voice_model_training/ (training pipeline)
- libs/seam-neural-synthesis/src/graph_contract.cpp
- libs/seam-neural-synthesis/include/seam/neural_synthesis/worker_protocol.hpp
- apps/seam-neural-worker/main.cpp
- libs/seam-rendering/src/render_snapshot.cpp

Blockers: Rights-cleared corpus, compatible vocoder, sustained training infrastructure.

#### 5b. V13 -- Automatic performance and advanced timbre

- Trained duration/pitch/variance predictor
- Breathiness, power/tension, voicing, growl, formant control, style mapped to model/DSP
- Generated versus manual curves display
- Partial regeneration without destroying edits
- Alternate-take comparison and harmony generation

#### 5c. V17 -- Complete character and performance presentation

- Singer/style/status and mouth/performance states from selected resource
- Handle seek, loop, render replacement, missing assets, reduced motion
- Audio threads independent of texture/animation work

Phase 5 duration estimate: 100-160 hours engineering + external dependencies

### Phase 6: Product Release (V01, V14, V18)

Goal: Quality baseline, platform verification, release audit.

#### 6a. V01 -- Establish reproducible auditory baseline

Create:
- tests/singing_quality/ and tools/singing_quality/
- Corpus manifests, measurements, result collection
- Small dry-vocal comparison packet and one saved 30-60 second song

#### 6b. V14 -- Complete a release around finished songs

- U60/support work
- Signed/installed platform builds
- Host matrix verification (all 9 host tuples)
- Save/recovery, installer, bank-trust, accessibility, update

#### 6c. V18 -- Enforce full-scope Beta GO

Target files:
- docs/product/full-product-beta-contract.json (already exists, 20 reqs, 83 cases)
- tools/external_beta/full_product_gate.py

Implementation:
- Register mandatory EB-009-full-product
- Define typed R1-R20 evidence
- Hash complete capability contract into release identity
- Reproduce audit at every promotion/resume/closure entry point

Phase 6 duration estimate: 40-80 hours engineering + platform access

## 5. External Dependencies and Human Gates

These cannot be completed by engineering alone:

| Gate | Required input | Provider |
|---|---|---|
| U1.4 creator observation | One unaided person session | Project owner |
| U2.1 listening result | Japanese listener for triage; musician for phrasing | Owner + recruited listener |
| U3.5 learned singer qualification | Rights-cleared learned singer, compatible vocoder | Owner + external |
| U4.1 authorized recording | Recording with permissions | Owner + external |
| U4.2b native-speaker review | Per-language review | Owner + external |
| U4.3 style pair | Two compatible aligned styles | Follows from U4.1 |
| U5.1 Windows host matrix | Windows x64 + REAPER/Bitwig | Owner |
| M6 five independent creators | Five participants per canonical protocol | Owner + external |

## 6. Sequencing and Parallelism

Three work lanes can proceed in parallel after shared timing/expression contracts (V02-V05) settle:

1. Synthesis lane: V06, V08 (classical DSP improvement)
2. Editor/DAW lane: V09, V10, V11 (UI and exchange)
3. Voice production/evaluation lane: V07, V15, V16 (bank production and voice design)

Release reliability work (V14) continues in bounded slices throughout.
V12/V13 (neural) can begin dataset/training work in parallel with classical improvements.
V01 (baseline) should start early to provide comparison measurements.
V18 (gate) contract definition should happen early; final acceptance is late.

## 7. Time Estimates (24/7 AI agent work)

| Phase | Engineering hours | Calendar time (24/7) |
|---|---|---|
| Phase 1: CI Green + Merge | 2-4h | ~4 hours |
| Phase 2: Score Correctness (V02-V05) | 40-60h | 2-3 days |
| Phase 3: Voice Construction (V07, V15, V16) | 80-120h | 3-5 days |
| Phase 4: Classical Instrument (V06, V08-V11) | 60-100h | 3-4 days |
| Phase 5: Neural + Character (V12, V13, V17) | 100-160h | 4-7 days |
| Phase 6: Release (V01, V14, V18) | 40-80h | 2-3 days |
| Total engineering | 320-520h | 14-22 days |

Important caveats:
- These estimates cover only the engineering-automatable portion (~50% of the full scope).
- Human gates (listening review, creator sessions, authorized recordings, native-speaker review,
  Windows host matrix, independent creator cohort) add calendar time that cannot be compressed.
- Neural model training (V12) may require multiple training/evaluation cycles, each taking days
  to weeks depending on dataset size and available compute.
- The realistic calendar time to full Beta GO including human gates is 2-4 months minimum.

## 8. Risk Register

| Risk | Impact | Mitigation |
|---|---|---|
| GCC -O3 stall is deeper than wrappedPhaseDifference | CI stays red; merge blocked | Add fine-grained instrumentation; bisect the rendering path |
| No rights-cleared vocal corpus for neural training | V12 blocked indefinitely | Start with procedural-generated teacher data; evaluate TTS corpora |
| Procedural voice sounds robotic/unusable | R3 not met | Iterative improvement with listening feedback; consider hybrid approach |
| Windows host matrix hardware unavailable | V14/M5 blocked | Cloud Windows CI for automated tests; owner provides installed-host evidence |
| Style blend produces artifacts | R6 partial | Research phase-aware interpolation; multiple algorithm candidates |
| No Japanese/Korean native speaker reviewers | R7 language claims blocked | Recruit reviewers early; begin dictionary expansion without blocking claims |

## 9. Verification Strategy

Each phase produces its own verification evidence:

- Phase 1: CI all-green screenshot + master HEAD SHA match
- Phase 2: Timing/pitch/expression test suites pass; migration tests preserve old data
- Phase 3: Voice design to bank production to singing workflow end-to-end test
- Phase 4: DSP quality measurements; host matrix CLAP/VST3/AU pass per platform
- Phase 5: Neural singer renders held-out song; character animation syncs with audio
- Phase 6: Full full-product-beta-contract.json evaluator reports all 20 requirements met

## 10. Files for Developer Onboarding

A new developer should read, in order:

1. DEVELOPER_READING_GUIDE_2026-09-15.md -- project structure and conventions
2. VIRTUAL_SINGER_FEASIBILITY_AND_CODE_ROADMAP_2026-09-05.md -- full scope definition (R1-R20, V01-V18)
3. SEAM_JOINT_DEVELOPMENT_PLAN_R4_2026-09-16.md -- agreed execution order with code-level detail
4. This document -- current status and execution plan
5. docs/product/full-product-beta-contract.json -- machine-enforced release gate (20 reqs, 83 cases)

Key task sessions:
- Task 01a02275 -- primary implementation thread (CI fixes, R4 units, production readiness)
- Task 01a0a066 -- second developer review thread (independent assessment, joint R4 agreement)

## 11. Overall Progress: ~30%

Breaking this down honestly:

- Engineering completion (automatable): ~50% of specification weight
- Human gates completion: ~0%
- Combined weighted progress toward Beta GO: ~25-30%

The 50% engineering figure means half of the code/test work is done. But the product cannot ship
without the human gates (listening results, creator sessions, authorized recordings, platform evidence,
independent creator cohort), which represent the other half of what Beta GO requires. None of those
gates have been started.

The second developer review correctly identified the central risk: "We have reduced implementation
uncertainty far faster than musical uncertainty." The code is self-consistent, deterministic, bounded,
and recoverable. Nobody has heard whether the voice sounds good.
