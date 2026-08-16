# Project SEAM Phase 3 Implementation Report

**Implementation status:** Complete for the Phase 3 scope defined in this repository
**Branch policy:** `master` only
**Repository version:** `0.3.0`
**Verification date:** 2026-08-16

## 1. Executive summary

Phase 3 turns the Phase 2 phoneme/voicebank/raw-synthesis vertical slice into a production-facing phrase-render foundation. The project now persists deliberate sample and seam choices, renders voiced sustain with an editable Classic PSOLA path, creates immutable phrase-scoped render snapshots, performs revision-aware priority scheduling, stores validated PCM in a content-addressed cache, keeps stale audio available during background work, and hands precomputed samples to a preallocated SPSC ring buffer.

The implemented end-to-end path is:

```text
Project JSON schema 3
→ phrase segmentation
→ immutable phrase-scoped snapshot
→ Japanese phonemizer
→ automatic plus forced unit selection
→ vowel-onset timing
→ per-unit Raw or Classic PSOLA render
→ per-boundary seam operations
→ phrase PCM
→ versioned memory/disk cache
→ stale-while-render publication
→ SPSC playback handoff
```

The Phase 3 demo uses a deterministic synthetic voicebank with editable pitch marks. It forces one sample variant, mixes Raw and Classic PSOLA units in the same phrase, applies region pitch automation and a custom incoming seam, cancels an obsolete render, publishes the newer result, proves a cache hit, and reads the result through the audio ring buffer.

This is not yet a native graphical application or a commercially releasable voice product. The production iPlug2/Skia window, operating-system IME overlay, official human-recorded bank, physical audio-device integration, SpectralClassic, Stretch, and plugin formats remain explicitly deferred.

## 2. Branch and ownership policy

All implementation work remains on the single local branch:

```text
master
```

The repository policy scripts and Git hooks reject non-`master` branches. Every Phase 3 commit is authored by:

```text
julymeltdown <quintuplets2000@gmail.com>
```

The final commit list is stored in `docs/phase3/evidence/git-history.txt` and in the delivered `.git` directory.

## 3. Canonical project-state additions

### 3.1 Unit-selection override

`UnitSelectionOverride` records deliberate user intent that automatic planning must not erase:

```text
start PhonemeKey
covered token count
voicebank Unit ID
renderer choice
lock state
```

The key is based on note ID plus generated-phoneme ordinal. This allows the selection to survive re-planning as long as the relevant phoneme remains. The selector validates that the forced unit covers the requested exact phone sequence. Invalid or missing forced units fail explicitly rather than falling back to an unrelated sample.

### 3.2 Seam override

`SeamOverride` identifies the incoming unit by its first phoneme and may override:

- seam amount;
- overlap in microseconds;
- crossfade curve;
- phase-reset amount;
- envelope-blend amount;
- lock state.

The fields are optional so a boundary can inherit defaults while overriding only one characteristic. Numeric ranges and key validity are checked by domain validation.

### 3.3 Pitch automation

`PitchAutomation` stores ordered, unique region-relative points:

```text
integer tick
signed cents
step / linear / smooth interpolation
```

The synthesis layer maps relevant points to each rendered unit's destination-frame range. Start and end values are sampled even when no explicit point lies exactly on the unit boundary.

### 3.4 Reversible commands

The application layer adds commands for:

- upserting and removing a unit-selection override;
- upserting and removing a seam override;
- upserting and removing a pitch-automation point.

Each command captures the previous state and supports undo/redo through the existing `EditorSession`. Note deletion removes dependent phoneme, unit, and seam overrides; undo restores them with the note and lyric.

## 4. Project JSON schema 3

Schema 3 adds the following required region fields:

```text
unitSelectionOverrides
seamOverrides
pitchAutomation
```

The reader accepts schemas 1, 2, and 3.

- schema 1 receives empty phoneme and render-control collections;
- schema 2 retains phoneme overrides and receives empty Phase 3 collections;
- schema 3 requires every canonical collection to be present and correctly typed;
- unknown future schemas are rejected.

Round-trip tests cover unit ID, renderer choice, optional seam fields, interpolation type, strong IDs, Unicode lyrics, and equality of the loaded canonical model.

