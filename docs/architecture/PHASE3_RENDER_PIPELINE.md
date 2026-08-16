# Phase 3 Render Pipeline

## 1. Scope

Phase 3 turns the Phase 2 offline raw-synthesis prototype into an editable and schedulable phrase-rendering foundation. It introduces persistent render intent, Classic PSOLA, phrase-scoped snapshots, a content-addressed PCM cache, background scheduling, stale-audio publication, and an SPSC playback handoff.

The native window and physical audio device remain outside this phase. The implemented code is the production-facing library path that those adapters will call.

## 2. End-to-end flow

```text
Project revision
  │
  ├─ PhraseSegmenter
  │    └─ stable PhraseSegment IDs
  │
  ├─ DirtyPhraseInvalidator
  │    └─ changed phrase plus adjacent boundaries
  │
  ├─ RenderSnapshotFactory
  │    ├─ phrase-scoped project copy
  │    ├─ presentation state removed
  │    ├─ relevant tempo/meter prefix retained
  │    ├─ surrounding pitch anchors retained
  │    └─ content hash generated
  │
  ├─ BackgroundRenderScheduler
  │    ├─ revision and priority arbitration
  │    ├─ old-job cancellation
  │    ├─ cache lookup
  │    └─ worker execution
  │
  ├─ PhraseRenderPipeline
  │    ├─ Japanese phonemizer
  │    ├─ deterministic unit selection
  │    ├─ vowel-onset timing solver
  │    ├─ Raw/PSOLA renderer dispatch
  │    └─ per-boundary SeamComposer
  │
  ├─ PcmCache
  │    ├─ versioned binary payload
  │    ├─ finite-sample validation
  │    └─ checksum validation
  │
  ├─ StaleWhileRenderStore
  │    └─ previous PCM stays readable until publication
  │
  └─ SpscAudioRingBuffer
       └─ future playback feeder → audio callback handoff
```

## 3. Phrase segmentation

A phrase is a stable group of notes within one vocal region. Splits occur when:

- a positive rest is at least `splitRest`;
- adding a note would exceed `maximumDuration`;
- the region boundary is reached.

The phrase ID hashes:

- region ID;
- phrase start and end ticks;
- ordered note IDs.

This ID is stable for unchanged phrase membership and does not depend on viewport state or process-local addresses.

Dirty invalidation returns every intersecting phrase and, by default, the immediate left and right neighbors. Neighbor invalidation is required because preutterance, release, and seam overlap may cross a phrase boundary.

## 4. Immutable phrase-scoped snapshots

`RenderSnapshotFactory` validates the full project and voicebank, then builds an immutable audio-only project slice. It retains:

- the selected track;
- the selected phrase's notes and referenced lyrics;
- phoneme, unit-selection, and seam overrides belonging to those notes;
- pitch points inside the phrase plus the nearest interpolation anchors around it;
- tempo and meter events that can affect time conversion up to the phrase end;
- the selected voicebank manifest, style, quality, sample rate, and engine version.

It deliberately normalizes or removes:

- project title and project ID;
- track and region display names;
- character reference and display mode;
- snap state and snap grid;
- mute/solo presentation state;
- unrelated tracks and phrases;
- tempo and meter events after the phrase.

The resulting cache identity changes when phrase audio can change and remains stable for unrelated presentation edits.

## 5. Persistent render intent

Phase 3 stores only deliberate user intent.

### Unit selection override

A `PhonemeKey`, token count, unit ID, renderer selection, and lock state force a specific sample coverage. Automatic candidates are still regenerated for all uncovered tokens.

### Seam override

A boundary identified by the first phoneme of the incoming unit can override:

- seam amount;
- maximum overlap in microseconds;
- crossfade curve;
- phase-reset amount;
- spectral-envelope blend amount;
- lock state.

### Pitch automation

Region-relative cents are sampled into each unit's destination frame range. The renderer receives a compact per-unit pitch curve with points at the unit start, internal automation points, and unit end.

All three types have undoable application commands and schema-3 persistence.

## 6. Classic PSOLA

### Preconditions

Classic PSOLA requires:

- valid unit markers;
- at least three valid editable pitch marks;
- a nonempty source WAV;
- a valid output sample rate and frame count;
- a target MIDI note and valid pitch curve.

