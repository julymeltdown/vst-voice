# Project SEAM — Phase 12A production plug-in render integration

Project SEAM is a C++20 sample-concatenative singing-voice editor. Phoneme boundaries, source-unit changes, pitch-shift artifacts, and sample seams are editable musical material rather than defects that must always be hidden.

This repository contains:

- Phase 1 editor/domain foundation;
- Phase 2 phoneme, voicebank, and Raw synthesis vertical slice;
- Phase 3 editable PSOLA and background phrase render loop;
- Phase 4 multi-renderer synthesis, Unit/Sample inspection, callback-ready playback, and bounded cache;
- Phase 4.1 correctness, durability, security, and concurrency stabilization;
- Phase 5 native standalone/runtime vertical slice;
- completed **Phase 5.1 native product surfaces, Character 01 integration, graphical Voicebank Studio, and recording transport**;
- completed **Phase 6 persisted multichannel project routing and callback delivery for 1–8 output channels**;
- completed **Phase 7 signed, verifiable, and transactional `.seambank` packaging and installation**;
- integrated **Phase 8 Windows Win32/TSF/WASAPI and macOS AppKit/NSTextInputClient/CoreAudio platform adapters**;
- completed **Phase 9 trusted system-font Unicode/CJK rasterization for Korean, Japanese, Chinese, and Latin native UI text**;
- completed **Phase 10 loadable CLAP 1.2.10 render-player module with bounded multichannel state, host transport, and sample-accurate Master Gain automation**;
- implemented **Phase 11 CLAP GUI Feature Alpha** with DAW-embedded Piano Roll, technical Phoneme/Unit/Pitch lanes, direct Note/Lyric/Seam editing, asynchronous preview, and live human-sample note input;
- completed **Phase 12A production preview integration** with exact Voicebank ID/version/content-hash resolution, trust-aware installed receipts, relink/select APIs, shared four-renderer Phrase rendering, and PCM parity tests.

Development uses the **`master` branch only**.

> **Current maturity: Feature Alpha, not Release Candidate.** Phase 12A now routes CLAP preview rendering through the production Voicebank/Phonemizer/Unit/Timing/four-Renderer/Seam/Cache pipeline and resolves exact trusted Voicebank identities. Phoneme, Unit and Pitch lanes are still not fully direct-manipulation editors, and host timeline/routing plus target-platform release gates remain. See [`docs/STATUS_KO.md`](docs/STATUS_KO.md), [`docs/REMAINING_TASKS_KO.md`](docs/REMAINING_TASKS_KO.md), and [`docs/RELEASE_READINESS_KO.md`](docs/RELEASE_READINESS_KO.md).



## Phase 12A implementation

- `VoicebankCatalog` discovers standard installed roots, explicit relink roots, bundle/sidecar resources and development fixtures.
- Each track persists exact Voicebank ID, version and synthesis content SHA-256.
- Signed installation receipts distinguish trusted installed banks from untrusted or development candidates.
- Missing/version/hash/trust failures publish silence and explicit diagnostics; no other bank is silently substituted.
- `ProductionRegionRenderer` is shared by direct engine rendering and CLAP async preview, with sample-identical parity tests and Phrase cache reuse.
- The public-domain production bank remains a nonofficial technical fixture.

See [`docs/phase12a/`](docs/phase12a/) for acceptance, architecture and known limitations.

## Phase 11 implementation

### Embedded CLAP authoring surface

- `ProjectSEAMEditor.clap` exposes CLAP GUI, note-input, state, audio-port and timer-support extensions.
- The child view embeds the existing Project SEAM controller and renders Piano Roll, Lyrics, Phoneme, Unit, Seam, Pitch and optional Character 01 surfaces. Note, lyric and seam amount are directly editable; complete direct editing for phoneme timing, unit selection/renderer and pitch points remains.
- Edits submit immutable Project copies and an exact resolved Voicebank candidate to a cancellable worker. Phase 12A calls the shared production PhraseRenderPipeline and content-addressed Phrase cache; completed PCM is published through a bounded reader-counted triple buffer. The audio thread performs no project parsing, Voicebank resolution, file access or render work.
- A 16-voice live sampler handles sample-accurate CLAP note-on/off events using the public-domain human technical fixture.