Detailed format documentation is in `docs/formats/PROJECT_JSON_V3.md`.

## 5. Voicebank manifest schema 2

### 5.1 Editable pitch marks

Each voicebank unit can now store an ordered array of `PitchMark` values:

```text
source sample frame
confidence
manual lock state
```

Marks are source-WAV frame positions. Validation rejects:

- unordered or duplicate marks;
- positions outside the declared audio range;
- non-finite or out-of-range confidence;
- invalid edit indices.

### 5.2 Generation and editing

The Phase 3 voicebank layer provides:

- deterministic generation from F0 analysis and local waveform refinement;
- validation against source ranges;
- add;
- move;
- remove;
- lock/unlock.

Manual lock state is preserved in schema 2 and is intended for the later Voicebank Studio pitch-mark editor.

### 5.3 Migration

Schema 1 manifests load with empty pitch-mark collections. The bank remains structurally valid and can continue to use Raw rendering. Classic PSOLA requires sufficient valid marks and either returns `Unsupported` or follows the explicitly configured Raw fallback policy.

Detailed format documentation is in `docs/formats/VOICEBANK_MANIFEST_V2.md`.

## 6. Classic PSOLA renderer

### 6.1 Scope

`ClassicPsolaRenderer` is a deterministic, sample-based pitch renderer. It does not generate a new voice with a neural model. The original sample remains the source of consonants, transitions, sustain grains, release, phase character, and noise.

### 6.2 Input checks

The renderer rejects:

- missing or empty source audio;
- unsupported sample rates;
- invalid output lengths or MIDI values;
- invalid unit markers;
- invalid pitch curves;
- fewer than three valid pitch marks;
- non-finite controls.

### 6.3 Source-preserving fallback waveform

Before PSOLA overlap-add, the renderer constructs a deterministic source-derived waveform:

- the consonant and transition use direct source interpolation;
- the stable vowel follows the declared loop;
- the release uses the original release range;
- output length and vowel-onset offset are preserved.

This waveform remains outside the voiced sustain replacement, which prevents PSOLA from turning unvoiced consonants into pitched material.

### 6.4 Grain synthesis

The voiced sustain uses pitch-mark-centered windowed grains. The local source period is bounded around the median period to avoid isolated marker errors causing extreme grain sizes. Output marks advance according to:

```text
base target MIDI
+ per-frame pitch curve
+ configured source-pitch residual
```

Overlap weights normalize the voiced replacement. Cancellation is checked during long loops. The result is finite and frame-stable.

### 6.5 Product character

The implementation deliberately does not perform:

- neural reconstruction;
- phrase-level formant matching;
- automatic spectral boundary optimization;
- automatic source-pitch flattening;
- hidden unit substitution.

The resulting PSOLA path is cleaner and more controllable than raw resampling while retaining sample identity and join character.

## 7. Renderer dispatcher

`UnitRendererDispatcher` resolves the requested renderer from the voicebank hint, global render policy, and explicit user override.

Implemented renderers:

```text
Raw
Classic PSOLA
```

Reserved but not implemented:

```text
SpectralClassic
Stretch
```

When `allowRawFallback` is enabled, an unsupported or unusable requested renderer may produce Raw output. The result includes:

- requested renderer;
- actual renderer;
- fallback flag;
- diagnostic text.

When fallback is disabled, the same condition returns a visible error. The system never reports fallback output as if the requested renderer ran.

## 8. Production phrase renderer

`ConcatenativePhraseRenderer` consumes the deterministic unit plan and timing plan and then:

1. loads each referenced WAV through a per-render audio cache;
2. resolves the forced or inherited renderer;
3. maps pitch automation to unit-relative frame points;
4. renders the unit;
5. realigns its rendered vowel onset to the timing plan's desired onset;
6. resolves the incoming boundary override;
7. records placement diagnostics;
8. composes all units through `SeamComposer`.

A phrase can therefore contain Raw and Classic-PSOLA units simultaneously. The Phase 3 demo uses three PSOLA placements and one Raw placement.

## 9. Seam operations