### Source treatment

The renderer first creates a deterministic fallback waveform:

- consonant and transition regions are copied/resampled from the source;
- the stable vowel uses the declared loop;
- the release region preserves source transient/noise identity.

PSOLA replaces only the voiced sustain region. This preserves the sample-based character at consonants and releases.

### Grain synthesis

For every output mark:

1. select a source pitch mark from the stable region;
2. derive a bounded local source period;
3. extract a symmetric windowed grain;
4. overlap-add into the destination sustain;
5. advance by the target period computed from base note, pitch automation, and source-residual policy.

Cancellation is checked during fallback generation and pitch-mark synthesis. Non-finite or structurally invalid input fails explicitly.

## 7. Renderer dispatch

`UnitRendererDispatcher` resolves the renderer from:

1. an explicit unit-selection override;
2. a forced render policy;
3. the voicebank unit hint.

Implemented:

- `Raw`;
- `ClassicPsola`.

Not yet implemented:

- `SpectralClassic`;
- `Stretch`.

When Raw fallback is permitted, the dispatcher returns both requested and actual renderer plus a diagnostic string. When fallback is disabled, it returns `Unsupported`. The caller can therefore display the real execution path.

## 8. Seam composition

The incoming unit's boundary settings are applied after unit rendering. Operations include:

- overlap limit;
- smooth, linear, equal-power, or hard-character crossfade;
- phase reset by blending toward the incoming unit's original phase relationship;
- envelope blend over the overlap;
- minimum de-click behavior.

Unit alignment is recalculated from the rendered unit's vowel-onset offset so renderer changes do not break note-on alignment.

## 9. PCM cache

The PCM cache key is a stable content hash, not a project revision. The binary format contains:

```text
magic
format version
sample rate
start frame
frame count
PCM checksum
little-endian float payload
```

Validation includes:

- restricted cache-key characters;
- sample-rate bounds;
- nonzero bounded frame count;
- truncated-header and payload checks;
- finite float validation;
- checksum validation.

Memory entries are immutable shared objects. Disk writes use a uniquely named temporary file followed by replacement. Cache corruption returns a parse error and increments the corruption statistic; it does not propagate invalid PCM.

## 10. Scheduler semantics

The scheduler owns a fixed pool of `std::jthread` workers. Requests contain:

- phrase ID;
- content cache key;
- project revision;
- sample rate;
- priority;
- cancellation-aware render task.

Rules:

- a lower revision than the latest known revision completes immediately as `Stale`;
- submitting a newer request cancels the active/queued control for the same phrase;
- cache hits bypass the render callback;
- stale workers are prevented from storing PCM;
- completion status is one of `Completed`, `CacheHit`, `Cancelled`, `Stale`, or `Failed`;
- queued jobs are ordered by priority and then FIFO sequence.

Revision is intentionally absent from `CachedPcm`. A newer revision may publish identical cached content without rewriting it.

## 11. Stale-while-render and playback handoff

`StaleWhileRenderStore` holds the most recently published immutable PCM and its publication revision. `markDirty()` does not remove the audio. A newer accepted publication clears the dirty flag. Older publications are rejected.

`SpscAudioRingBuffer` is preallocated and performs no allocation during `write`, `read`, or `clear`. It is the handoff primitive for a future playback-feeder thread and physical audio callback. The Phase 3 demo exercises this primitive, but it is not yet connected to iPlug2 or an operating-system device.

## 12. Failure policy

The pipeline does not silently repair structural problems.

- missing voicebank units: error;
- invalid pitch marks: PSOLA unsupported or explicit fallback;
- invalid cache payload: parse error;
- render cancellation: conflict/cancelled completion;
- stale revision: stale completion with no PCM;
- unsupported language: explicit unsupported error;
- unsupported renderer with fallback disabled: explicit unsupported error.

## 13. Remaining work

- native editor integration and real transport;
- playback feeder that streams multiple cached phrases with crossfades;
- true SpectralClassic renderer;
- Signalsmith-backed Stretch adapter after dependency approval;
- persistent cache eviction policy and size quota;
- official human-recorded voicebank and manual pitch-mark workflow;
- CLAP/VST3/AU host transport integration.
