# Phase 5 Native Runtime Architecture

## 1. Scope

Phase 5 moves Project SEAM from a callback-ready engine to a native standalone runtime vertical slice. It connects the existing command/domain model to a real operating-system window, native text input, a dedicated playback producer, and an operating-system audio adapter.

The verified implementation target in this repository is Linux/X11 because that platform is available in the build environment. The design keeps platform code behind interfaces so Windows/macOS adapters can be added without changing editor or synthesis modules. Builds without an implemented native window or physical audio backend receive explicit `Unavailable` adapters rather than unresolved factories or silent simulation.

## 2. Runtime graph

```text
X11 Window / XIM
        │ pointer, key, committed text
        ▼
NativeEditorController
        │ undoable application commands
        ▼
EditorSession + PianoRollModel
        │ retained view state
        ▼
EditorScenePainter
        │ logical drawing operations
        ▼
RasterCanvas → PixelSurface → XPutImage

UI transport command
        ▼
PlaybackFeederService (producer thread)
        ▼
PlaybackFeeder → SPSC PCM Ring
        ▼
RingBufferAudioProcessor (callback consumer)
        ▼
PulseAudio adapter or explicit callback-clock fallback
```

## 3. Module boundaries

### `seam-native-ui`

Owns:

- `PixelSurface` and `RasterCanvas`;
- editor scene colors and painting;
- pointer/key/text event DTOs;
- `NativeEditorController` interaction state;
- `INativeWindow` and X11 implementation.

Does not own:

- score or voicebank persistence;
- synthesis;
- playback mixing;
- audio-device callbacks;
- X11 objects outside the X11 adapter translation unit.

### `seam-rendering`

Adds `PlaybackFeederService`. The service is the sole feeder-thread owner and does not know which physical audio backend consumes the ring.

### `seam-platform`

Adds:

- `IAudioDevice`;
- runtime-loaded PulseAudio Simple output;
- callback-clock fallback;
- device information and callback statistics.

The physical adapter receives an `IAudioProcessor`; it does not inspect Project or render state.

## 4. Native editor event contract

```text
Pointer down on note
→ selection update
→ drag preview only

Pointer move
→ temporary drag state
→ repaint

Pointer release
→ exactly one MoveNotesCommand
→ one undo entry
```

```text
Double click note
→ begin native text input
→ system IME owns composition/candidate interaction
→ committed Unicode updates TextCompositionModel
→ Enter commits one SetLyricCommand
→ Escape cancels
```

The process locale is initialized before worker threads start, and the X11 implementation requests the environment input-method module before opening XIM. It uses XIM with `Xutf8LookupString`. Intermediate IME events filtered by XIM are not converted into synthetic key sequences. The current X11 text editor is intentionally minimal: insertion, deletion, cursor movement, commit, and cancel are supported; rich selection and clipboard editing remain future work.

## 5. HiDPI contract

All editor geometry is expressed in logical pixels. A native window owns a physical `PixelSurface` and creates a `RasterCanvas` with a scale factor.

```text
logical coordinate × scale = physical pixel
```

Pointer coordinates are divided by the same scale before they reach `NativeEditorController`. Tests paint the same logical scene at 1× and 2× and verify valid non-empty deterministic surfaces.

## 6. Software raster policy

The software painter exists to establish behavior and provide a shippable reference shell. It is not presented as the final high-density GPU renderer.

Properties:

- BGRA 32-bit surface;
- bounded dimensions and allocation;
- alpha-blended rectangles;
- lines and gradients;
- small built-in diagnostic font;
- deterministic checksum;
- PPM evidence export.

Current limitation: the built-in painter does not rasterize full CJK glyph sets. Native IME accepts and stores Unicode correctly, while a later audited font renderer is required for production-quality CJK display.

## 7. X11 lifecycle

```text
XOpenDisplay
→ create window and GC
→ initialize XIM/XIC
→ allocate PixelSurface
→ dispatch resize/input events
→ repaint at a bounded 60 Hz cadence
→ present with XPutImage
→ optional final PPM snapshot
→ destroy XIC/XIM/GC/window/display
```

The window can run under Xvfb for CI smoke testing. The test opens a real X11 window, paints, runs the playback threads, closes on a timer, and writes a screenshot.

## 8. Playback service lifecycle

```text
construct feeder and service
→ enqueue timeline/loop
→ start service thread
→ enqueue play
→ service fills high watermark
→ start audio device
→ callback consumes ring
→ stop device
→ stop and join service
```

The app pre-buffers before starting the callback to avoid a deterministic initial underflow. A seek or loop change still uses the consumer-owned reset epoch established in Phase 4.1.

## 9. Physical and fallback audio

### PulseAudio Simple

- opened on the non-real-time application thread;
- runtime-loaded with `dlopen`;
- writes interleaved signed 16-bit mono/stereo blocks from a dedicated device thread;
- calls only `IAudioProcessor::process` and format conversion in that thread;
- exposes write-failure and callback statistics.

### Callback-clock fallback

- runs the same `IAudioProcessor` contract on a dedicated clocked thread;
- preallocates left and right blocks;
- reports missed deadlines;
- never claims physical output.

## 10. Failure handling

- No display: native window `open()` returns `IoError`.
- Missing Pulse library or server: physical adapter returns an explicit error.
- Fallback disabled by caller: application may abort instead of pretending audio exists.
- Invalid size, scale, sample rate, block size, or channel count: rejected before thread creation.
- Duplicate service/device start: returns `Conflict`.
- Control queue full: remains an explicit `Conflict` from Phase 4.1.

## 11. Verified acceptance boundary

Implemented and verified:

- first-party retained native editor controller;
- software-raster scene;
- real X11 window under Xvfb;
- XIM-backed Unicode text input path;
- dedicated feeder service;
- physical PulseAudio adapter with explicit error handling;
- deterministic threaded fallback;
- HiDPI 1×/2× rendering;
- previous Phase 2–4 demos and Phase 4.1 regressions.

Not yet implemented or verified:

- Windows native shell/TSF;
- macOS AppKit shell/`NSTextInputClient`;
- WASAPI/CoreAudio;
- iPlug2/Skia production adapter;
- GPU waveform/spectrogram tiles;
- complete native Phoneme Lane, Unit Lane, and Sample Microscope integration;
- graphical Voicebank Studio and recording transport;
- plug-in formats.
