# Project SEAM Phase 2 Implementation Report

**Implementation status:** Complete for the Phase 2 scope documented in this repository, plus an end-to-end raw concatenative synthesis vertical slice  
**Branch policy:** `master` only  
**Repository version:** `0.2.0`  
**Verification date:** 2026-08-16

## 1. Executive summary

Phase 2 extends the Phase 1 score/editor foundation into a working phoneme, voicebank, and raw sample-concatenation pipeline. The repository can now take a small vocal score, convert Japanese kana lyrics into phonemes, map those phonemes to units in a data-only voicebank, align each unit's vowel onset to the target note, transform and loop the recorded unit audio, compose explicit sample seams, and write a real PCM WAV file.

The implementation is not a mocked UI-only prototype. The included Phase 2 demonstration constructs a deterministic synthetic voicebank as test data and executes the same library path exposed by the CLI and unit tests:

```text
Project notes and lyrics
→ Japanese kana phonemizer
→ generated phoneme tokens and explicit user overrides
→ deterministic complete-cover unit selection
→ vowel-onset timing solver
→ per-unit RawLoopRenderer
→ SeamComposer
→ mono PCM WAV
```

The synthetic bank exists to validate the engine and is not represented as a commercial voice. The production native iPlug2/Skia shell, operating-system IME overlays, PSOLA, spectral synthesis, background render scheduling, and an official licensed human-recorded bank remain outside this phase and are explicitly documented as deferred.

## 2. Git and branch state

Only one local branch is present:

```text
master
```

The Phase 2 implementation commit is:

```text
90ec54c feat: implement Phase 2 phoneme and raw synthesis pipeline
```

The repository continues the existing master-only history:

```text
90ec54c feat: implement Phase 2 phoneme and raw synthesis pipeline
9e32bab docs: document Phase 1 architecture and verification
c84050a feat: add Phase 1 low-poly character directions
533eed0 feat: implement Phase 1 editor and domain foundation
6f5f3c1 chore: establish master-only repository and license policy
```

The active Git hooks and verifier reject non-`master` work. Verification output is stored in `docs/phase2/evidence/branch-policy.txt`.

## 3. Implemented functionality

### 3.1 Canonical phoneme edit model

The domain now contains explicit phoneme identity and user-edit state:

- `PhonemeKey`: stable identity formed from a note ID and ordinal;
- `PhonemeToken`: generated symbol, role, timing, voiced flag, and lock state;
- `PhonemeTiming`: signed microsecond offsets relative to note-on;
- `PhonemeOverride`: optional symbol, start, end, and lock overrides.

Generated phonemes are derived state. The project stores only notes, lyrics, and deliberate user overrides. This prevents a phonemizer upgrade from creating two competing sources of truth while preserving manual edits.

Note deletion now removes attached phoneme overrides. Undo restores the note, the lyric when appropriate, and each override at its original position.

### 3.2 Reversible lyric and phoneme commands

The application layer adds command objects for:

- setting or replacing note lyrics;
- inserting or updating a phoneme override;
- removing a phoneme override;
- undoing and redoing all of the above as one editor transaction.

The existing monotonic project revision mechanism remains intact, so downstream render invalidation can later use the same state changes.

### 3.3 Japanese kana phonemizer

`IPhonemizer` defines a backend-independent language front end. The first implementation is a deterministic Japanese kana phonemizer with:

- Hiragana and Katakana normalization;
- base mora conversion;
- contracted mora such as `きゃ`;
- sokuon `っ` emitted as `cl`;
- moraic nasal `ん` emitted as `N`;
- long-vowel/continuation handling;
- punctuation represented as a pause token;
- warnings for unsupported text;
- explicit symbol, timing, and lock overrides.

The implementation does not invoke a model, network service, embedded interpreter, or external dictionary executable.

### 3.4 Backend-independent text composition and phoneme lane

Phase 2 adds an IME composition state model that represents begin, update, commit, and cancel behavior without tying the editor domain to Windows or macOS APIs. This model is covered by tests and is ready for later native text-overlay adapters.

