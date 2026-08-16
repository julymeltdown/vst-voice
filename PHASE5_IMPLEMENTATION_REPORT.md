# Project SEAM Phase 5 Implementation Report

## 1. Result

Phase 5 implements the first native standalone runtime vertical slice on top of the Phase 4.1-stabilized editing, synthesis, cache, scheduler, and callback contracts.

The verified runtime target is Linux/X11 because it is the native desktop environment available in the build and test container. The implementation is deliberately structured behind backend-neutral interfaces. It does not claim that Windows TSF, macOS AppKit, WASAPI, CoreAudio, iPlug2, or Skia have been implemented.

The completed vertical slice is:

```text
Native X11 Window / XIM
        ↓
NativeEditorController
        ↓
EditorSession + PianoRollModel
        ↓
EditorScenePainter + Software Raster
        ↓
Dedicated PlaybackFeederService
        ↓
SPSC PCM Ring
        ↓
RingBufferAudioProcessor
        ↓
PulseAudio physical adapter
or explicit non-physical callback-clock fallback
```

## 2. Phase objectives

Phase 5 was intended to close the following gaps left after Phase 4.1:

1. establish a real native window lifecycle rather than SVG-only evidence;
2. route pointer, keyboard, wheel, and Unicode text input into existing application commands;
3. establish a logical-pixel and physical-pixel DPI contract;
4. move PCM production into a continuously running dedicated feeder thread;
5. connect the existing callback consumer to an operating-system audio adapter;
6. retain an explicit deterministic fallback for CI and machines without a usable physical server;
7. preserve all Phase 4.1 real-time ownership and data-race constraints;
8. produce reproducible native-window, audio, sanitizer, benchmark, and package evidence.

## 3. Added modules

### 3.1 `seam-native-ui`

```text
libs/seam-native-ui/
├── include/seam/native_ui/
│   ├── editor_controller.hpp
│   ├── editor_scene.hpp
│   ├── native_window.hpp
│   └── pixel_surface.hpp
└── src/
    ├── editor_controller.cpp
    ├── editor_scene.cpp
    ├── native_window_unavailable.cpp
    ├── native_window_x11.cpp
    └── pixel_surface.cpp
```

The module owns native input DTOs, the retained interaction controller, deterministic software drawing, and the native-window abstraction. It does not own score persistence, synthesis, voicebank parsing, render scheduling, or audio mixing.

### 3.2 Playback feeder service

```text
libs/seam-rendering/include/seam/rendering/playback_feeder_service.hpp
libs/seam-rendering/src/playback_feeder_service.cpp
```

`PlaybackFeederService` owns the only thread allowed to call `PlaybackFeeder::feedToWatermark`. UI code sends immutable control commands through the already stabilized feeder command queue and wakes the service. The audio callback remains the sole SPSC consumer.

### 3.3 Audio device abstraction

```text
libs/seam-platform/include/seam/platform/audio_device.hpp
libs/seam-platform/src/threaded_audio_device.cpp
libs/seam-platform/src/pulse_audio_device.cpp
libs/seam-platform/src/system_audio_device_unavailable.cpp
```

The abstraction reports backend name, physical/non-physical status, actual format, callback counters, write failures, and xruns.

## 4. Native editor behavior

### 4.1 Existing domain and application model reused

`NativeEditorController` operates directly on:

- `EditorSession`;
- `ProjectFactory`;
- `PianoRollModel`;
- `TextCompositionModel`;
- existing undoable note and lyric commands.

No duplicate native-only Project or Note model was introduced.

### 4.2 Pointer editing

Supported interactions in the Phase 5 native slice:

- note hit test and selection;
- shift-toggle selection;
- box selection;
- note drag;
- double-click note creation;
- double-click or Enter lyric editing;
- delete/backspace selected notes;
- arrow-key pitch/time movement;
- Ctrl/Cmd+Z undo;
- Ctrl/Cmd+Y and Ctrl/Cmd+Shift+Z redo;
- wheel pan;
- Ctrl/Cmd+wheel zoom;
- plus/minus zoom;
- Space transport toggle.

A note drag does not mutate the Project on every pointer move.