### Rights and release boundary

- `assets/demo-human-voicebank-public-domain` records the upstream public-domain statement, original and derived hashes, retrieval date and processing. It explicitly declares `official=false` and `contractedSinger=false`.
- Official Voicebank 01 remains blocked by the real performer-contract and directed-recording acceptance gates under `docs/legal` and `docs/voicebank`.
- macOS bundle, signing/notarization, PKG, Windows packaging/signing, official CLAP validation and VST3/AU wrapper pipelines are source-integrated. They are not labelled signed, notarized or DAW-certified until target jobs produce that evidence.

See [`docs/phase11/IMPLEMENTATION_REPORT.md`](docs/phase11/IMPLEMENTATION_REPORT.md), [`docs/phase11/ACCEPTANCE.md`](docs/phase11/ACCEPTANCE.md), and [`docs/phase11/HOST_CERTIFICATION_MATRIX.md`](docs/phase11/HOST_CERTIFICATION_MATRIX.md).


## Phase 10 implementation

### Real loadable CLAP module

- `ProjectSEAM.clap` exports `clap_entry` and a stable one-plugin factory.
- The plug-in has no audio input and one main output carrying one through eight channels.
- A bounded `SEAMCLP1` state stores a pre-rendered Project SEAM multichannel result and Master Gain.
- Host seconds transport, beat/tempo fallback, pause silence, and free-running operation are implemented.
- Master Gain (`-60..+6 dB`) is sample-accurately automatable and persisted in state.
- Audio processing performs no project parsing, voicebank access, filesystem I/O, resampling, allocation, or character work.
- `seam_clap_state_tool` packs an existing one-to-eight-channel WAV export into `SEAMCLP1`, inspects it, and extracts it back to WAV.

### Dynamic ABI verification

`seam_clap_host` loads the built module through the operating-system dynamic loader, scans its descriptor and extensions, feeds deliberately partial state streams, activates four-channel output, verifies sample-offset automation and stopped-transport silence, saves state, and performs complete teardown.

### Accurate limitation

Phase 10 is a headless **render player**, not the complete Piano Roll editor inside a DAW. Authoring and sample-concatenative synthesis remain in the standalone editor. CLAP GUI embedding, asynchronous host-side rendering, live note-event synthesis, VST3, AU, and broad third-party-host certification are later phases. Character 01 remains optional product identity and does not enter plug-in PCM or state.

See [`docs/phase10/IMPLEMENTATION_REPORT.md`](docs/phase10/IMPLEMENTATION_REPORT.md), [`docs/phase10/ACCEPTANCE.md`](docs/phase10/ACCEPTANCE.md), [`docs/architecture/CLAP_PLUGIN_RUNTIME.md`](docs/architecture/CLAP_PLUGIN_RUNTIME.md), and [`docs/formats/CLAP_STATE_V1.md`](docs/formats/CLAP_STATE_V1.md).


## Phase 9 implementation

### Native Korean, Japanese, and Chinese display

- New first-party `seam_text` module with strict UTF-8 scalar validation.
- Trusted system TTF/TTC discovery with locale-aware CJK face selection and
  fallback.
- Antialiased glyph rasterization, same-face kerning, text metrics, wrapping,
  ellipsis, and combining-mark placement.
- Bounded whole-text cache: 512 entries and 32 MiB.
- `RasterCanvas` uses the Unicode engine on X11, Win32, and AppKit while keeping
  the deterministic ASCII bitmap renderer as a non-fatal fallback.
- Native Japanese lyric labels now render as glyphs instead of UTF-8 byte
  placeholders.

### Font trust and licensing boundary