The `PhonemeLaneModel` converts generated phonemes and manual timing into visible lane geometry and supports hit testing. The Phase 2 SVG evidence uses the actual note and phoneme view models rather than a static mockup.

### 3.5 Project JSON schema 2

Project serialization now writes schema 2. Each vocal region contains an explicit `phonemeOverrides` array.

The decoder:

- reads schema 2;
- migrates schema 1 regions to an empty override set;
- rejects unsupported future schema versions;
- preserves UTF-8 lyrics and strong IDs;
- does not persist generated phonemes, selected units, timing plans, or PCM.

The end-to-end demo verifies round-trip project equality.

### 3.6 Data-only voicebank domain

A first version of the voicebank manifest is implemented with:

- voicebank identity, version, display name, language, styles, and expected sample rate;
- unit identity, alias, phone sequence, kind, source path, root MIDI note, style, take, priority, gain, renderer hint, and enabled state;
- acoustic source markers for offset, consonant end, vowel onset, stable region, sustain loop, release, and audio end.

Audio paths must be relative. Absolute paths and parent traversal are rejected. The v1 format is deliberately data-only and cannot embed executable code, scripts, dynamic libraries, shaders, or web content.

### 3.7 Bounded WAV I/O and audio inspection

The first-party RIFF/WAVE implementation supports bounded input for common uncompressed formats:

- PCM 8-bit;
- PCM 16-bit;
- PCM 24-bit;
- PCM 32-bit;
- IEEE float 32-bit.

The reader validates chunk boundaries and frame counts. Multichannel input can be mixed to mono. The writer emits mono PCM16 WAV.

Audio inspection includes:

- peak amplitude;
- RMS;
- DC offset;
- clipping count;
- duration and frame metadata.

### 3.8 Waveform, spectrogram, and pitch analysis

The voicebank foundation now produces inspectable analysis artifacts:

- multi-level waveform peak summaries;
- SVG waveform output;
- a first-party radix-2 FFT spectrogram;
- PGM spectrogram output;
- autocorrelation-based pitch frames;
- voiced/unvoiced confidence;
- octave-error-resistant selection of the first near-global local autocorrelation peak.

These analysis services are library code used by the CLI and tests. They are not yet wrapped by a production Voicebank Studio window.

### 3.9 Acoustic marker editing and validation

The marker editor validates and normalizes source landmarks while preserving monotonic ordering. It handles missing or invalid sustain-loop ranges without leaving a loop outside the source audio.

The bank validator checks:

- manifest structure;
- safe source paths;
- file existence and readable WAV data;
- sample-rate expectations;
- source marker ordering and bounds;
- clipping and DC offset;
- root-pitch plausibility;
- pitch analysis confidence;
- sustain-loop boundary discontinuity;
- duplicate aliases and variants;
- enabled-unit inventory.

Warnings and information remain visible rather than being silently corrected.

### 3.10 Voicebank CLI

`seam_voicebank_cli` provides three commands:

```text
validate MANIFEST [BANK_ROOT]
inspect MANIFEST
analyze WAV OUTPUT_DIRECTORY
```

`validate` returns a structured JSON result and a failing process status when bank errors are present. `inspect` exposes unit inventory. `analyze` writes statistics, pitch frames, a waveform SVG, and a spectrogram.

### 3.11 Deterministic unit selection

The synthesis layer implements:

- exact phone-span candidate generation;
- style and enabled-state filtering;
- pitch-distance scoring;
- preference for longer multi-phone coverage;
- explicit priority and stable take ordering;
- dynamic programming to find a complete sequence cover;
- deterministic tie resolution by coverage and unit ID;
- alternative-unit inventory for the editor.

Random selection is not used. The same score and voicebank produce the same selected unit plan.

### 3.12 Vowel-onset timing solver

Each selected unit is placed so that its transformed vowel onset lands on the owning note's note-on frame:

```text
unit destination start = note-on frame - transformed vowel-onset offset
```

The solver reports rather than conceals timing conflicts, including:

