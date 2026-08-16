# Project SEAM Phase 4 Implementation Report

> **Superseded stability note:** Phase 4 feature delivery remains historically accurate, but its cache identity, persistence, playback ownership, and DSP-boundary contracts were hardened by Phase 4.1. See [`PHASE4_1_IMPLEMENTATION_REPORT.md`](PHASE4_1_IMPLEMENTATION_REPORT.md).

**Implementation status:** Complete for the Phase 4 scope defined in this repository
**Branch policy:** `master` only
**Repository version:** `0.4.0`
**Verification date:** 2026-08-16

## 1. Executive summary

Phase 4 completes the multi-renderer sample-synthesis foundation and connects rendered Phrase PCM to a callback-ready playback path. The project now runs four explicit per-Unit renderers—Raw, Classic PSOLA, SpectralClassic, and Stretch—without representing unimplemented metadata as completed behavior. It also adds a Unit Lane view model, an editable Sample Microscope model, vocal/backing timeline mixing, loop and seek behavior, an allocation-free callback consumer, and bounded memory/disk PCM caching.

The main end-to-end path is:

```text
Project schema 3
→ Japanese phonemizer
→ deterministic plus forced Unit selection
→ vowel-onset Timing Solver
→ per-Unit Raw / PSOLA / Spectral / Stretch dispatch
→ per-boundary Seam composition
→ Phrase PCM
→ revision-aware scheduler and bounded content cache
→ PlaybackTimeline
→ PlaybackFeeder
→ SPSC ring
→ RingBufferAudioProcessor
```

The Phase 4 demo forces four consecutive Units to four different backends, renders one Phrase, creates Raw and mixed-render comparisons, builds a Unit Lane from actual selection/timing/render results, edits an acoustic marker and pitch mark through the Sample Microscope model, persists the edited voicebank manifest, mixes backing audio, and captures PCM from the feeder/ring/callback path with zero underflow. It also forces both memory and disk cache eviction under deliberately small limits.

The repository still does not claim a native iPlug2/Skia application, operating-system IME adapter, physical audio-device attachment, complete Voicebank Studio shell, official human-recorded voicebank, or plugin format. The supplied editor images are evidence renders from actual UI models, not screenshots of a native window.

## 2. Phase 4 scope delivered

### 2.1 Synthesis

- Concrete `SpectralClassicRenderer`.
- Concrete `StretchUnitRenderer`.
- Explicit dispatch for all four renderer identifiers.
- Raw fallback only after an actual backend rejection and an enabled fallback policy.
- Pitch automation forwarded to PSOLA, SpectralClassic, and Stretch.
- Stable-vowel-only quality processing so source consonant/release identity remains visible.

### 2.2 Playback

- `PlaybackTimeline` with absolute-frame clips.
- Multiple overlapping vocal/backing clips.
- Per-clip gain, fade-in, and fade-out.
- `PlaybackFeeder` with preallocated scratch memory.
- Seek, loop, play/pause, watermark filling, and statistics.
- `RingBufferAudioProcessor` implementing the existing callback interface.
- Deterministic zero-fill and underflow accounting.
- PCM16 stereo export support for playback evidence.

### 2.3 Cache governance

- Configurable memory-byte limit.
- Configurable disk-byte limit.
- Configurable disk-entry limit.
- Memory access tracking and eviction.
- Disk oldest-entry pruning.
- Usage and eviction statistics.
- Existing version, checksum, path, finite-sample, and atomic-write protections retained.

### 2.4 Inspection models

- `UnitLaneModel` combines phonemes, Unit plan, solved timing, actual render diagnostics, and timeline geometry.
- `SampleMicroscopeModel` provides waveform columns, spectrogram data, marker visuals, pitch-mark visuals, frame/pixel mapping, and hit testing.
- Acoustic-marker edits are validated by `MarkerEditor`.
- Pitch-mark edits are validated by `PitchMarkEditor`.
- Edited manifest output is demonstrated by the Phase 4 vertical slice.

## 3. SpectralClassic renderer

### 3.1 Purpose

