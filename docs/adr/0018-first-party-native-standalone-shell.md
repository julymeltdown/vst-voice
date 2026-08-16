# ADR 0018: Add a first-party native standalone shell before the optional iPlug2/Skia shell

**Status:** Accepted  
**Date:** 2026-08-16

## Context

Phase 4.1 made the domain, renderer, cache, and playback contracts safe enough for a native runtime. The intended long-term shell remains iPlug2 with Skia, but those source trees are not vendored and their exact transitive build closure has not yet passed the repository's permissive-license gate. Blocking all native integration until that audit would leave window, event, IME, DPI, and callback contracts untested.

## Decision

Implement a small first-party standalone shell with:

- a backend-independent retained editor controller;
- a deterministic software-raster `PixelSurface` and `RasterCanvas`;
- an X11 window backend for the currently available verification platform;
- XIM-backed native text input on X11;
- platform factories that fail explicitly when no backend is compiled.

The shell is not a substitute for the planned GPU renderer. It is a reference implementation and executable contract test for native lifecycle, input, HiDPI coordinates, repainting, and editor commands.

## Consequences

- A real native window can be tested now without copying an existing editor or importing an unaudited SDK.
- Domain and application modules remain free of X11, iPlug2, and Skia types.
- The software painter is deterministic and useful for screenshots and golden geometry checks.
- Windows and macOS production adapters remain separate deliverables and must not be claimed from the X11 implementation.
- iPlug2/Skia can later replace the platform/painter adapters without replacing the editor controller or command model.