- Project SEAM redistributes no font files.
- Projects, voicebanks, character packages, and `.seambank` archives cannot
  provide fonts or font paths.
- Only known operating-system locations or explicit trusted absolute files are
  parsed.
- The pinned `stb_truetype` single header is the only new distributed source
  dependency; its exact revision, SHA-256, MIT notice, and SPDX entry are
  recorded.

### Accurate limitation

Phase 9 targets CJK and Latin product UI. It does not claim full Arabic, Indic,
Southeast Asian, or arbitrary OpenType shaping. The audited iPlug2 + Skia
production adapter remains a separate later phase.

See [`docs/phase9/IMPLEMENTATION_REPORT.md`](docs/phase9/IMPLEMENTATION_REPORT.md),
[`docs/phase9/ACCEPTANCE.md`](docs/phase9/ACCEPTANCE.md), and
[`docs/architecture/UNICODE_TEXT_RENDERING.md`](docs/architecture/UNICODE_TEXT_RENDERING.md).


## Phase 8 implementation

### Windows native adapters

- Real Win32 native window using the existing first-party software-raster editor scene.
- Per-monitor DPI, resize, repaint, pointer, wheel, keyboard, close, and screenshot paths.
- Unicode lyric editing through a native `EDIT` overlay participating in TSF, with explicit `ITfThreadMgr` activation.
- Shared-mode event-driven WASAPI output for one through eight float channels.
- Shared-mode event-driven WASAPI mono capture for Voicebank Studio recording.

The text adapter is accurately described as a **native `EDIT` overlay under TSF text services**. It is not claimed to be a custom `ITextStoreACP` implementation.

### macOS native adapters

- Real AppKit `NSWindow` and custom `NSView` using the same first-party software-raster editor scene.
- Backing-scale-aware CoreGraphics presentation, pointer, wheel, keyboard, resize, close, and screenshot paths.
- `NSTextInputClient` marked-text, selection, commit, cancel, deletion, cursor movement, and candidate-window positioning.
- CoreAudio DefaultOutput AudioUnit output for one through eight non-interleaved float channels when accepted by the selected device.
- CoreAudio HALOutput mono capture for Voicebank Studio recording.

### Platform verification boundary

- Linux X11/XIM/PulseAudio remains runtime-verified in the current environment.
- Windows and macOS implementations are selected by CMake, covered by a static source-contract test, and included in a three-platform GitHub Actions build/test matrix.
- This Linux package does not claim physical Windows/macOS speaker, microphone, DPI, focus, lifecycle, or IME runtime certification. Those tests remain platform release gates.

### Character 01 across native platforms

Character 01 continues to use the shared product-presentation layer. Windows and macOS do not duplicate character policy. Full/Minimal/Off display state, voicebank cards, welcome surfaces, and status portraits remain optional UI surfaces; Character 01 never enters synthesis, multichannel routing, recording, cache identity, `.seambank` trust decisions, or exported PCM.

See [`docs/phase8/IMPLEMENTATION_REPORT.md`](docs/phase8/IMPLEMENTATION_REPORT.md), [`docs/phase8/PLATFORM_MATRIX.md`](docs/phase8/PLATFORM_MATRIX.md), and [`docs/architecture/NATIVE_PLATFORM_ADAPTERS.md`](docs/architecture/NATIVE_PLATFORM_ADAPTERS.md).


## Phase 7 implementation

### Signed data-only voicebank packages

- `.seambank` v1 stores a canonical entry table, contiguous payloads, an embedded Ed25519 public key, and an Ed25519 signature.
- The signature binds the header, table, payload, and embedded public key through a SHA-256 package digest.
- Every asset has an independent SHA-256 checksum.
- Packages reject path traversal, hidden paths, symbolic links, scripts, executables, duplicate entries, overlapping extents, and configured resource-limit violations.
- `manifest.json` and all audio assets referenced by the voicebank manifest are verified.

### Explicit trust and transactional installation

