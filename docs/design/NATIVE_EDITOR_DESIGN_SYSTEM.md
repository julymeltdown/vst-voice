# Native Editor Design System

## Scope

This document owns the code-level visual contract for the native Project SEAM
editor. It describes the implementation at commit `d7af125b`; it is not a
release, accessibility-certification, or Usable Alpha acceptance claim.

## Frame and hierarchy

The editor has one shared frame model:

1. Toolbar: project identity, transport, tempo, lyric distribution, loop,
   voice identity, then render state.
2. Timeline: ruler, keyboard, piano roll, and overlap-aware notes.
3. Technical lanes: phoneme, unit, seam, and pitch.
4. Recovery/status: diagnostics, export progress, and transport status.

`EditorSceneLayout` owns visible dimensions and control bounds. The painter,
native controller, semantic tree, and CLAP adaptation consume the same layout
inputs rather than maintaining independent constants.

## Text and density

- `RasterCanvas::drawText(Rect, ...)` clips every glyph to the owner rectangle.
- `EditorLabelPolicy` selects full, compact, or hidden labels by available
  width. Complete text remains available to semantic/detail paths.
- CJK, combining marks, emoji, and malformed UTF-8 fallback rendering use the
  shared Unicode display-width utilities.
- Empty technical lanes collapse to 20 px; expanded lanes persist a bounded
  user-selected height without changing render identity.

## Notes and interaction

`note_visual_layout` preserves a note's truthful timeline range while allocating
up to three visible overlap bands. Higher-density members are represented by a
`+N` indicator and remain addressable through stable overlap-candidate cycling.
Paint, hit, and semantic bounds are separately explicit.

## Voice identity and character assets

`VoiceIdentity` is the single source of truth for exact voicebank identity,
render readiness, recovery state, and optional character activation. A portrait
is rendered or exposed only when the selected card, content hash, character ID,
character version, and voicebank binding all match. Character presentation is
therefore optional UI state and never render identity.

## Recovery and accessibility

Diagnostic copy maps stable error codes to a human title, impact sentence, and
up to two visible primary recovery controls. The recovery panel's paint, pointer,
and semantic button bounds are derived by `EditorSceneLayout`; the diagnostic
body is not an invisible primary action.

## Current evidence

At `d7af125b`, the following local evidence has been observed:

- Release native test executable: 414 passed, 0 failed.
- Phase 11 CLAP editor test: passed.
- Release CTest re-run passed its formerly failing Phase 12b and tracked-source
  closure targets after the commit.
- ASan/UBSan and TSan suites reported no sanitizer finding; both only had the
  pre-commit tracked-source-closure failure, which the committed target now
  passes.
- Native paint benchmark: p95 5.12 ms for the dense reference fixture.
- Deterministic capture matrix: 48 non-empty captures at six viewports, four
  zoom levels, and two backing scales.

## Boundaries

The remaining product-level release evidence is intentionally not implied by
this document: independent visual review closure, VoiceOver/Accessibility
Inspector observation, physical listening, distribution signing/notarization,
and the separate External Beta and Usable Alpha contracts remain fail-closed.
