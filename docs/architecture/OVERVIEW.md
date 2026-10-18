# Architecture overview

Regenerated on 2026-10-18 from the configured CMake target graph (`cmake --graphviz` on the `release` configuration, macOS arm64). It replaces the Phase 1–3 overview and `MODULE_BOUNDARIES.md`, whose dependency maps no longer matched the build. When a `target_link_libraries` line changes, regenerate the tables below instead of editing them by hand.

## Style

Project SEAM is a local-first modular monolith: 28 static libraries, two CLAP module libraries and 26 non-test executables, plus 141 test executables, all built from one top-level `CMakeLists.txt` and three subdirectories (`phase12c/`, `phase13a/`, `phase13b/`). Score state, reversible editing, unit selection, phrase rendering, caching and playback handoff need deterministic in-process coordination; nothing here is a network service. The library graph is acyclic, and `cmake/NeuralDependencyDirection.cmake` checks that rendering-independent neural preparation never links upward.

## Layers

A library's layer is one more than the deepest library it links. Arrows in the next table list direct links after transitive reduction, so an edge already implied through another dependency is omitted.

| Layer | Libraries |
|---|---|
| 0 | `seam_core` |
| 1 | `seam_domain`, `seam_text`, `seam_clap` |
| 2 | `seam_formats`, `seam_phonemizer` |
| 3 | `seam_application`, `seam_voicebank`, `seam_character`, `seam_neural_worker_protocol` |
| 4 | `seam_synthesis`, `seam_platform`, `seam_interchange`, `seam_live_voice` |
| 5 | `seam_editor_ui`, `seam_voice_design` |
| 6 | `seam_distribution`, `seam_voicebank_production`, `seam_editor_inspection` |
| 7 | `seam_neural_synthesis`, `seam_candidate_packaging` |
| 8 | `seam_rendering` |
| 9 | `seam_authoring_runtime`, `seam_playback_platform` |
| 10 | `seam_native_ui` |
| 11 | `seam_standalone_authoring`, `seam_voicebank_studio_app`, `seam_clap_editor` |

| Library | Owns | Direct links (reduced) |
|---|---|---|
| `seam_core` | `Result`/`Error`, typed IDs, logging, SHA-256, stable hashes, file I/O and locks, resampling, realtime audit hooks | — |
| `seam_domain` | Project, tracks, regions, notes, lyrics, tempo/meter, phoneme/unit/seam intent, pitch automation. No I/O, threads, DSP or graphics. | `seam_core` |
| `seam_text` | Unicode handling and trusted system-font text rasterization for the UI | `seam_core` |
| `seam_clap` | CLAP plug-in identity, session and state codec | `seam_core` |
| `seam_formats` | Project JSON codec (writer v21, readers 1–21) and performance JSON | `seam_domain` |
| `seam_phonemizer` | Japanese, English and Korean front ends with user overrides | `seam_domain` |
| `seam_application` | Reversible commands, editor sessions, selection | `seam_phonemizer` |
| `seam_voicebank` | Data-only bank model, manifest codec, WAV I/O, markers, pitch marks, F0, validation | `seam_formats`, `seam_phonemizer` |
| `seam_character` | Character package model | `seam_formats` |
| `seam_neural_worker_protocol` | Neural worker request/response codec | `seam_formats` |
| `seam_synthesis` | Unit selection, timing, Raw/PSOLA/spectral renderers, seam composition | `seam_voicebank` |
| `seam_platform` | Audio devices, recording, helper processes, crash capture, OS adapters | `seam_voicebank` (only for `wav.hpp`) |
| `seam_interchange` | USTX and Standard MIDI import/export with explicit losses | `seam_application`, `seam_formats` |
| `seam_live_voice` | Live articulation engine for the CLAP editor | `seam_voicebank` |
| `seam_editor_ui` | Backend-independent piano-roll, lanes, text composition | `seam_application`, `seam_synthesis` |
| `seam_voice_design` | Procedural singer recipes: phonation, frication and plosive sources, vocal tract, articulation and transition plans | `seam_synthesis`, `seam_text` |
| `seam_distribution` | `.seambank` signing, verification, installation, procedural admission | `seam_voice_design` |
| `seam_voicebank_production` | Producer workspace, takes, review, prepared donors, pointer recovery | `seam_voice_design` |
| `seam_editor_inspection` | Unit/sample inspection models | `seam_editor_ui` |
| `seam_neural_synthesis` | Neural deployment resolution and bounded worker launch | `seam_distribution`, `seam_neural_worker_protocol`, `seam_platform` |
| `seam_candidate_packaging` | Candidate bank packaging | `seam_distribution`, `seam_voicebank_production` |
| `seam_rendering` | Phrase snapshots, `ProductionProjectRenderer`, scheduler, PCM cache, stale-audio publication | `seam_neural_synthesis` |
| `seam_authoring_runtime` | `AuthoringSession`, project lifecycle, export service, generation campaigns | `seam_interchange`, `seam_rendering`, `seam_voicebank_production` |
| `seam_playback_platform` | Playback feeder and device binding | `seam_rendering` |
| `seam_native_ui` | SING shell, controllers, CoreGraphics/CoreText paint backend, accessibility tree | `seam_authoring_runtime`, `seam_candidate_packaging`, `seam_character`, `seam_editor_inspection` |
| `seam_standalone_authoring` | Standalone application controller | `seam_native_ui`, `seam_playback_platform` |
| `seam_voicebank_studio_app` | Voicebank Studio application | `seam_native_ui` |
| `seam_clap_editor` | Embedded CLAP editor runtime | `seam_native_ui`, `seam_live_voice` |