- A cryptographically valid package is not automatically trusted. Installation requires an independently supplied trusted public key.
- Installation extracts into a staging directory, validates the installed manifest, writes a receipt, rechecks the package digest, and atomically publishes `<root>/<voicebank-id>/<version>`.
- Existing versions are preserved unless `--replace` is explicitly supplied.
- Private signing keys are never included in a package or repository.

### Distribution tools

```text
seam_bank_tool keygen
seam_bank_tool pack
seam_bank_tool verify
seam_bank_tool list
seam_bank_tool install
seam_phase7_demo
```

See [`docs/formats/SEAMBANK_V1.md`](docs/formats/SEAMBANK_V1.md), [`docs/phase7/SIGNING_AND_INSTALLATION.md`](docs/phase7/SIGNING_AND_INSTALLATION.md), and [`docs/phase7/ACCEPTANCE.md`](docs/phase7/ACCEPTANCE.md).


## Phase 6 implementation

### Persisted project routing graph

- Project JSON schema 4 persists buses, sends, track-output matrices, a master bus, and device-output routes.
- Schemas 1–3 migrate to the former stereo behavior.
- Matrices are explicit row-major destination-channel × source-channel maps.
- Routing validation rejects missing buses, duplicate IDs, dimension mismatches, invalid master buses, and cycles.
- Topological ordering is deterministic, so identical routing state produces identical processing order.

### Multichannel render and playback

- `RoutedPlaybackTimeline` mixes mono or interleaved 1–8 channel clips into preallocated bus workspaces.
- Bus gain, mute, solo, and downstream sends are applied before device mapping.
- `SpscInterleavedAudioRingBuffer` preserves complete multichannel frames rather than independent samples.
- `MultichannelPlaybackFeederService` retains the Phase 4.1 control-queue and consumer-owned reset-epoch contract.
- `MultichannelRingBufferAudioProcessor` deinterleaves callback PCM into 1–8 device channel views without allocation, locks, file I/O, or routing-graph traversal in the callback.
- Linux PulseAudio and the deterministic threaded device adapter accept 1–8 output channels.

### Phase 6 evidence

`seam_phase6_demo` constructs a real four-channel graph: vocal stereo is routed to device channels 1–2 and backing stereo to channels 3–4. It exports the schema-4 project and a four-channel WAV, then passes the same content through the feeder, interleaved ring, and callback processor.

See [`docs/phase6/IMPLEMENTATION_REPORT.md`](docs/phase6/IMPLEMENTATION_REPORT.md), [`docs/phase6/ACCEPTANCE.md`](docs/phase6/ACCEPTANCE.md), and [`docs/formats/PROJECT_JSON_V4.md`](docs/formats/PROJECT_JSON_V4.md).


## Phase 5.1 implementation

### Character 01 is now a defined product surface

- Character 01 is the avatar of the first official voicebank and SEAM's sample-splice product identity; it is not a singer persona.
- Canonical low-poly art lives under `assets/character-01/source/`; bounded pre-rendered runtime portraits live under `assets/character-01/runtime/`.
- Voicebank Manifest schema 3 can bind an optional `characterId` and `characterVersion`.
- Native editor presentation modes are `Full`, `Minimal`, and `Off`.
- Full mode occupies a dedicated right-side dock and never covers Piano Roll, Phoneme, Unit, or Automation lanes.
- Character package presence never changes synthesis, exported audio, or PCM cache identity.
- See `docs/phase5_1/CHARACTER_INTEGRATION.md` for the exact surface matrix and architecture boundary.

### Native technical editor surfaces

- Native Phoneme Lane populated from the Japanese phonemizer.
- Native Unit Lane populated from persistent Unit selection overrides and renderer choice.
- Native Pitch Automation lane.
- Technical lanes are excluded from piano-roll note-drag hit testing.

### Graphical Voicebank Studio and recording transport

