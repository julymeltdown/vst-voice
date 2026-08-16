# Project SEAM — Phase 7 signed voicebank distribution

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
- completed **Phase 7 signed, verifiable, and transactional `.seambank` packaging and installation**.

Development uses the **`master` branch only**.


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

Phase 6 extends the named suite to cover routing-graph validation, four-channel bus/device mapping, multichannel feeder/ring/callback order, and schema-4 round trips. CTest additionally covers the Phase 6 four-channel smoke path, the prior headless demos, native editor X11/Xvfb smoke test, and graphical Voicebank Studio X11/Xvfb smoke test.

```text
Named tests                        102 PASS / 0 FAIL
Debug CTest                         10/10 PASS
Release CTest                       10/10 PASS
ASan + UBSan CTest                  10/10 PASS
ThreadSanitizer coverage             pending Phase 6 evidence refresh
Master-only policy                  PASS
Dependency-license audit            PASS
Git object integrity                PASS
```

All configured builds use warnings as errors.

## Honest current boundary

Phase 6 now contains real persisted 1–8 channel routing, bus/send processing, device mapping, a multichannel feeder/ring/callback path, and four-channel evidence. The repository still does **not** claim:

- signed/installable `.seambank` packages;
- Windows shell, TSF, or WASAPI runtime verification;
- macOS AppKit, NSTextInputClient, or CoreAudio runtime verification;
- audited iPlug2 + Skia production integration;
- production CJK font shaping/rasterization in the first-party software painter;
- a contracted human-recorded commercial voicebank;
- CLAP, VST3, or AU targets.

Those items are separate distribution, platform, and content-production phases. They are not represented by fake placeholders. Character 01 remains an optional product surface and voicebank binding; it does not participate in routing, synthesis, cache identity, or exported PCM.

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
```

Benchmark values are machine- and build-specific regression evidence, not universal performance guarantees. SpectralClassic remains a correctness-first quality renderer and is not presented as a guaranteed real-time preview path.

## Repository policy

Only `master` is permitted. Hooks are configured with:

```bash
git config core.hooksPath .githooks
```

## Documentation

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