Classic PSOLA depends on reliable periodic pitch marks. Some breathy, noisy, or strongly shifted material needs a different transform. SpectralClassic provides a deterministic source-based fallback without introducing a neural waveform generator.

### 3.2 Boundary-preserving segmentation

The renderer divides each Unit into:

```text
source consonant and transition
stable vowel
source release
```

Only the stable vowel is transformed through STFT analysis/resynthesis. The preceding consonant and final release are interpolated directly from the source WAV. This prevents the spectral stage from smoothing away the entry and exit character that identifies the sample.

### 3.3 Frame analysis

For each stable-vowel output center:

1. A Hann-windowed source frame is sampled from the declared loop.
2. An in-repository radix-2 FFT produces a complex spectrum.
3. Magnitude and a coarse local spectral envelope are measured.
4. The target ratio is calculated from root MIDI, target MIDI, and per-frame pitch automation.
5. Complex harmonic energy is sampled from source-bin positions and moved to target bins.
6. A bounded envelope ratio supplies optional formant preservation.
7. Phase either advances continuously or moves toward source phase according to `phaseReset`.
8. IFFT and weighted overlap-add reconstruct the stable region.

The envelope stage multiplies the shifted spectrum rather than mixing an unshifted spectrum into it. Regression testing specifically guards against retaining an unintended source fundamental.

### 3.4 Controls and validation

Controls:

```text
fftSize
hopSize
formantFollow
phaseReset
additionalGainDb
PitchCurve
```

Validation rejects non-power-of-two FFT sizes, invalid hop sizes, non-finite controls, invalid curves, invalid Unit markers, unsupported pitch ratios, and loops too short for the configured window. Long renders periodically observe a stop token.

### 3.5 Current performance position

SpectralClassic is implemented as a correctness-first renderer. It is not represented as a guaranteed real-time preview backend. The Phase 4 benchmark records actual throughput in the current build and environment. Preview optimization, plan reuse, precomputed windows, and SIMD are future work.

## 4. StretchUnit renderer

### 4.1 Purpose

StretchUnit handles long vowels, aperiodic material, weak pitch-mark confidence, and large duration changes. The implementation is first-party and does not introduce a new external runtime dependency.

### 4.2 Unit-scoped granular processing

The stable vowel is reconstructed through windowed overlapping grains. Within-grain source progression applies pitch ratio; source-center progression applies `sourceDrift`. `transientPreservation` attenuates grain contribution near the stable-region edges.

The renderer instance receives a single Unit and returns a single `RenderedUnit`. No analysis or overlap state crosses a Unit boundary. The final Phrase still passes through the normal `SeamComposer`, preserving the user's audible boundary policy.

### 4.3 Controls

```text
grainSize
hopSize
transientPreservation
sourceDrift
additionalGainDb
PitchCurve
```

Like SpectralClassic, Stretch validates marker ranges, loop length, pitch ratio, finite values, and cancellation.

## 5. Renderer dispatch and phrase integration

The dispatcher now executes:

```text
RawLoopRenderer
ClassicPsolaRenderer
SpectralClassicRenderer
StretchUnitRenderer
```

A dispatch result reports:

- requested renderer;
- actual renderer;
- fallback state;
- rendered PCM;
- diagnostic text.

The phrase renderer copies the same user Pitch Curve into PSOLA, SpectralClassic, and Stretch parameters. Unit-level explicit renderer choices remain canonical project state from Phase 3. The Phase 4 demo proves that one Phrase can contain one placement from each backend without hidden fallback.

## 6. Unit Lane model

The Unit Lane is intentionally a framework-independent view model. It accepts:

- canonical project and region;
- generated phonemes;
- selected Unit plan;
- solved Unit timing;
- optional actual render result;
- timeline transform;
- lane geometry and sample rate.

Each visual contains:

```text
starting PhonemeKey
selected Unit ID
alternative IDs
pixel bounds
target MIDI
forced-selection flag
requested renderer
actual renderer
fallback flag
seam amount and curve
diagnostic
```

