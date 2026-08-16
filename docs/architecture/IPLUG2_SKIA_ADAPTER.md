# Optional iPlug2 + Skia Production Adapter Plan

## Phase 5 status

Project SEAM now has a first-party native standalone reference shell:

```text
NativeEditorController
→ EditorScenePainter
→ RasterCanvas / PixelSurface
→ X11 window + XIM
```

This closes the native lifecycle, event, logical-DPI, text-input, feeder-thread, and callback contracts on the available verification platform. It does **not** mean iPlug2 or Skia is present. `SEAM_ENABLE_IPLUG2_SKIA=ON` continues to fail fast until exact source revisions and their complete dependency closure are approved.

## Why retain the future adapter

The software-raster shell is designed for correctness and deterministic evidence. A production editor still benefits from:

- GPU-accelerated waveform and spectrogram tiles;
- full Unicode/CJK font shaping and rasterization;
- Windows/macOS standalone lifecycle;
- later CLAP/VST3/AU entry points;
- host resize, DPI, and focus integration.

The future adapter must reuse the Phase 5 controller and scene contracts instead of moving editor behavior into SDK-specific controls.

## Planned responsibilities

```text
iPlug2
- Windows/macOS application lifecycle
- standalone audio/MIDI device integration or platform bridge
- later plug-in entry points
- host resize and DPI bridge

Skia
- piano-roll grid and notes
- shaped Unicode text
- automation paths
- waveform/spectrogram tiles
- retained editor scene painting
```

## Required mapping

```text
Native/iPlug2 pointer or keyboard event
→ Project SEAM Input DTO
→ NativeEditorController
→ application command

Editor scene state
→ Skia painter
→ platform surface
```

## Dependency gate

Before enabling the adapter:

1. pin exact immutable iPlug2 and Skia revisions;
2. enumerate all source and binary inputs used by Windows and macOS builds;
3. verify each license and notice under the repository allowlist;
4. record source hashes in `third_party/manifest.yml`;
5. produce a clean package SBOM;
6. test native close, resize, focus, IME, and audio shutdown on both platforms.

## Acceptance criteria

- native windows on Windows and macOS;
- editor geometry matches the Phase 5 software reference within documented tolerance;
- full CJK text input and display;
- 100–200% DPI scaling;
- no domain/application header includes iPlug2 or Skia;
- native drag commits one command;
- graphics and font work never execute on the callback;
- exact dependency commits and notices ship with the package.
