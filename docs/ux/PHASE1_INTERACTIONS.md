# Phase 1 Interaction Specification

## Note creation

- Pointer position is converted to region-local tick and MIDI key.
- Start and duration are snapped when project snapping is enabled.
- Duration is forced positive.
- A new lyric token and note are created atomically.
- The new note becomes the sole selection.

## Move

- Every selected note records before/after tick and pitch.
- Pitch is clamped to MIDI 0–127.
- A move before region tick zero is rejected.
- A multi-note move occupies one undo entry.

## Resize

- Start and end deltas are independent.
- Resulting duration must be positive.
- Domain validation rejects a note extending beyond its region.

## Delete

- The command records original note and lyric locations.
- A lyric is removed only when no remaining note references it.
- Undo restores the canonical state.

## Box selection

- The box intersects materialized note rectangles in the visible viewport.
- Replace and additive selection are supported.

## Zoom and pan

- Horizontal zoom preserves the tick beneath the anchor pointer.
- Vertical layout is controlled only by `PitchTransform`.
- No canonical state is changed by viewport navigation.
