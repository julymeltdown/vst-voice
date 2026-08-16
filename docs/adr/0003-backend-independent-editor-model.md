# ADR 0003: Backend-Independent Editor Model

- Status: Accepted
- Date: 2026-08-16

## Decision

Keep interaction geometry and editing operations independent of Skia/iPlug2. Use a proof SVG painter in Phase 1.

## Consequence

The native adapter is additional work, but UI rules can be unit-tested and visualized without a GPU/window SDK. The domain remains portable to standalone and plugin shells.
