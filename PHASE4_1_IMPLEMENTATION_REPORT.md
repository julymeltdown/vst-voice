# Project SEAM Phase 4.1 Stabilization Implementation Report

**Implementation status:** Complete for the Phase 4.1 stabilization scope
**Repository version:** `0.4.1`
**Branch policy:** `master` only
**Date:** 2026-08-16

## 1. Purpose

Phase 4 established four concrete sample renderers, inspection models, callback-ready playback handoff, and a bounded disposable PCM cache. A subsequent code review found that the feature surface was real but identified correctness and durability defects that would become substantially harder to fix after attaching a native window and physical audio device.

Phase 4.1 is therefore a stabilization release rather than a feature-expansion release. It closes the review's release-blocking findings before Phase 5 begins.

The stabilization objectives are:

```text
correct rendered-audio cache identity
+ generated render ABI revisions
+ durable project and manifest persistence
+ bounded untrusted input parsing
+ consistent DSP transition boundaries
+ SPSC-safe playback transport control
+ final scheduler stale-result gating
+ transaction-safe editor commands
+ regression and concurrency verification
```

Phase 4.1 does not claim the native iPlug2/Skia editor, OS IME integration, physical audio device, graphical Voicebank Studio, human-recorded commercial voicebank, signed package format, or plugin targets.

## 2. Review findings closed

| Review finding | Resolution |
|---|---|
| CR-001 cache key omitted WAV bytes and effective render settings | Render identity v3 hashes selected WAV SHA-256, selected Unit metadata, effective render/seam options, phrase state, and algorithm revisions |
| CR-002 hard-coded `0.3.0` engine identity | CMake now generates application version, render ABI, component revisions, and PCM cache format revision |
| IO-001 remove-before-rename save sequence | Common durable atomic writer with same-filesystem temporary file, durable flush, atomic replacement, parent directory sync, backup, and fault injection |
| DSP-001 Spectral/Stretch skipped recorded vowel transition | Original audio is retained through `stableStart`; only stable vowel material is transformed |
| DSP-002 phase-alignment offset ended at overlap boundary | Selected source alignment remains consistent for the incoming Unit after overlap; equal-power crossfade is corrected |
| RT-001 feeder transport state was data-racy | Fixed-capacity SPSC control queue, feeder-owned mutable state, atomically published observations, consumer-owned ring reset epochs |
| RT-002 stale scheduler result could be published after a newer revision | Final revision gate after cache publication and another gate when enqueuing completion |
| SEC-001 JSON/PCM resource limits were incomplete | Bounded file reads, JSON depth/node/string/collection limits, exact payload-size verification before PCM allocation |
| SEC-002 symlink escape from voicebank root | Canonical bank asset resolver rejects absolute, dot, parent, symbolic-link, non-regular, and canonical-escape paths |
| APP-001 command rollback failure could leave partial state | Project snapshot transactions restore exact state on apply, validation, undo, redo, and composite-command failure |

## 3. Generated build and render identity

### 3.1 Single version source

`CMakeLists.txt` now declares project version `0.4.1` and generates:

```text
seam/build/version.hpp
├── kApplicationVersion
├── kRenderAbiId
├── kPhonemizerRevision
├── kUnitSelectorRevision
├── kTimingSolverRevision
├── kRawRendererRevision
├── kPsolaRendererRevision
├── kSpectralRendererRevision
├── kStretchRendererRevision
├── kSeamComposerRevision
└── kPcmCacheFormatRevision
```

The current render ABI is:

```text
seam-render-abi-4.1-r1
```

Application version and render ABI are intentionally separate. A packaging-only application update need not invalidate audio, while a DSP, planning, timing, or serialization change can increment its specific revision or the aggregate ABI.

### 3.2 Render identity v3

The previous identity hashed phrase project JSON and the complete voicebank manifest. It did not bind the actual selected WAV bytes or all effective renderer settings. This could reuse stale audio after a WAV replacement and could invalidate unrelated phrases after editing an unused Unit.

The new SHA-256 identity contains:

```text
identity schema tag
render ABI and algorithm revisions
phrase-scoped canonical project JSON
render quality
sample rate
selected style
voicebank identity and language contract
effective renderer policy and fallback policy
Raw / PSOLA / Spectral / Stretch parameters
Pitch Curve points and interpolation
Default Seam settings
selected Unit plan entries
selected Unit acoustic markers and Pitch Marks
selected WAV SHA-256 for every planned Unit
```

It deliberately excludes unrelated voicebank Units and presentation-only project state.

### 3.3 Immutable planning contract