Fan-in is highest for `seam_core` (27 libraries), `seam_domain` (17), `seam_formats` (15), `seam_voicebank` (13) and `seam_phonemizer` (11). A header change in any of these rebuilds most of the tree.

## Executables

| Group | Targets |
|---|---|
| Products | `seam_editor_native` (`Project SEAM.app`), `seam_voicebank_studio_native` (`SEAM Voicebank Studio.app`), `seam_clap_plugin`, `seam_clap_editor_plugin` |
| Command-line tools | `seam_voicebank_cli`, `seam_bank_tool`, `seam_clap_state_tool`, `seam_installer_verifier`, `seam_singer_pilot`, `seam_singing_quality_render`, `seam_neural_worker` (optional) |
| Test hosts and harnesses | `seam_clap_host`, `seam_clap_editor_host`, `seam_phase12c_*` (matrix, soak, cancellation storm, live demo; the X11 GUI lifecycle runner and live CLAP host build only on Linux) |
| Fixture producers | `seam_fixture_synthetic_bank` and `seam_fixture_clap_state_wav`; their CTest setup entries are `FIXTURES_SETUP` for the tests that read the synthetic voicebank and the CLAP state WAV |
| Smoke journeys | Nine `seam_smoke_*` programs, each running one pipeline end to end under CTest |
| Benchmark | `seam_phase5_benchmark`, checked by `tests/design/test_phase5_benchmark_contract.py` |

## Data flow for one edit

```text
SING shell input (seam_native_ui)
  -> reversible command (seam_application) -> canonical domain validation (seam_domain)
  -> AuthoringSession revision and dirty-phrase invalidation (seam_authoring_runtime)
  -> phonemizer -> unit plan -> timing plan (seam_phonemizer, seam_synthesis)
  -> immutable phrase snapshot; admitted sample bank / procedural recipe / neural bundle (seam_rendering, seam_distribution)
  -> renderer and explicit seam composition (seam_synthesis, seam_neural_synthesis)
  -> content-addressed PCM cache and stale-while-render publication (seam_rendering)
  -> SPSC playback ring -> device callback (seam_playback_platform, seam_platform)
```

Project revision decides whether a completion is current; it is not part of the PCM cache key. The key covers phrase-affecting content, renderer, engine version, resource identity, style and sample rate, so an unrelated edit can reuse cached PCM.

## Rules that still hold

- `seam_domain` stays free of serialization, filesystem paths as identity, graphics, threads, DSP, plug-in SDKs and OS handles. Derived state (phonemes, unit plans, snapshots, PCM, caches) can be recomputed and never becomes the only record of user intent.
- The audio callback reads only preallocated buffers and atomics. It never allocates, locks, parses files, walks the project, phonemizes, selects units or touches the cache. `seam_realtime_allocation_probe` checks the allocation half of this rule.
- Resource admission happens before snapshot or cache lookup. A cache hit never replaces admission of a changed file-backed selection.

## Known structural costs

These are recorded here because they shape build time and change cost. The [completion plan](../plans/2026-10-18-project-completion-plan.md) decides which ones are worth changing.

1. `seam_platform` links `seam_voicebank` only to reach `seam/voicebank/wav.hpp` from `recording_session`. That lifts the platform library from layer 3 to layer 4 and makes every platform consumer link `seam_voicebank` and `seam_phonemizer`. `seam_formats` and `seam_domain` stay below the platform layer either way: `crash_capture.cpp` uses the JSON codec and the audio devices include `seam/domain/routing.hpp`.
2. `seam_native_ui` (about 55k lines, the largest library) links the authoring runtime, the producer packaging stack and character packages at once, so a UI change relinks most products, and a producer header change recompiles the Studio parts of the UI library: seven of its files include producer or candidate headers directly, `voicebank_studio.hpp` among them.
3. There are 141 separate test executables, each statically linking large parts of the tree, so link time dominates many focused rebuilds.
4. Active code still lives under historical directory names (`phase12c/`, `phase13a/`, `phase13b/`) and the phase-named test executables built from them.
