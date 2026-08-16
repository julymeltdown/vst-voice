# Module Boundaries

## Dependency map

| Module | May depend on |
|---|---|
| `seam_core` | C++ standard library |
| `seam_domain` | `seam_core` |
| `seam_phonemizer` | `seam_domain`, `seam_core` |
| `seam_application` | `seam_domain`, `seam_core` |
| `seam_formats` | `seam_domain`, `seam_core` |
| `seam_voicebank` | `seam_formats`, `seam_domain`, `seam_core` |
| `seam_synthesis` | `seam_voicebank`, `seam_phonemizer`, `seam_formats`, `seam_domain`, `seam_core` |
| `seam_rendering` | `seam_synthesis`, `seam_voicebank`, `seam_phonemizer`, `seam_formats`, `seam_domain`, `seam_core`, C++ threads |
| `seam_editor_ui` | `seam_application`, `seam_phonemizer`, `seam_domain`, `seam_core` |
| `seam_platform` | `seam_core`, narrowly defined application/audio ports later |
| applications | required modules through public interfaces |

## Forbidden dependencies from domain

- iPlug2;
- Skia;
- JSON/CBOR libraries;
- SQLite;
- CLAP/VST3/AU SDKs;
- operating-system window handles;
- audio device APIs;
- paths used as domain identity;
- voicebank file parsing;
- renderer implementation classes;
- background worker or cache implementation types.

## Canonical state after Phase 3

- project identity and settings;
- tempo and meter events;
- tracks and regions;
- notes and lyrics;
- voicebank and character references;
- explicit phoneme overrides and lock state;
- explicit unit-selection overrides and renderer choice;
- per-boundary seam overrides;
- region pitch-automation points.

## Derived state

- phonemizer output;
- candidate and selected automatic unit plans;
- timing plans;
- phrase segments;
- immutable render snapshots;
- waveform/spectrogram tiles;
- rendered PCM;
- viewport and selection geometry;
- command history;
- disk and memory caches;
- scheduler queues and stale-audio publication state.

Derived data can be invalidated and recomputed. It cannot become the sole source of user intent.

## Real-time rule

`seam_rendering` may prepare and move PCM on worker/feeder threads. The future device callback may consume only preallocated buffers and atomics. It must never depend directly on the project, phonemizer, voicebank parser, cache filesystem, or renderer.