- negative preutterance;
- unit transition longer than the available destination;
- adjacent-unit overlap;
- missing note references.

Adjacent overlap is expected in a sample-concatenative bank and is passed explicitly to the seam stage.

### 3.13 RawLoopRenderer

The implemented raw renderer performs:

- source-to-output sample-rate conversion;
- target-MIDI pitch conversion through deterministic source-position resampling;
- preservation of the transition segment;
- stable-vowel looping;
- controllable loop-boundary print-through;
- release placement;
- unit and request gain;
- DC removal;
- one-millisecond edge de-click;
- finite-value safeguards.

It deliberately does not perform phrase-wide naturalization, neural reconstruction, spectral-envelope matching, or automatic source-F0 flattening. The source sample's residual variation remains audible by construction. A separately controllable source-residual curve belongs to the future PSOLA/spectral renderers, not this raw implementation.

### 3.14 SeamComposer

The seam stage exposes the join instead of treating it as an internal implementation detail. A normalized seam amount interpolates between:

- a smooth cubic overlap;
- a hard midpoint handoff.

The composer does not move unit boundaries or optimize spectral similarity. It retains minimal edge fading to remove digital impulses without erasing the intended timbral join.

### 3.15 End-to-end demonstration

`seam_phase2_demo` executes the full portable vertical slice. It:

1. creates a five-unit synthetic test bank;
2. writes real WAV unit files and a manifest;
3. creates four notes with Japanese lyrics;
4. adds a locked manual phoneme timing override;
5. phonemizes the region;
6. selects a complete deterministic unit plan;
7. solves vowel-onset timing;
8. renders each unit;
9. composes the seams;
10. writes a mono 48 kHz PCM WAV;
11. writes waveform and spectrogram artifacts;
12. validates the bank;
13. saves and reloads the schema 2 project;
14. renders an editor evidence SVG from the real view-model data.

The generated audio is test synthesis, not prerecorded final phrase audio.

## 4. Demonstration result

The committed evidence reports:

```json
{
  "notes": 4,
  "phonemes": 7,
  "selectedUnits": 4,
  "audioFrames": 78405,
  "sampleRate": 48000,
  "durationSeconds": 1.6334375,
  "projectSchema": 2,
  "voicebankSchema": 1,
  "projectRoundTripEqual": true,
  "bankErrors": 0,
  "bankWarnings": 1,
  "timingIssues": 3
}
```

The single bank warning is intentional: one synthetic sustain loop contains a visible boundary discontinuity so the validator, evidence UI, and warning policy are exercised. Duplicate alias information demonstrates that multiple variants are retained. The three timing issues are explicit adjacent-unit overlaps introduced by preutterance; they are passed to the seam engine and are not fatal bank errors.

Audio statistics:

```text
Channels             1
Sample width         16-bit PCM
Sample rate          48,000 Hz
Frames               78,405
Duration             1.6334375 s
Peak                  0.252464
RMS                   0.127240
DC offset             0.000002056
```

## 5. Verification

### 5.1 Unit and integration cases

The directly executed test binary reports:

```text
33 passed, 0 failed
```

Coverage includes:

- tempo, meter, quantization, and sample conversion;
- command undo/redo and strong-ID reservation;
- project schema migration and UTF-8 round trip;
- 10,000-note viewport virtualization;
- text composition and phoneme-lane behavior;
- Japanese phonemization and overrides;
- WAV I/O, waveform, FFT spectrogram, pitch analysis, and markers;
- manifest persistence and bank validation;
- deterministic unit selection;
- raw unit rendering;
- seam composition;
- end-to-end phrase rendering.

### 5.2 CTest matrices

Each preset runs four CTest entries:

```text
seam_tests
seam_phase2_demo_smoke
seam_voicebank_cli_help
seam_voicebank_cli_validate
```

Verified results:

| Build | Compiler policy | Result |
|---|---|---:|
| Debug/dev | warnings as errors | 4/4 passed |
| Release | warnings as errors | 4/4 passed |
| AddressSanitizer + UndefinedBehaviorSanitizer | warnings as errors | 4/4 passed |

