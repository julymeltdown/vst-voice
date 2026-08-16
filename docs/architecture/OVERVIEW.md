# Architecture Overview

## Architectural style

Project SEAM is a local-first modular monolith. Score state, reversible editing, sample selection, phrase rendering, and playback require deterministic in-process coordination. Network services would add latency and failure modes without product value.

## Dependency graph

```text
seam_core
   │
   ├── seam_domain
   │      ├── seam_application
   │      ├── seam_phonemizer
   │      └── seam_formats
   │              │
   │              └── seam_voicebank
   │                       │
   │                       └── seam_synthesis
   │
   ├── seam_editor_ui
   └── seam_platform

apps
├── seam_phase1_demo
├── seam_phase2_demo
└── seam_voicebank_cli
```

The actual CMake graph adds only the narrow public dependencies listed in `MODULE_BOUNDARIES.md`.

## State flow

```text
Native/editor input
→ application command
→ canonical domain validation
→ project revision
→ regenerate phonemes as needed
→ regenerate deterministic unit and timing plans
→ render units
→ compose explicit seams
→ cache or export PCM
```

## Current implementation layers

### `seam_core`

Errors, results, typed IDs, and logging.

### `seam_domain`

Tempo, meter, project, notes, lyrics, phoneme keys, and user overrides. It has no serialization, graphics, or DSP dependency.

### `seam_application`

Reversible commands, editor sessions, selection, and ID-safe project construction.

### `seam_phonemizer`

Deterministic language front end. The Phase 2 implementation supports Japanese Kana and applies canonical user overrides.

### `seam_voicebank`

Data-only bank model, manifest codec, WAV I/O, marker editing, waveform, spectrogram, F0 analysis, and validation.

### `seam_synthesis`

Unit candidates, deterministic complete-cover selection, note-on/vowel-onset timing, raw sample looping, seam composition, and phrase rendering.

### `seam_editor_ui`

Backend-independent piano-roll and phoneme-lane geometry plus text-composition state. SVG proof views remain the current visual adapter.

### `seam_platform`

Real-time callback contracts. A production device/window adapter is still dependency-gated.

## Real-time boundary

The future callback may read only preallocated PCM buffers and atomics. It may not:

- allocate or free memory;
- parse files;
- traverse project state;
- phonemize;
- select units;
- load WAV files;
- run FFT or F0 analysis;
- wait on locks;
- write logs.

Phase 2 renders offline and exports evidence. Background scheduling and callback feeding are later phases.
