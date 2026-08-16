# Editor Foundation

## Coordinate spaces

The editor separates four coordinate systems:

```text
Tick          canonical musical position
SampleFrame   canonical rendered-audio position
Microseconds  future phoneme timing overrides
Pixel         transient viewport position
```

Phase 1 implements Tick and Pixel conversion. Audio sample-frame conversion is implemented in `TempoMap`; phoneme microsecond overrides arrive later.

## TimelineTransform

`TimelineTransform` owns:

- PPQ
- pixels per quarter note
- scroll origin in ticks
- tick→pixel
- pixel→tick
- duration→pixel
- anchor-preserving zoom
- horizontal pan

A zoom operation computes the tick under the pointer before zooming and adjusts the scroll origin so the same tick remains under the pointer afterward.

## PitchTransform

`PitchTransform` owns row height and the top MIDI key. It converts MIDI pitch to vertical pixels and vice versa.

## NoteSpatialIndex

The Phase 1 index stores compact note metadata sorted by absolute start tick. Viewport queries use binary search on time and then filter pitch/range intersection. A 10,000-note project therefore creates only visible `NoteVisual` values for painting and hit testing.

## Editing operations

- Draw: pixel→tick and pixel→MIDI, optional snapping, AddNote command.
- Move: selected IDs→before/after positions, one MoveNotes command.
- Resize: selected IDs→before/after ranges, one ResizeNotes command.
- Delete: captures notes, original indices, and unreferenced lyrics for exact undo.
- Box select: intersects current visible note rectangles.

A production input adapter should use preview transactions during mouse movement and commit one command on pointer release.