Phase 2 exposed a basic smooth-to-hard join. Phase 3 adds explicit boundary policy:

- smooth curve;
- linear curve;
- equal-power curve;
- hard-character curve;
- maximum overlap;
- phase reset;
- envelope blend;
- default de-click protection.

The composer does not change the semantic unit plan or move phoneme boundaries in search of naturalness. It operates at the positions selected by the timing and render stages.

## 10. Phrase segmentation and dirty invalidation

`PhraseSegmenter` forms deterministic note groups based on rest length and maximum duration. Phrase identity hashes the region, bounds, and note IDs.

`DirtyPhraseInvalidator` identifies every intersecting phrase and can include immediate neighbors. Neighbor invalidation is the safe default because preutterance, release, and seam overlap cross logical note and phrase boundaries.

## 11. Immutable render snapshots

### 11.1 Reason

Background workers must not observe mutable editor objects. At the same time, hashing the entire project would invalidate every phrase after a harmless title, character, or unrelated-note edit.

### 11.2 Phrase-scoped extraction

The snapshot factory copies only the selected track and phrase. It retains:

- selected notes and referenced lyrics;
- relevant phoneme, unit, and seam overrides;
- pitch automation inside the phrase plus surrounding interpolation anchors;
- tempo and meter events that can affect the phrase;
- sample rate, style, quality, engine version, and voicebank manifest.

It removes or normalizes:

- project title and display ID;
- track and region display names;
- character reference and display mode;
- snap settings;
- mute/solo presentation flags;
- unrelated tracks and phrases;
- later tempo and meter events.

### 11.3 Content identity

The stable content hash includes the canonical phrase project, voicebank manifest, segment identity, engine version, render quality, style, and sample rate. Tests prove that:

- changing a note in the phrase changes the hash;
- changing a different phrase does not;
- renaming the project/track/region does not;
- character and snap changes do not;
- a tempo event after the phrase does not;
- a tempo event inside the phrase does.

## 12. Complete snapshot render pipeline

`PhraseRenderPipeline` performs the complete worker-side operation from a snapshot:

```text
snapshot validation
→ Japanese Kana phonemizer
→ deterministic unit selector
→ timing solver
→ concatenative phrase renderer
→ phrase PCM and diagnostics
```

The pipeline currently rejects non-Japanese voicebanks explicitly. It checks cancellation between major stages and passes the stop token into unit rendering.

## 13. Content-addressed PCM cache

### 13.1 Payload

`CachedPcm` contains only:

```text
sample rate
absolute start frame
float samples
```

Project revision is deliberately separate.

### 13.2 Binary format

The versioned `.spcm` format stores:

- magic;
- format version;
- sample rate;
- start frame;
- frame count;
- stable checksum;
- little-endian float payload.

### 13.3 Validation and safety

The cache rejects:

- malformed keys and path traversal;
- unsupported sample rates;
- empty or excessive payloads;
- non-finite samples;
- invalid header versions;
- truncated payloads;
- checksum mismatch.

Memory hits return immutable shared objects. A memory clear followed by load exercises the disk path. Writes use a unique temporary file and replacement. Corruption increments an explicit statistic and never returns invalid PCM.

## 14. Background render scheduler

### 14.1 Request

Every request supplies:

- phrase ID;
- content cache key;
- project revision;
- sample rate;
- priority;
- cancellation-aware task.

### 14.2 Priority

```text
Playhead
Next
Selected
Viewport
Background
```

Within the same priority, sequence order is FIFO.

### 14.3 Revision behavior

- an older request than the latest known revision completes immediately as stale;
- a newer request requests cancellation of existing work for that phrase;
- a worker checks freshness before storing PCM;
- stale output is never written into the content cache;
- completion reports completed, cache hit, cancelled, stale, or failed.

### 14.4 Cache reuse

The scheduler checks the cache before queueing a worker. A valid hit bypasses the task. Because revision is not part of `CachedPcm`, the same content can be published under a newer revision. Tests verify that the callback is not executed on such reuse.

## 15. Stale-while-render store

The stale-audio store separates the latest published revision from immutable PCM content.