- `seam_voicebank_studio_native` is a real X11 application.
- Unit browser, waveform, spectrogram, acoustic-marker editing, Pitch-Mark editing, and manifest save are wired to existing validated domain paths.
- `IAudioInputDevice` adds an input-side platform contract.
- Linux can capture through runtime-loaded PulseAudio Simple; CI/headless mode uses an explicit non-physical threaded input fallback.
- `RecordingSession` uses preallocated bounded storage and exports mono PCM16 takes.

### New executable

```text
seam_voicebank_studio_native   graphical bank inspector/editor + recording transport
```

## Phase 5 implementation

### Native standalone editor

- First-party retained `NativeEditorController` over the existing Project, Session, and PianoRoll models.
- Deterministic BGRA software-raster `PixelSurface` and `RasterCanvas`.
- Real X11 window with resize, pointer, wheel, keyboard, repaint, and timed-close handling.
- XIM/XIC-backed Unicode lyric input with commit/cancel and undoable `SetLyricCommand`.
- Logical 1×/2× DPI coordinate contract.
- Native X11 smoke test under Xvfb with a real window screenshot.

### Native playback runtime

- Dedicated `PlaybackFeederService` thread is the only PCM producer.
- UI transport methods enqueue timeline, loop, play, and seek commands.
- Existing SPSC ring remains callback-consumer-owned.
- Runtime-loaded PulseAudio Simple physical adapter for Linux.
- Explicit callback-clock fallback that owns a real thread but reports `physical=false`.
- Native app pre-buffers before starting the callback.

### New executables

```text
seam_editor_native      interactive X11 standalone vertical slice
seam_phase5_demo        deterministic UI/controller/audio evidence
seam_phase5_benchmark   software paint and callback regression benchmark
```

## Phase 4.1 implementation

### Render identity v3

- Generated application version, render ABI, component revisions, and PCM cache format revision.
- SHA-256 content identity.
- Phrase-scoped canonical project state.
- Immutable phoneme and deterministic Unit plans owned by the snapshot.
- Selected Unit metadata only; unrelated Unit edits do not invalidate the Phrase.
- SHA-256 of each selected WAV's actual bytes.
- Decoded selected Unit audio is frozen from those same bytes, so an in-flight render cannot diverge after a file replacement.
- Effective Raw, PSOLA, Spectral, Stretch, Pitch Curve, fallback, and Seam settings.
- Cache identity and production rendering consume the same immutable plans.

### Durable persistence and bounded input

- Shared durable atomic writer for Project JSON and Voicebank Manifest persistence.
- Same-directory temporary file, durable flush, atomic replace, parent-directory sync where supported, and `.bak` generation for canonical files.
- Deterministic write-stage fault injection.
- Exact signed 64-bit JSON representation.
- Correct surrogate-pair handling and lone-surrogate rejection.
- JSON input byte, depth, node, string, and collection limits.
- Project and Manifest file-size limits.
- Canonical voicebank asset containment with symbolic-link rejection.
- PCM cache v3 validates declared payload against actual file size before allocating samples.

### DSP boundary corrections

- SpectralClassic and Stretch preserve the original recording through `stableStart`.
- Only stable-vowel material is transformed; actual `vowelOnsetOffset` remains intact.
- Phase-alignment offset remains consistent after the overlap.
- Equal-power Seam mode uses a true equal-power gain law.
- Regression tests cover the source transition and overlap-exit derivative.

### Playback and scheduler concurrency

- Fixed-capacity SPSC playback control queue.
- Only the feeder mutates timeline, loop, playhead, and playing state.
- Atomically published observations and statistics.
- Consumer-owned audio-ring reset with monotonic request/acknowledgment epochs.
- Producer no longer advances the consumer's read index.
- Final scheduler revision gate after cache publication and before completion publication.
- Deterministic concurrency regression tests and a ThreadSanitizer preset.

### Transaction-safe editing

