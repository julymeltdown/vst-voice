# Native Editor Design System

## Scope

This document owns the code-level visual contract for the native Project SEAM
editor. It describes the implementation at commit
`af5a1d8f95fad33f03b5ae56ccf8158c7574c6dc`; it is not a
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
- Toolbar metadata is assigned collision-free responsive regions: compact
  widths suppress voice identity, medium widths reserve identity ahead of
  project/lyrics controls, and wide widths bound project text before identity.

## Notes and interaction

`note_visual_layout` preserves a note's truthful timeline range while allocating
up to three visible overlap bands. Higher-density members are represented by a
`+N` badge whose 30 x 18 px geometry does not inherit a thin note band.
Activating the group opens a bounded five-row detail panel containing every
member in stable order; repeated activation advances the selected row. Paint,
hit, detail, and semantic bounds share explicit layout helpers.

## Voice identity and character assets

`VoiceIdentity` is the single source of truth for exact voicebank identity,
render readiness, recovery state, and optional character activation. A portrait
is rendered or exposed only when the selected card, content hash, character ID,
character version, and voicebank binding all match. Character presentation is
therefore optional UI state and never render identity.

Lane and identity changes use one 150 ms smoothstep transition driven by an
injectable UI clock. The platform Reduce Motion capability bypasses the
intermediate frame and commits the same final geometry immediately.

## Recovery and accessibility

Diagnostic copy maps stable error codes to a human title, impact sentence, and
up to two visible primary recovery controls. The recovery panel's paint, pointer,
and semantic button bounds are derived by `EditorSceneLayout`; the diagnostic
body is not an invisible primary action.

## Current evidence

At `af5a1d8f95fad33f03b5ae56ccf8158c7574c6dc`, the following
local evidence was observed:

- Release CTest: 66/66 passed, including native, Phase 11 CLAP host, platform,
  source-contract, and fail-closed external-gate tests.
- Native test executable: 425 passed, 0 failed.
- ASan/UBSan: 63/63 passed with no sanitizer finding.
- ThreadSanitizer: 63/63 passed with no race finding.
- Phase 11 explicit tests and source/voice contract: 3/3 passed.
- Native paint benchmark at 10,000 notes: 2.19 ms p95 against a 16.7 ms budget,
  with 1,416 text-cache hits, 24 misses, and zero underflow frames.
- Deterministic visual packet: 48 viewport/scale/zoom captures plus 13 journey
  frames, reproduced twice with identical hashes.
- Actual AppKit journey: exact Release bundle, isolated support root, native
  note creation, writable multilingual lyric, Tab focus, full detail value, and
  discard-on-quit.
- Two fresh independent reviewers inspected all 64 images and returned PASS
  with no blocking product or evidence finding.

## Boundaries

This closes the local native-editor design rubric, not the product release
program. Physical Narrator observation, independent accessibility
certification, rights-cleared production character art, physical listening,
distribution signing/notarization, commercial-host certification, external
musician evidence, and the separate Usable Alpha and External Beta contracts
remain fail-closed.
