# ADR 0020: Keep the first-party software-raster shell for Usable Alpha

**Status:** Accepted
**Date:** 2026-08-22

## Context

Project SEAM already has a deterministic first-party `RasterCanvas` renderer,
shared editor layout/semantic contracts, and native AppKit, Win32, and X11
window adapters. ADR 0018 intentionally introduced this shell before an
optional iPlug2/Skia migration so lifecycle, input, IME, HiDPI, accessibility,
repaint, and audio boundaries could be verified without an unaudited external
dependency closure.

The Usable Alpha gate is still blocked by target-machine, physical-audio,
rights-cleared-bank, host, and stability evidence. None of the current
failures identifies the software-raster/native adapter boundary as the cause.

## Decision

Keep the first-party software-raster renderer and native platform adapters as
the production rendering architecture for Usable Alpha and the first external
Beta qualification. The canonical surface remains:

```text
EditorSceneState / shared semantic tree
        -> RasterCanvas / PixelSurface
        -> AppKit, Win32, or X11 native adapter
```

Do not introduce iPlug2, Skia, WebView, React, or a GPU renderer as a release
prerequisite for this gate. A later renderer may replace only the painter and
platform presentation adapters after a measured performance or fidelity defect
is demonstrated and the same semantic, input, accessibility, and evidence
contracts are preserved.

## Consequences

- P14-001 is resolved; it is no longer a decision blocker.
- Deterministic screenshots, layout checks, and accessibility geometry remain
  meaningful across the current platform adapters.
- The software rasterizer's performance and full CJK/font coverage remain
  explicit acceptance measurements, not reasons to silently claim GPU parity.
- Any future renderer migration requires a new ADR, a measured regression
  rationale, independent visual comparison, and cross-platform contract tests.
- The remaining Usable Alpha blockers are target-environment and content/IP
  evidence, not an unresolved rendering-stack choice.
