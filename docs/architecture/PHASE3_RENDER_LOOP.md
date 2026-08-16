# Phase 3 render-loop architecture

## 1. Purpose

Phase 3 converts persistent editor decisions into cancellable phrase PCM without allowing render workers to read mutable UI state.

```text
Editor command
  → project revision
  → phrase segmentation / dirty invalidation
  → immutable phrase snapshot
  → content-addressed cache lookup
  → prioritized render job
  → complete phrase pipeline
  → cache publication
  → stale-while-render replacement
  → SPSC feeder buffer
  → audio callback
```

## 2. Canonical versus derived state

Canonical state:

- notes and lyrics;
- phoneme overrides;
- forced unit selection and renderer override;
- per-boundary seam override;
- pitch automation;
- voicebank reference.

Derived state:

- generated phoneme sequence;
- unit candidates and selected plan when no forced override exists;
- timing placements;
- rendered unit PCM;
- phrase PCM and cache files.

The render cache can be removed without damaging a project.

## 3. Phrase segmentation

`PhraseSegmenter` sorts notes deterministically and starts a new phrase when:

- the positive rest is at least `splitRest`; or
- adding a note would exceed `maximumDuration`.

Phrase IDs hash region ID, phrase boundaries, and note IDs. `DirtyPhraseInvalidator` includes intersecting phrases and, by default, their immediate neighbours because preutterance, release, and context can cross a phrase boundary.

## 4. Immutable snapshot

`RenderSnapshotFactory` extracts a phrase-only `Project` copy. It retains only the time-map history and canonical state that can affect the phrase. A content key includes:

```text
snapshot format tag
engine version
render quality
phrase identity and bounds
sample rate
voice style
canonical phrase-project JSON
voicebank manifest JSON
```

The project revision is carried separately for stale-result rejection.

## 5. Production phrase pipeline

`PhraseRenderPipeline` currently supports Japanese voicebanks and performs:

1. `JapaneseKanaPhonemizer`;
2. `DeterministicUnitSelector` with explicit overrides;
3. `TimingSolver` with vowel-onset alignment;
4. `ConcatenativePhraseRenderer`;
5. `UnitRendererDispatcher` per selected unit;
6. `SeamComposer` per boundary.

Cancellation is checked before and between major stages and periodically inside Classic PSOLA.

## 6. Scheduler

`BackgroundRenderScheduler` owns 1–16 `std::jthread` workers. Jobs are ordered by:

```text
Playhead > Next > Selected > Viewport > Background
```

Equal-priority jobs preserve submission order. A new revision for a phrase requests cancellation of the previous control. Older submissions and older completions are reported as `Stale` instead of replacing the latest phrase.

The completion state is one of:

- `Completed`;
- `CacheHit`;
- `Cancelled`;
- `Stale`;
- `Failed`.

## 7. PCM cache

`PcmCache` stores mono float PCM in a bounded first-party binary format containing:

- magic and schema version;
- sample rate;
- phrase start frame;
- frame count;
- stable checksum;
- little-endian IEEE float samples.

Keys accept only bounded alphanumeric, hyphen, and underscore input. Reads reject unsupported sample rates, oversized payloads, truncation, non-finite samples, and checksum mismatch. Memory entries can be evicted while disk entries remain reusable.

The cache format is not a signed distribution format and must not be trusted as an authenticity mechanism.

## 8. Stale-while-render playback

`StaleWhileRenderStore` preserves the most recent PCM and marks it dirty while a newer render is pending. Publishing an older revision is rejected. This keeps playback available during edits without confusing old audio with the newest revision.

## 9. Audio transfer

`SpscAudioRingBuffer` is preallocated and supports one feeder thread and one audio callback. Read underflow is zero-filled. The Phase 3 demonstration validates wrapping and ordering, but the ring buffer is not yet connected to the deferred native iPlug2 callback.

## 10. Current limits

- No spectral or Signalsmith backend is implemented.
- No native editor or production playback feeder exists.
- The cache is local disposable state, not a project container.
- Multi-channel audio is not represented by the Phase 3 ring buffer.
- Only the Japanese phonemizer is connected to the production phrase pipeline.
