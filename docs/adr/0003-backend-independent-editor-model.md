# ADR 0003: Backend-Independent Editor Model

- Status: Accepted
- Date: 2026-08-16

> **Status note, 2026-10-18.** The Phase 1 SVG proof painter (`SvgEditorRenderer`) was retired on 2026-10-18 (`c52517f9`, `41dc63fd`). The decision itself still holds: `seam_editor_ui` and `seam_application` carry the interaction geometry and editing operations and link no painter backend.

## Decision

Keep interaction geometry and editing operations independent of Skia/iPlug2. Use a proof SVG painter in Phase 1.

## Consequence

The native adapter is additional work, but UI rules can be unit-tested and visualized without a GPU/window SDK. The domain remains portable to standalone and plugin shells.