```text
Pointer Down
→ capture drag state

Pointer Move
→ update temporary interaction state
→ request repaint

Pointer Up
→ execute exactly one MoveNotes command
→ create exactly one undo entry
```

### 4.3 Unicode lyric editing

The native text contract supports:

- begin;
- composition update;
- cursor movement;
- insertion;
- backspace/delete;
- commit;
- cancel.

The process locale is initialized before playback worker threads start. The X11 backend then requests the environment input-method module and opens XIM/XIC. Committed UTF-8 input is decoded into UTF-32 and submitted through `SetLyricCommand`, so Unicode lyric changes participate in undo and redo.

The compact built-in diagnostic font only rasterizes a small ASCII set. Unicode input and persistence are correct, but production CJK shaping and glyph rasterization remain future work.

## 5. Native X11 window

The X11 adapter implements:

```text
XOpenDisplay
→ create Window and GC
→ establish WM_DELETE_WINDOW
→ open XIM and XIC
→ map window
→ translate native input events
→ repaint at a bounded cadence
→ present BGRA surface with XPutImage
→ optional final PPM screenshot
→ destroy XIC/XIM/GC/Window/Display
```

Supported event categories:

- expose;
- resize;
- focus;
- pointer down/up/move;
- vertical and horizontal wheel input;
- keyboard input;
- native UTF-8 commits;
- window close.

The CTest native smoke target opens the actual X11 window under Xvfb and exports the final native frame. It is not an SVG-only mock.

Platforms without an implemented native-window backend receive an explicit `UnavailableWindow` implementation instead of an unresolved factory or a silent fake window.

## 6. Software raster reference renderer

`PixelSurface` is a bounded BGRA 32-bit surface. `RasterCanvas` supplies the minimum operations needed by the retained editor scene:

- alpha-blended rectangles;
- strokes;
- lines;
- vertical gradients;
- compact diagnostic text;
- deterministic checksum;
- PPM export.

All scene geometry is expressed in logical pixels.

```text
physical pixel = logical coordinate × scale
native input logical coordinate = physical coordinate ÷ scale
```

The same logical editor state is rendered at 1× and 2× in tests and evidence. This painter is a correctness reference and fallback shell, not the final GPU renderer.

## 7. Dedicated playback runtime

### 7.1 Thread ownership

```text
UI Thread
→ setTimeline / setLoop / setPlaying / seek commands

PlaybackFeederService Thread
→ apply feeder commands
→ fill SPSC ring to high watermark

Audio Callback Thread
→ consume SPSC ring
→ zero-fill underflow deterministically
```

The service has explicit start/stop lifecycle, double-start rejection, bounded polling intervals, wake generation, and counters for iterations, wake signals, idle waits, and frames fed.

### 7.2 Pre-buffering

The native application starts the feeder before the audio callback and waits for an initial amount of PCM when playback starts. This avoids interpreting startup ordering as a callback underrun in native smoke evidence.

## 8. Audio device implementations

### 8.1 PulseAudio physical adapter

The Linux adapter runtime-loads the operating system's PulseAudio Simple shared library. Project SEAM does not copy or redistribute PulseAudio source or shared objects in the repository package.

Behavior:

- validates sample rate, block size, and channel count;
- opens the default physical output;
- runs the Project SEAM `IAudioProcessor` contract on a dedicated write thread;
- converts finite float PCM to interleaved signed 16-bit PCM;
- reports write failures explicitly;
- drains on normal stop;
- identifies itself as `physical=true`.

The named deterministic test points PulseAudio at a deliberately unreachable local endpoint. This validates that the adapter resolves its runtime API and returns a bounded structured error without relying on CI machine audio configuration. The interactive application separately attempts the user's actual default server.

### 8.2 Callback-clock fallback

The fallback:

- owns a real `std::jthread`;
- calls the same `IAudioProcessor` callback contract at the requested block cadence;
- tracks callback count, frames, and late wakeups;
- performs no physical speaker output;
- identifies itself as `physical=false`.

It is used for deterministic tests, headless evidence, and environments where physical output cannot be opened. The fallback is never described as successful physical audio.

Platforms without a physical adapter receive an explicit unavailable implementation.

## 9. Added executables

### `seam_editor_native`

Interactive native standalone vertical slice.