Hit testing returns `PhonemeKey`, giving a stable bridge to future selection and command handling. No Skia type enters the model.

## 7. Sample Microscope model

The Sample Microscope prepares Voicebank Studio data without depending on a native widget toolkit.

### 7.1 Read path

- Converts source audio to mono for analysis.
- Builds a bounded waveform pyramid level.
- Builds an STFT spectrogram.
- Maps all present Unit markers into pixel space.
- Maps Pitch Marks, confidence, and lock state into pixel space.
- Supports marker and pitch-mark hit testing.

### 7.2 Edit path

Marker movement maps the requested pixel to a source frame and delegates to `MarkerEditor::set`. Pitch-mark movement delegates to `PitchMarkEditor::move`. The model then refreshes its visuals from the validated Unit. This keeps ordering and range rules in the voicebank domain rather than duplicating them in UI code.

The Phase 4 demo edits the vowel onset and one pitch mark, rebuilds the microscope data, and writes a separate edited manifest artifact.

## 8. Playback architecture

### 8.1 PlaybackTimeline

A timeline owns a sorted collection of immutable `PlaybackClip` descriptors. Each clip references `CachedPcm` and defines gain, fades, and enabled state. Mixing occurs in absolute sample-frame space and supports overlapping vocal and backing clips.

The mix stage clamps non-finite or pathological values while deliberately avoiding mastering, compression, or other hidden production effects.

### 8.2 PlaybackFeeder

The feeder is a non-real-time producer. It owns a fixed scratch block and:

- accepts an immutable timeline;
- maintains an absolute playhead;
- clears queued audio on seek;
- applies loop wrapping;
- fills an SPSC ring to a requested watermark;
- stops at timeline end when looping is disabled;
- records feed, write, ring-full, loop, and seek statistics.

### 8.3 Callback processor

`RingBufferAudioProcessor` implements `IAudioProcessor`. During each callback it:

1. reads up to the requested frame count from the ring;
2. receives zero-fill for missing frames from the ring API;
3. applies an atomic output gain;
4. copies mono to left and right outputs;
5. zeroes any extra output span;
6. updates atomic callback and underflow counters.

It does not allocate, lock, perform I/O, parse data, inspect a project, or execute synthesis.

The Phase 4 callback evidence captures 96 blocks of 256 frames with zero underflow after feeder watermark fills.

## 9. PCM cache limits

`PcmCacheLimits` introduces:

```text
maximumMemoryBytes
maximumDiskBytes
maximumDiskEntries
```

### 9.1 Memory tier

Every memory entry records payload bytes and an access counter. A hit refreshes the counter. Insertion evicts the least-recently-used entries until the byte budget is satisfied. A payload larger than the memory budget can remain on disk while not occupying the memory tier.

### 9.2 Disk tier

Disk entries are enumerated by `.spcm` extension, measured, and ordered by modification time with a deterministic path tie-break. The oldest entries are removed until both disk constraints are met. Disk reads refresh modification time.

### 9.3 Safety

The existing protections remain in place:

- restricted cache key alphabet;
- fixed extension under the cache root;
- versioned binary header;
- frame-count and sample-rate bounds;
- finite-value validation;
- stable checksum;
- temporary-file write and replacement;
- corrupt-entry statistics.

Cache usage and eviction counts are exposed for a future settings UI.

## 10. WAV output extension

`writePcm16Wav` adds 1–8 channel interleaved PCM16 output with sample-rate, channel-count, payload-alignment, RIFF-size, and finite-value checks. `writeMonoPcm16Wav` now delegates to this function. The Phase 4 demo uses stereo output for playback and callback evidence.

## 11. Phase 4 vertical slice

The demo performs the following sequence:

```text
create deterministic synthetic bank
→ create four-note Japanese Phrase
→ force PSOLA, Spectral, Stretch, and Raw Units
→ apply pitch and hard-character seam controls
→ render Raw reference
→ render four-backend production Phrase
→ build Unit Lane
→ build and edit Sample Microscope
→ persist edited manifest
→ write Phrase waveform and spectrogram
→ mix vocal plus synthetic backing
→ capture feeder/ring/callback PCM
→ force bounded-cache eviction
→ run scheduler cancellation, publication, and cache-hit proof
→ save/reload project
→ write structured summary
```

