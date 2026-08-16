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
| `seam_editor_ui` | `seam_application`, `seam_phonemizer`, `seam_domain`, `seam_core` |
| `seam_platform` | `seam_core`, narrowly defined application ports later |
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
- renderer implementation classes.

## Canonical state after Phase 2

- project identity and settings;
- tempo and meter events;
- tracks and regions;
- notes and lyrics;
- voicebank and character references;
- explicit phoneme overrides and lock state.

## Derived state

- phonemizer output;
- candidate and selected unit plans;
- timing plans;
- waveform/spectrogram tiles;
- rendered PCM;
- viewport and selection geometry;
- command history;
- caches.

Derived data can be invalidated and recomputed. It cannot become the sole source of user intent.