```text
publish revision N
→ mark dirty after an edit
→ continue reading revision N PCM
→ render revision N+1
→ publish N+1
→ clear dirty flag
```

A publication older than the current revision is rejected. The store does not infer revision from the cache object.

## 16. SPSC audio ring buffer

The ring buffer is allocated once and supports a single producer and single consumer. It:

- preserves sample order across wrap-around;
- reports readable and writable frames;
- zero-fills the unread portion of an output span;
- performs no allocation in `write`, `read`, or `clear`.

The Phase 3 demo writes cached phrase PCM and reads a simulated callback block. A real audio-device callback is not yet connected.

## 17. End-to-end demonstration

`seam_phase3_demo` performs the following:

1. writes five synthetic source WAV units;
2. creates voicebank schema 2 with pitch marks;
3. creates four notes and Japanese lyrics;
4. applies a forced sample/renderer override;
5. adds a custom seam override;
6. creates three pitch-automation points;
7. phonemizes and selects units;
8. renders a Raw reference phrase;
9. renders the mixed production phrase;
10. writes waveform and spectrogram evidence;
11. creates a phrase snapshot and content hash;
12. submits and cancels an obsolete render;
13. submits the current production render;
14. publishes it over stale Raw PCM;
15. proves a content-cache hit;
16. moves PCM through the SPSC buffer;
17. saves and reloads project schema 3;
18. generates an editor/render evidence SVG and structured summary.

The current committed evidence is regenerated by `scripts/generate_phase3_evidence.py`.

## 18. Test coverage

The directly executed test binary reports:

```text
55 passed, 0 failed
```

Phase 3-specific tests include:

- persistent render commands and deletion cleanup;
- project schema 3 round trip and migration;
- pitch-mark generation and editing;
- forced unit selection;
- Classic PSOLA pitch behavior and finite output;
- real phase-reset and envelope-blend operations;
- explicit renderer fallback;
- stable segmentation and neighbor invalidation;
- phrase-scoped snapshot hash behavior;
- complete immutable-snapshot pipeline;
- memory/disk cache persistence;
- non-finite, checksum-corrupt, and truncated cache rejection;
- scheduler cancellation, priority, stale rejection, and cache reuse;
- stale-audio publication;
- SPSC wrap-around ordering.

## 19. Performance evidence

The benchmark executable measures:

- Classic PSOLA iterations and real-time multiple;
- seam-composition iterations;
- forced disk-cache reads after memory eviction;
- scheduler completion throughput.

Values are machine-specific regression evidence. The generated JSON is stored at `docs/phase3/evidence/phase3-benchmark.json`.

## 20. Security and data boundaries

No voicebank or project file can execute code. Phase 3 retains the data-only bank policy. The PCM cache restricts keys and validates payload bounds. The render snapshot owns immutable copies rather than references into mutable UI state. Cache corruption is isolated as recomputable derived data.

Still deferred:

- signed `.seambank` packaging;
- compressed-package extraction limits;
- persistent cache quota/eviction;
- native crash handling and installer signing.

## 21. Honest limitations

The following are not claimed as implemented:

- iPlug2 + Skia production window;
- native Windows TSF or macOS text overlay;
- Voicebank Studio GUI;
- recording transport;
- actual SpectralClassic synthesis;
- Signalsmith Stretch integration;
- official licensed human voicebank;
- physical audio callback and multi-phrase playback feeder;
- CLAP, VST3, or AU;
- cache eviction policy;
- voicebank package signatures.

The current SVG is evidence generated from actual domain and render data, not a native-app screenshot. The current WAV is a real render from synthetic unit files, not a prerecorded commercial voice.

## 22. Phase 3 completion judgment

The phase is complete for its defined technical scope because:

1. user sample, renderer, seam, and pitch choices are canonical and reversible;
2. a sample-based Classic PSOLA path is implemented and tested;
3. an immutable phrase can run through the full synthesis pipeline on a worker;
4. stale revisions cannot replace newer PCM;
5. unchanged audio content can be reused across revisions;
6. cached PCM can be handed to a future callback without project traversal;
7. evidence is reproducible under debug, release, and sanitizer builds;
8. the repository remains on `master` only.