`RenderSnapshotFactory` now performs phonemization and deterministic Unit selection while constructing the snapshot. For each selected path it reads a bounded byte buffer once, computes SHA-256 from those bytes, decodes the same bytes, and freezes the resulting `AudioBuffer` in the snapshot. The snapshot therefore owns the exact immutable `PhonemeResult`, `UnitPlan`, render options, selected audio identities, and decoded Unit audio that produced its cache identity.

The production render pipeline consumes those stored objects rather than reopening the WAV or recomputing a potentially different plan after cache lookup. This closes the hash-versus-render time-of-check/time-of-use gap: the bytes whose digest forms the cache identity are the bytes rendered by the worker.

## 4. SHA-256 and voicebank asset identity

A first-party SHA-256 implementation was added to `seam-core` for strings, byte spans, and bounded files. Published empty-string and `abc` vectors are covered by tests.

Selected voice assets are resolved through `resolveBankAsset` before hashing or decoding. The resolver rejects:

- empty or absolute paths;
- `.` and `..` components;
- symbolic links in any existing path component;
- non-regular final files;
- canonical paths outside the canonical bank root.

This closes both cache-identity ambiguity and direct-directory symlink escape in the current data-only voicebank model.

## 5. Durable persistence

### 5.1 Common writer

`seam-core/file_io` provides bounded file reads and a common durable atomic writer.

POSIX sequence:

```text
create same-directory unique temporary file
→ write all bytes
→ fsync temporary file
→ optional durable backup of previous target
→ atomic rename over target
→ fsync parent directory
```

Windows sequence:

```text
CreateFileW with write-through
→ WriteFile all bytes
→ FlushFileBuffers
→ optional durable backup
→ MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)
```

Project JSON and voicebank manifests use this writer and retain a `.bak` generation when replacing an existing file. PCM cache entries use the same atomic replacement primitive without canonical-project backup because the cache is reproducible and disposable.

### 5.2 Fault injection

The writer exposes deterministic fault stages:

```text
TemporaryCreated
TemporaryWritten
TemporarySynced
BackupCommitted
BeforeReplace
Replaced
DirectorySynced
```

Regression tests verify that failure before replacement leaves the old target and backup intact, while a reported failure after replacement leaves the newly committed target plus the old backup rather than an absent file.

## 6. Bounded JSON and exact numeric representation

The internal JSON value type now distinguishes signed 64-bit integers from floating-point numbers. Musical ticks, sample frames, identifiers, and schema values can round-trip exactly through the full `int64_t` range rather than passing through `double`.

`JsonParseLimits` bounds:

```text
input bytes
maximum nesting depth
maximum node count
maximum string bytes
maximum entries per array or object
```

Additional parser hardening includes:

- complete UTF-16 surrogate-pair decoding for JSON escapes;
- rejection of lone high or low surrogates;
- rejection of duplicate object keys;
- rejection of integers outside `int64_t` and non-finite numeric results;
- bounded Project and Voicebank Manifest file reads.

The Project codec currently permits up to 64 MiB, and the Voicebank Manifest codec up to 32 MiB. These limits are product safety boundaries, not target normal file sizes.

## 7. PCM cache format v3

PCM cache entries now use magic `SEAMPCM4` and generated format revision `3`.

Before allocating sample memory, cache loading verifies:

```text
file size >= fixed header
magic and format version
sample-rate range
frame-count maximum
payload bytes <= configured maximum entry size
expected total file size == actual file size
finite float values
content checksum
```

This prevents a short malicious or corrupt file from declaring hundreds of millions of frames and forcing a large allocation before truncation is discovered.

Cache writes are encoded into a precisely sized byte buffer and committed through the durable atomic writer. Existing memory/disk byte budgets, entry-count budget, eviction, corruption counters, and path-safe content keys remain active.

## 8. DSP boundary corrections

### 8.1 SpectralClassic and Stretch transition preservation

Both renderers previously copied the source only through `vowelOnset`, then began transformed stable material. The recorded `vowelOnset → stableStart` transition was therefore replaced even though the renderer contract said that only stable vowel material should be transformed.

The corrected structure is:

```text
Audio Offset → Stable Start       copied from original recording
Stable Start → Release Start      transformed stable-vowel material
Release Start → Audio End         copied from original release
```

`vowelOnsetOffset` remains tied to the actual recorded vowel onset for timing alignment.

### 8.2 Seam phase continuity

Phase alignment can choose a shifted incoming source position during the overlap. Previously that offset ended immediately after the overlap, potentially creating a second discontinuity at the overlap exit.

Phase 4.1 keeps the selected incoming source offset consistent through the remainder of the incoming Unit. The equal-power curve also uses a true sine/cosine gain law rather than linear gains named equal-power.

A regression fixture measures the derivative immediately after overlap and prevents reintroduction of the second click.

## 9. Playback concurrency contract

### 9.1 Control ownership