The sanitizer run completed without reported memory or undefined-behavior findings.

### 5.3 Performance evidence

The current container benchmark is evidence for regression comparison, not a universal hardware guarantee:

```text
Raw renderer
- 200 iterations
- 4,800,000 rendered samples
- 395.298 ms
- 252.974× real-time multiple

Seam composer
- 200 iterations
- 15,600,000 composed samples
- 1,503.616 ms

Pitch analyzer
- 5 iterations
- 430 pitch frames
- 2,579.288 ms
```

The pitch analyzer is an offline voicebank-analysis component, not an audio-callback operation.

### 5.4 Policy and repository checks

```text
Master-only branch verifier     PASS
License allowlist audit         PASS
Git object integrity (`fsck`)   PASS
Git whitespace check            PASS
```

No production third-party source is currently vendored. The license audit therefore reports zero distributed dependencies.

## 6. Main source modules

```text
seam_domain
  Phoneme identity, timing, overrides, and canonical project state

seam_phonemizer
  IPhonemizer and deterministic Japanese kana implementation

seam_application
  Lyric and phoneme commands plus existing editor session

seam_editor_ui
  Text composition and Phoneme Lane view models; SVG proof renderer

seam_formats
  Project JSON schema 2 and migration support

seam_voicebank
  Manifest, WAV, waveform, FFT spectrogram, pitch, markers, validation

seam_synthesis
  Candidate generation, deterministic selection, timing, raw rendering, seams

seam_phase2_demo
  End-to-end executable and visual/audio evidence producer

seam_voicebank_cli
  Validation, inspection, and offline WAV analysis
```

A complete repository file inventory is generated at `docs/phase2/FILE_TREE.txt`.

## 7. Evidence files

The reproducible evidence bundle is under `docs/phase2/evidence/`:

```text
phase2-editor.png
phase2-editor.svg
phase2-raw-phrase.wav
phase2-raw-waveform.svg
phase2-raw-spectrogram.png
phase2-summary.json
phase2-demo.seam.json
synthetic-voicebank-manifest.json
voicebank-validation.json
voicebank-inspection.json
cli-analysis-analysis.json
phase2-benchmark.json
test-output.txt
ctest-dev.txt
ctest-release.txt
ctest-sanitize.txt
branch-policy.txt
license-audit.txt
verification-matrix.json
```

The evidence can be regenerated with:

```bash
python3 scripts/generate_phase2_evidence.py --root .
```

## 8. Explicitly incomplete work

The following items are not represented as complete:

- production iPlug2 + Skia native desktop window;
- approved/vendored native graphics and application dependencies;
- Windows TSF and macOS native text-input overlay adapters;
- a production Voicebank Studio GUI;
- microphone recording transport;
- editable PSOLA pitch marks and PSOLA renderer;
- SpectralClassic renderer;
- per-unit production renderer dispatcher and stretch fallback;
- persistent manual unit-selection commands;
- background render scheduler, cancellation, dirty-phrase invalidation, and PCM cache;
- signed `.seambank` packaging;
- an official licensed human-recorded commercial voicebank;
- CLAP, VST3, and AU targets.

The generated editor image is an SVG proof produced from real model data, not a native-window screenshot. The generated WAV is genuine raw concatenative output from synthetic unit WAVs.

## 9. Recommended next implementation phase

The next phase should prioritize a production editing loop rather than adding more file formats:

1. persistent explicit unit-selection and per-boundary seam overrides;
2. phrase segmentation and immutable render snapshots;
3. cancellation-aware background render scheduler;
4. content-addressed PCM cache and stale-while-render playback;
5. ClassicPSOLA with editable pitch marks;
6. native Voicebank Studio screens around the existing analysis services;
7. exact iPlug2/Skia revision audit, followed by native shell integration;
8. Windows/macOS IME overlay adapters;
9. licensed test recordings that replace the synthetic bank for listening calibration.

This order preserves the product's core principle: note, phoneme, sample unit, and seam must remain visible and independently editable throughout the production playback path.
