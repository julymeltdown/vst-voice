# Phase 1 Acceptance

## Build

- [x] C++20 enforced.
- [x] Debug preset builds with warnings as errors.
- [x] Release preset defined.
- [x] Address/undefined sanitizer preset defined.
- [x] CTest integration.

## Domain

- [x] Tick, tempo, meter, quantization.
- [x] Project, track, region, note, lyric.
- [x] Domain validation.
- [x] Strong IDs and post-load ID synchronization.

## Editing

- [x] Add, move, resize, delete.
- [x] Multi-selection and box selection.
- [x] Undo/redo and revision.
- [x] Snap.
- [x] Zoom/pan transforms.
- [x] 10,000-note virtualization.
- [x] Proof piano-roll renderer.

## Persistence

- [x] UTF-8 JSON parser/writer.
- [x] Schema validation.
- [x] Round-trip equality.
- [x] Temporary-file replacement save.

## Platform boundary

- [x] Real-time callback contract and simulator.
- [x] Desktop backend contract.
- [ ] Production iPlug2/Skia adapter; dependency-gated and carried to Phase 2.

## Character

- [x] Three retained directions.
- [x] 128 px thumbnail and silhouette evidence.
- [x] Low-poly blockout fixtures.
- [x] authenticity and style constraints.
- [ ] Final canonical design; intentionally deferred.
