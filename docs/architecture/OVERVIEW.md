# Architecture Overview

## Architectural style

Project SEAM is a local-first modular monolith. Score state, reversible editing, sample selection, phrase rendering, caching, and playback handoff require deterministic in-process coordination. Network services would add latency and failure modes without product value.

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
   │                                │
   │                                └── seam_rendering
   │
   ├── seam_editor_ui
   └── seam_platform

apps
├── seam_phase1_demo
├── seam_phase2_demo
├── seam_phase3_demo
└── seam_voicebank_cli
```

The actual CMake graph adds only the narrow public dependencies listed in `MODULE_BOUNDARIES.md`.

## State and render flow

```text
Native/editor input
→ application command
→ canonical domain validation
→ monotonic project revision
→ phrase segmentation and dirty invalidation
→ immutable phrase-scoped snapshot
→ Japanese phonemizer
→ deterministic unit plan
→ vowel-onset timing plan
→ Raw/Classic-PSOLA unit rendering
→ explicit seam composition
→ content-addressed PCM cache
→ stale-while-render publication
→ SPSC playback buffer
→ future real-time audio callback
```

## Current implementation layers

### `seam_core`

Errors, results, typed IDs, stable hashes, and logging.

### `seam_domain`

Tempo, meter, project, notes, lyrics, phoneme keys, unit-selection intent, seam intent, and pitch automation. It has no serialization, graphics, threading, cache, or DSP dependency.

### `seam_application`

Reversible commands, editor sessions, selection, ID-safe project construction, and render-control commands.

### `seam_phonemizer`

Deterministic language front end. The current implementation supports Japanese Kana and applies canonical user overrides.

### `seam_voicebank`

Data-only bank model, manifest codec, WAV I/O, marker and pitch-mark editing, waveform, spectrogram, F0 analysis, and validation.

### `seam_synthesis`

Unit candidates, deterministic complete-cover selection, note-on/vowel-onset timing, Raw and Classic-PSOLA rendering, renderer dispatch/fallback, pitch curves, explicit seam operations, and phrase rendering.

### `seam_rendering`

Phrase segmentation, phrase-scoped content snapshots, full phrase render orchestration, revision-aware priority scheduling, content-addressed PCM storage, stale-audio publication, and an SPSC PCM ring buffer.

### `seam_editor_ui`

Backend-independent piano-roll and phoneme-lane geometry plus text-composition state. SVG proof views remain the current visual adapter.

### `seam_platform`

Real-time callback contracts. A production device/window adapter remains dependency-gated.

## Snapshot and revision separation

A project revision determines whether a completion is current. It is not part of the PCM cache payload. The cache key is derived from phrase-affecting audio content, renderer quality, engine version, voicebank manifest, style, and sample rate. Consequently, a newer project revision can reuse the same PCM when its phrase content did not change.

## Real-time boundary

The future callback may read only preallocated PCM buffers and atomics. It may not:

- allocate or free memory;
- parse files;
- traverse project state;
- phonemize;
- select units;
- load WAV files;
- run FFT, F0 analysis, PSOLA, or seam composition;
- access the filesystem cache;
- wait on locks;
- write logs.

Phase 3 provides the worker-side scheduler and lock-free handoff primitive. Connecting them to a real iPlug2/device callback remains a later integration phase.