- Composite command apply/revert uses a whole-Project transaction snapshot.
- EditorSession execute, validation, undo, and redo failures restore the exact previous Project.
- Failed undo/redo history is invalidated safely instead of retaining inconsistent command state.

## Existing Phase 4 functionality

### Four concrete Unit renderers

- `RawLoopRenderer` for direct sample character and loop print.
- `ClassicPsolaRenderer` for pitch-mark-controlled voiced sustain.
- `SpectralClassicRenderer` for stable-vowel STFT pitch transformation with explicit formant and phase controls.
- `StretchUnitRenderer` for deterministic Unit-scoped granular stretch.
- Requested renderer, actual renderer, fallback state, and diagnostic remain inspectable.
- Raw fallback occurs only after backend rejection and an explicit fallback policy.
- All four renderers can coexist in one Phrase.

### Inspection models

- Unit Lane model with selected Unit, alternatives, destination geometry, target MIDI, forced state, actual renderer, fallback, and Seam data.
- Sample Microscope model with waveform columns, spectrogram, acoustic markers, Pitch Marks, frame/pixel mapping, and hit tests.
- Validated acoustic-marker and Pitch-Mark movement.
- Edited voicebank manifest persistence in the end-to-end demo.

### Callback-ready playback foundation

- Absolute-frame `PlaybackTimeline` for vocal and backing clips.
- Per-clip gain and edge fades.
- Preallocated `PlaybackFeeder` with seek, loop, and watermark fill.
- SPSC audio-ring handoff.
- Allocation-free `RingBufferAudioProcessor` with deterministic zero-fill and underflow counters.
- Stereo PCM16 evidence writer.

## Verification

Phase 10 retains every prior domain, synthesis, routing, distribution, recording, Unicode, native editor, and Voicebank Studio test while adding CLAP state, WAV conversion, dynamic module loading, host transport, and automation coverage.

```text
Named tests                            128 PASS / 0 FAIL
Linux Debug CTest                       20/20 PASS
Linux Release CTest                     20/20 PASS
ASan + UBSan named tests               128 PASS / 0 FAIL
ASan + UBSan Phase 10 CTest              6/6 PASS
Dynamic ProjectSEAM.clap load           PASS
WAV pack / inspect / extract            PASS
clap_entry export                       PASS
Phase 8 platform source contract        PASS
Master-only policy                      PASS
Dependency-license audit                PASS
Git object integrity                    PASS
```

The aggregate Linux Debug/Release suites include the existing X11 editor and Voicebank Studio smokes. Phase 10 sanitizer evidence directly covers the entire named suite plus the demo, WAV/state bridge, and dynamic CLAP host path. Windows/macOS runtime evidence must still be collected on those operating systems.

All configured builds use warnings as errors.

## Honest current boundary

Phase 9 contains first-party source implementations for Linux, Windows, and macOS native windows, composition input, output devices, recording devices, and trusted-system-font CJK/Latin rasterization. Linux is runtime-verified in the current environment. Windows and macOS are source-integrated and CI-gated but are **not** represented as locally hardware-certified.

The repository still does **not** claim:

- physical Windows runtime acceptance for TSF, WASAPI speaker/microphone, DPI, focus, and window lifecycle;
- physical macOS runtime acceptance for `NSTextInputClient`, CoreAudio speaker/microphone, backing scale, and lifecycle;
- audited iPlug2 + Skia production integration;
- full complex-script shaping beyond the implemented CJK/Latin product-UI scope;
- a contract-recorded and release-cleared human voicebank;
- CLAP, VST3, or AU plugin delivery.

These are separate platform/content/plugin release gates, not fake placeholders. Character 01 remains an optional product surface and voicebank binding; it does not participate in routing, synthesis, recording, cache identity, package trust, or exported PCM.

## Build

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Release and memory/undefined-behavior sanitizer builds:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure

cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize --output-on-failure
```

ThreadSanitizer:

```bash
cmake --preset thread-sanitize
cmake --build --preset thread-sanitize
ctest --preset thread-sanitize --output-on-failure
```


## Run the Phase 10 CLAP vertical slice

Build and generate the diagnostic render/state:

```bash
./build/dev/seam_phase10_demo --output out/phase10
```

Pack an existing multichannel WAV, inspect the state, and extract it again:

```bash
./build/dev/seam_clap_state_tool pack \
  out/phase10/phase10-diagnostic-4ch.wav \
  out/phase10/render.seamclapstate \
  --title "Project SEAM render" --gain-db 0
./build/dev/seam_clap_state_tool inspect out/phase10/render.seamclapstate
./build/dev/seam_clap_state_tool extract \
  out/phase10/render.seamclapstate out/phase10/recovered.wav
```

Load the actual module through the first-party smoke host:

```bash
./build/dev/seam_clap_host \
  --plugin ./build/dev/ProjectSEAM.clap \
  --state out/phase10/render.seamclapstate \
  --summary out/phase10/clap-host-summary.json
```

Generate reproducible Phase 10 evidence:

```bash
python3 scripts/generate_phase10_evidence.py --root . --skip-build
```

## Run the Phase 8 capability evidence

```bash
./build/dev/seam_phase8_demo --output out/phase8
python3 scripts/verify_phase8_platform_sources.py --root .
```

The JSON output reports the platform selected by the current build. It does not replace physical-device and IME runtime acceptance.

## Run the Phase 6 multichannel vertical slice

```bash
./build/dev/seam_phase6_demo --output out/phase6
```

Key output:

```text
out/phase6/phase6-routing-project.json
out/phase6/phase6-four-channel-routing.wav
out/phase6/phase6-summary.json
```

## Run the Phase 5 native runtime

Headless deterministic vertical slice:

```bash
./build/dev/seam_phase5_demo --output out/phase5
```

Native X11 editor:

```bash
./build/dev/seam_editor_native
```

CI/headless native smoke:

```bash
xvfb-run -a ./build/dev/seam_editor_native \
  --force-threaded-audio \
  --auto-close-ms 500 \
  --screenshot out/phase5-native-window.ppm
