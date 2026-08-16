# Project SEAM Phase 5 Implementation Report

## Result

Phase 5 implements the first native standalone runtime vertical slice on top of the Phase 4.1-stabilized engine. It adds a real X11 window, XIM-backed lyric input, deterministic software rendering, a dedicated playback feeder thread, a physical PulseAudio adapter, and an explicit threaded fallback.

## Implemented modules

### Native editor UI

```text
libs/seam-native-ui/
├── editor_controller
├── editor_scene
├── pixel_surface
└── native_window_x11
```

The controller maps native events to existing undoable application commands. Note drag is preview-only until pointer release. Native text is committed through `SetLyricCommand`, so Unicode lyric edits participate in undo/redo.

### Native window

The X11 backend owns display/window/GC/XIM resources, initializes locale-dependent input before worker threads, translates input, renders at a bounded cadence, supports HiDPI logical coordinates, and can emit a final PPM screenshot. CI opens this window with Xvfb.

### Playback runtime

`PlaybackFeederService` owns a dedicated producer thread and fills the Phase 4.1 SPSC ring to a high watermark. `RingBufferAudioProcessor` remains the callback consumer.

### Audio devices

- `PulseAudioDevice`: runtime-loaded physical Linux output.
- `ThreadedAudioDevice`: deterministic callback-clock fallback for CI/offline use.

The fallback is clearly identified as non-physical. Unsupported operating systems receive explicit unavailable factories, so disabling the native desktop target or building without X11/PulseAudio does not leave unresolved platform symbols.

### Executables

- `seam_editor_native`: interactive native standalone editor slice.
- `seam_phase5_demo`: deterministic headless scene, controller, feeder, callback, and WAV evidence.
- `seam_phase5_benchmark`: software paint and callback pipeline regression benchmark.

## Verification scope

The named suite expands from 84 to 92 tests. New coverage includes:

- deterministic pixel raster and PPM export;
- logical HiDPI rendering;
- one-command pointer drag;
- Unicode native lyric commit and undo;
- feeder service lifecycle;
- threaded callback device;
- concurrent seek/play controls under active feeder and callback threads;
- explicit physical audio open success or error.

## Honest boundary

This phase verifies the native contract on Linux/X11. It does not claim Windows TSF, macOS AppKit, WASAPI, CoreAudio, iPlug2, or Skia. The software painter uses a compact diagnostic font and therefore does not yet provide production CJK glyph rasterization, although Unicode input and project persistence are correct.