The UI-facing methods no longer directly mutate feeder-owned transport state. They enqueue immutable commands in a fixed-capacity SPSC queue:

```text
SetTimeline
SetLoop
SetPlaying
Seek
```

Only the feeder thread mutates:

```text
timeline
loop
playhead
playing
pending reset epoch
```

The UI observes playhead and playing through atomics. Statistics use independent atomic counters.

### 9.2 Consumer-owned reset epoch

A seek, timeline replacement, or pause must discard queued stale audio. The producer must not move the consumer's read index.

The ring now uses this handshake:

```text
feeder requests monotonically increasing reset epoch
→ callback consumer observes request
→ callback moves its own read index to current write index
→ callback zero-fills the current output block
→ callback publishes acknowledged epoch
→ feeder resumes only after acknowledgment
```

The implementation is prepared for the dedicated feeder thread and physical callback adapter planned for Phase 5 without violating SPSC index ownership.

## 10. Scheduler final revision gate

A render job can finish and publish its content-addressed cache entry even when a newer project revision has been submitted. Cache storage is safe because the content key is immutable, but the old result must not be presented as current.

Phase 4.1 performs latest-revision checks:

1. before rendering work is accepted as current;
2. after cache publication and optional test hook;
3. while pushing the completion event.

An intentionally blocked race test submits revision 2 after revision 1 writes its cache but before final publication. Revision 1 is reported as stale/cancelled with no PCM pointer, and only revision 2 can complete as current.

## 11. Transaction-safe editor commands

`CompositeCommand` and `EditorSession` now snapshot the complete Project before applying or reverting a transaction.

On any of the following failures, the exact pre-operation Project is restored:

- child command apply failure;
- child command revert failure;
- project validation failure after apply;
- undo failure;
- redo failure.

An undo/redo failure invalidates the affected history safely rather than retaining a command whose internal state may no longer match the restored project.

This is a correctness-first implementation. Future profiling may replace full Project copies with a smaller transactional diff or persistent data structure after equivalent semantics are retained.

## 12. Verification additions

A new `tests/test_stabilization.cpp` suite covers:

1. SHA-256 published vectors;
2. generated application/render/cache identity;
3. exact `int64_t`, Unicode surrogate pairs, and JSON budgets;
4. atomic-write fault injection;
5. selected WAV and effective-option render identity;
6. frozen selected audio remains stable after an on-disk WAV replacement;
7. voicebank symlink escape rejection;
8. PCM declared-payload guard before allocation;
9. Spectral/Stretch transition preservation;
10. seam continuity after overlap;
11. scheduler final-revision race;
12. playback command queue and reset epoch;
13. loop/play-state queued-audio invalidation;
14. threaded feeder/control stress under ThreadSanitizer;
15. command apply/undo transaction restoration.

The named test count increased from 70 to 84.

Build presets now include:

```text
dev               Debug, warnings as errors
release           Release, warnings as errors
sanitize          AddressSanitizer + UndefinedBehaviorSanitizer
thread-sanitize   ThreadSanitizer
```

## 13. Acceptance status

Phase 4.1 is accepted when all of the following are true:

- cache identity changes after selected WAV or effective render option changes;
- unrelated Unit changes do not invalidate the Phrase;
- generated render ABI is used instead of a manually maintained default;
- Project/Manifest writes have durable replacement and backup semantics;
- parser and cache resource guards reject hostile declarations before large allocation;
- source vowel transition is preserved in Spectral and Stretch renderers;
- phase alignment does not create a second overlap-exit discontinuity;
- playback transport changes are communicated by SPSC commands;
- ring reset is consumer-owned and acknowledged by epoch;
- stale scheduler completions cannot be published as current;
- editor command failure restores the exact previous project;
- Debug, Release, ASan/UBSan, and TSan verification pass;
- master-only and license policy checks pass.

The exact verification logs are indexed in `docs/phase4_1/EVIDENCE.md`. The final source-tree verification recorded 84/84 named tests, 6/6 Debug CTest, 6/6 Release CTest, 6/6 ASan+UBSan CTest, and 6/6 ThreadSanitizer CTest.

## 14. Remaining scope after Phase 4.1

The following remain intentionally outside this stabilization release:

- production iPlug2 + Skia native editor window;
- Windows TSF and macOS native IME adapters;
- physical operating-system audio-device adapter;
- continuously running native playback-feeder thread;
- complete graphical Voicebank Studio;
- microphone recording transport;
- true multichannel project routing;
- contracted human-recorded Official Voicebank 01;
- signed and installed `.seambank` distribution format;
- CLAP, VST3, and AU targets;
- persistent decoded-WAV cache across phrase render jobs;
- install-time precomputed voicebank content index and signature.

These are Phase 5 and later product-development tasks. They are not represented as completed by Phase 4.1.