Expected renderer placement count:

```text
Raw              1
Classic PSOLA    1
SpectralClassic  1
Stretch          1
Fallback         0
```

## 12. Tests

Phase 4 extends tests across four areas.

### Synthesis

- Spectral backend executes explicitly.
- Granular Stretch executes explicitly.
- Invalid Stretch input produces inspectable Raw fallback only when allowed.
- Spectral output preserves exact length and moves harmonic energy toward target pitch.
- Stretch output is finite, deterministic, exact-length, and Unit-scoped.

### Playback

- Vocal and backing clips mix at absolute positions.
- Overlapping clips and edge fades are stable.
- Feeder loop and ring watermark behavior are deterministic.
- Callback path has zero underflow when fed.
- Explicit underflow zero-fills and increments counters.

### Cache

- Memory and disk budgets are enforced.
- Eviction counters change.
- Existing corruption and finite-value validation continue to pass.

### Inspection

- Unit Lane exposes actual renderer, fallback, seam, alternatives, and hit testing.
- Sample Microscope builds waveform and spectrogram models.
- Marker/pitch hit tests work.
- Validated marker and pitch-mark moves update both Unit and visual state.

## 13. Evidence artifacts

The Phase 4 generator creates:

- direct test and CTest logs;
- demo console output;
- project and edited voicebank manifests;
- editor and microscope SVG/PNG evidence;
- mixed-render, Raw, playback, and callback WAVs;
- waveform and spectrogram outputs;
- CLI validation and inspection outputs;
- benchmark JSON;
- branch, license, and Git integrity logs;
- audio metadata and verification matrix;
- per-artifact SHA-256 map.

## 14. Verification results

Final source verification after the Phase 4 feature commit produced:

```text
Direct named tests        70 passed / 0 failed
Dev CTest                 6 / 6 passed
Release CTest             6 / 6 passed
ASan + UBSan CTest        6 / 6 passed
Master-only policy        PASS
Dependency-license audit  PASS
git fsck --full           PASS
```

The Release benchmark on the verification host recorded:

```text
SpectralClassic          2.227× real-time
StretchUnit            219.847× real-time
Playback callback     2,601.977× real-time-equivalent throughput
Callback underflow        0 frames
Bounded cache             16 memory / 15 disk evictions
```

These figures are regression evidence for this build and host, not portable
performance guarantees. `SpectralClassic` remains classified as a
correctness-first quality renderer until lower-end hardware and interactive
edit workloads are profiled.

## 15. Honest remaining boundary

Phase 4 does not include:

- the actual iPlug2/Skia native editor window;
- Windows TSF or macOS native text-input adapters;
- a physical audio-device adapter or continuously running feeder thread;
- a complete graphical Voicebank Studio workflow;
- true multichannel project routing;
- an official contracted human voicebank;
- signed `.seambank` installation and update flow;
- CLAP, VST3, or AU targets;
- optimized real-time SpectralClassic preview rendering.

The callback architecture and UI models are production-shaped and tested, but the supplied images remain deterministic evidence renders, and the supplied voice is synthetic technical test data.

---

## Phase 4.1 stabilization follow-up

The Phase 4 feature implementation was subsequently reviewed for cache correctness, persistence durability, DSP boundary behavior, and concurrency. Those findings are closed by repository version `0.4.1`.

See:

- [`PHASE4_1_IMPLEMENTATION_REPORT.md`](PHASE4_1_IMPLEMENTATION_REPORT.md)
- [`docs/architecture/PHASE4_1_STABILIZATION.md`](docs/architecture/PHASE4_1_STABILIZATION.md)
- [`docs/phase4_1/ACCEPTANCE.md`](docs/phase4_1/ACCEPTANCE.md)

Phase 4 audiovisual evidence remains valid as feature evidence. Phase 4.1 changes correctness and infrastructure below that feature surface.