```bash
./build/dev/seam_editor_native
```

Headless CI/native-window smoke:

```bash
xvfb-run -a ./build/dev/seam_editor_native \
  --force-threaded-audio \
  --auto-close-ms 500 \
  --screenshot out/phase5-native-window.ppm
```

Options:

```text
--auto-close-ms N
--screenshot PATH
--scale N
--force-threaded-audio
--paused
```

### `seam_phase5_demo`

Deterministic headless vertical slice covering:

- controller note move;
- Unicode lyric commit;
- 1× and 2× rendering;
- feeder service;
- callback device;
- WAV evidence;
- structured JSON summary.

### `seam_phase5_benchmark`

Release regression benchmark covering:

- 5,000-note model;
- viewport virtualization;
- repeated native software painting;
- feeder/callback delivery;
- callback underflow accounting.

## 10. Build-system behavior

`SEAM_BUILD_NATIVE_DESKTOP` controls native editor components. When disabled:

- native UI tests and executables are not added;
- all non-native engine and Phase 1–4 targets still build;
- CTest continues to pass without unresolved native symbols.

`SEAM_ENABLE_IPLUG2_SKIA` remains dependency-gated and fails fast. No iPlug2 or Skia source is present or claimed in this phase.

## 11. Verification

Phase 5 adds eight named tests, increasing the direct suite from 84 to 92.

New test coverage:

- deterministic pixel output and PPM export;
- logical 1×/2× scene rendering;
- one-command note drag;
- Unicode lyric commit and undo;
- feeder-service lifecycle;
- callback-clock device lifecycle;
- concurrent seek/play under active feeder and callback threads;
- bounded physical-audio open result.

Final evidence matrix:

```text
Named tests                         92 PASS / 0 FAIL
Debug CTest                          8/8 PASS
Release CTest                        8/8 PASS
ASan + UBSan CTest                   8/8 PASS
ThreadSanitizer named suite         92 PASS / 0 FAIL
Native X11/Xvfb smoke               PASS
Callback underflow in evidence      0 frames
Master-only policy                  PASS
Dependency license audit            PASS
Git object integrity                PASS
```

Release benchmark from the verification environment:

```text
Visible notes                74 of 5,000
Paint iterations             60
Average software paint       0.553 ms
Audio callbacks              94
Feeder frames                28,672
Delivered callback frames    12,032
Underflow frames             0
```

Benchmark values are regression evidence for the current environment, not universal performance guarantees.

## 12. Documentation added

```text
docs/adr/0018-first-party-native-standalone-shell.md
docs/adr/0019-dedicated-playback-feeder-service.md
docs/adr/0020-system-audio-adapter-and-explicit-fallback.md
docs/architecture/PHASE5_NATIVE_RUNTIME.md
docs/phase5/ACCEPTANCE.md
docs/phase5/EVIDENCE.md
docs/phase5/FILE_TREE.txt
```

`README.md`, `SBOM.spdx.json`, `THIRD_PARTY_NOTICES.md`, the optional iPlug2/Skia plan, and the runtime-integration manifest were updated.

## 13. Explicitly outside Phase 5

The following remain future phases and are not represented by placeholders falsely described as complete:

- Windows native shell and TSF;
- macOS AppKit and native composition;
- WASAPI and CoreAudio;
- audited iPlug2 + Skia production adapter;
- production CJK font shaping/rasterization;
- full native Phoneme, Unit, Automation, and Sample Microscope panels;
- complete graphical Voicebank Studio;
- microphone recording transport;
- true multichannel project routing;
- licensed human-recorded official voicebank;
- signed/installable `.seambank` packages;
- CLAP, VST3, and AU.

## 14. Phase conclusion

Phase 5 moves Project SEAM beyond model-only and callback-ready evidence. A real operating-system window now edits the existing Project model, native Unicode commits enter the existing undo stack, a dedicated producer continuously feeds the real callback contract, and a physical Linux adapter exists with an explicit non-physical fallback.

The result is a verified Linux native standalone vertical slice, not yet a cross-platform production editor. The next development work should extend the native scene with Phoneme/Unit/Automation/Sample Microscope panels and add platform adapters in a separately auditable phase rather than disguising those gaps in the current implementation.
