# Phase 3 Acceptance Criteria

## Accepted scope

Phase 3 is accepted when all items below are true.

### Canonical state

- [x] Project schema 3 stores unit-selection overrides.
- [x] Project schema 3 stores seam overrides.
- [x] Project schema 3 stores region pitch automation.
- [x] Schema 1 and 2 migrate to schema 3 defaults.
- [x] Render-control edits are undoable and redoable.
- [x] Note deletion removes and restores dependent overrides.

### Voicebank and renderer

- [x] Voicebank schema 2 stores editable pitch marks.
- [x] Schema 1 voicebanks migrate with empty pitch marks.
- [x] Pitch marks can be generated, validated, moved, added, removed, and locked.
- [x] Classic PSOLA renders voiced sustain while retaining source consonant/release identity.
- [x] Pitch automation and source residual affect PSOLA target pitch.
- [x] Raw and Classic PSOLA can coexist in one phrase.
- [x] Unsupported renderer fallback is explicit and inspectable.

### Phrase orchestration

- [x] Phrase segmentation is deterministic.
- [x] Dirty invalidation includes adjacent seam context.
- [x] Background workers receive immutable phrase-scoped snapshots.
- [x] Snapshot hashes ignore unrelated phrase and presentation edits.
- [x] Snapshot hashes change for phrase-affecting score and tempo edits.
- [x] The complete phoneme → unit → timing → render pipeline runs from a snapshot.

### Cache and scheduler

- [x] PCM cache supports memory and disk tiers.
- [x] PCM payloads are versioned and checksummed.
- [x] Non-finite, truncated, and corrupted payloads are rejected.
- [x] Scheduler supports priority ordering.
- [x] Newer phrase revisions cancel or stale older work.
- [x] Cache hits bypass render callbacks.
- [x] Identical PCM content can be reused by a newer revision.
- [x] Stale PCM remains readable until newer publication.
- [x] SPSC audio ring preserves ordering across wrap-around.

### Verification

- [x] Debug build with warnings as errors.
- [x] Release build with warnings as errors.
- [x] ASan and UBSan build/tests.
- [x] Direct test binary reports zero failures.
- [x] End-to-end Phase 3 demo writes real PCM and project/bank evidence.
- [x] Branch policy reports only `master`.
- [x] License audit passes.
- [x] ZIP extraction, rebuild, and test pass before delivery.

## Explicitly not accepted as complete

- [ ] Native iPlug2 + Skia editor window.
- [ ] Native Windows/macOS IME overlay.
- [ ] Voicebank Studio GUI and recording transport.
- [ ] SpectralClassic renderer.
- [ ] Signalsmith Stretch adapter.
- [ ] Official human-recorded voicebank.
- [ ] Physical audio-device callback integration.
- [ ] CLAP, VST3, or AU target.
