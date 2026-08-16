# iPlug2 + Skia Adapter Plan

## Phase 1 status

The native adapter is intentionally not vendored or compiled. `SEAM_ENABLE_IPLUG2_SKIA=ON` fails fast rather than silently pretending the production shell exists.

The reasons are operational rather than architectural:

1. exact source revisions must be pinned;
2. the complete build closure, including optional graphics/font dependencies, must pass the permissive-license policy;
3. Windows and macOS toolchains must use the same approved revisions;
4. no UI/business logic may migrate into SDK-specific controls while integration is in progress.

## Adapter responsibilities

```text
iPlug2
- application/window lifecycle
- standalone audio/MIDI device integration
- later CLAP/VST3/AU entry points
- host resize and DPI bridge

Skia
- piano-roll grid
- notes and text
- automation paths
- waveform/spectrogram tiles
- retained editor scene painting
```

## Required interface mapping

```text
Native pointer/keyboard event
→ Project SEAM InputEvent
→ interaction state machine
→ application command

Editor scene
→ Skia painter
→ platform surface
```

The existing `PianoRollModel` and transforms remain unchanged. `SvgEditorRenderer` serves as the geometry reference for the first Skia painter.

## Acceptance criteria

- native window on Windows and macOS;
- editor preview matches SVG fixture geometry within a documented tolerance;
- high-DPI scaling from 100% to 200%; 
- no domain/application headers include iPlug2 or Skia;
- native mouse drag commits exactly one command;
- graphics work does not occur on the audio callback;
- exact dependency commits and notices are present in the manifest.