```

## Run the Phase 4 vertical slice

```bash
./build/dev/seam_phase4_demo --output out/phase4
```

Key output:

```text
out/phase4/phase4-demo.seam.json
out/phase4/phase4-editor.svg
out/phase4/phase4-microscope.svg
out/phase4/phase4-mixed-render.wav
out/phase4/phase4-raw-reference.wav
out/phase4/phase4-playback-mix.wav
out/phase4/phase4-callback-preview.wav
out/phase4/phase4-waveform.svg
out/phase4/phase4-spectrogram.pgm
out/phase4/phase4-edited-voicebank-manifest.json
out/phase4/phase4-summary.json
```

## Reproducible evidence

Phase 8 cross-platform source and Linux regression evidence:

```bash
python3 scripts/generate_phase8_evidence.py --root .
```

Phase 5 native-runtime evidence:

```bash
python3 scripts/generate_phase5_evidence.py --root .
```

Phase 4.1 stabilization evidence:

```bash
python3 scripts/generate_phase4_1_evidence.py --root .
```

Or from the development build:

```bash
cmake --build --preset dev --target seam_phase4_1_evidence
```

Phase 4 audiovisual evidence remains reproducible with:

```bash
python3 scripts/generate_phase4_evidence.py --root .
```

## Benchmarks

```bash
./build/release/seam_phase1_benchmark
./build/release/seam_phase2_benchmark
./build/release/seam_phase3_benchmark
./build/release/seam_phase4_benchmark
./build/release/seam_phase5_benchmark
./build/release/seam_phase10_benchmark
```

Benchmark values are machine- and build-specific regression evidence, not universal performance guarantees. SpectralClassic remains a correctness-first quality renderer and is not presented as a guaranteed real-time preview path.

## Repository policy

Only `master` is permitted. Hooks are configured with:

```bash
git config core.hooksPath .githooks
```

## Documentation

- [`PHASE10_IMPLEMENTATION_REPORT.md`](PHASE10_IMPLEMENTATION_REPORT.md)
- [`PHASE10_IMPLEMENTATION_REPORT_KO.md`](PHASE10_IMPLEMENTATION_REPORT_KO.md)
- [`docs/phase10/ACCEPTANCE.md`](docs/phase10/ACCEPTANCE.md)
- [`docs/architecture/CLAP_PLUGIN_RUNTIME.md`](docs/architecture/CLAP_PLUGIN_RUNTIME.md)
- [`docs/formats/CLAP_STATE_V1.md`](docs/formats/CLAP_STATE_V1.md)
- [`PHASE8_IMPLEMENTATION_REPORT.md`](PHASE8_IMPLEMENTATION_REPORT.md)
- [`docs/phase8/ACCEPTANCE.md`](docs/phase8/ACCEPTANCE.md)
- [`docs/phase8/PLATFORM_MATRIX.md`](docs/phase8/PLATFORM_MATRIX.md)
- [`docs/phase8/BUILD_AND_RUNTIME.md`](docs/phase8/BUILD_AND_RUNTIME.md)
- [`docs/architecture/NATIVE_PLATFORM_ADAPTERS.md`](docs/architecture/NATIVE_PLATFORM_ADAPTERS.md)
- [`PHASE7_IMPLEMENTATION_REPORT.md`](PHASE7_IMPLEMENTATION_REPORT.md)
- [`docs/phase7/SIGNING_AND_INSTALLATION.md`](docs/phase7/SIGNING_AND_INSTALLATION.md)
- [`docs/phase7/ACCEPTANCE.md`](docs/phase7/ACCEPTANCE.md)
- [`docs/formats/SEAMBANK_V1.md`](docs/formats/SEAMBANK_V1.md)
- [`docs/phase6/IMPLEMENTATION_REPORT.md`](docs/phase6/IMPLEMENTATION_REPORT.md)
- [`docs/phase6/ACCEPTANCE.md`](docs/phase6/ACCEPTANCE.md)
- [`docs/formats/PROJECT_JSON_V4.md`](docs/formats/PROJECT_JSON_V4.md)
- [`PHASE5_IMPLEMENTATION_REPORT.md`](PHASE5_IMPLEMENTATION_REPORT.md)
- [`docs/architecture/PHASE5_NATIVE_RUNTIME.md`](docs/architecture/PHASE5_NATIVE_RUNTIME.md)
- [`docs/phase5/ACCEPTANCE.md`](docs/phase5/ACCEPTANCE.md)
- [`docs/phase5/EVIDENCE.md`](docs/phase5/EVIDENCE.md)
- [`PHASE4_1_IMPLEMENTATION_REPORT.md`](PHASE4_1_IMPLEMENTATION_REPORT.md)
- [`docs/architecture/PHASE4_1_STABILIZATION.md`](docs/architecture/PHASE4_1_STABILIZATION.md)
- [`docs/phase4_1/ACCEPTANCE.md`](docs/phase4_1/ACCEPTANCE.md)
- [`docs/phase4_1/EVIDENCE.md`](docs/phase4_1/EVIDENCE.md)
- [`PHASE4_IMPLEMENTATION_REPORT.md`](PHASE4_IMPLEMENTATION_REPORT.md)
- [`docs/architecture/PHASE4_SPECTRAL_PLAYBACK.md`](docs/architecture/PHASE4_SPECTRAL_PLAYBACK.md)
- [`docs/licensing/DEPENDENCY_POLICY.md`](docs/licensing/DEPENDENCY_POLICY.md)

## Ownership and licensing

Project source and first-party concept assets are currently proprietary and all rights are reserved. No production third-party source is vendored. Reference projects are documented for behavioral study only.

See [`LICENSE`](LICENSE), [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), and [`third_party/manifest.yml`](third_party/manifest.yml).
