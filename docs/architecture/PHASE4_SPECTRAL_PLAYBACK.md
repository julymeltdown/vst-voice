# Phase 4 Architecture: Multi-renderer synthesis, inspection, and callback-ready playback

> **Current architecture note:** Phase 4.1 replaces the Phase 4 cache-format, transport-reset, scheduler-publication, and stable-vowel transition details. This document remains the renderer/playback feature history; current stabilization rules are in [`PHASE4_1_STABILIZATION.md`](PHASE4_1_STABILIZATION.md).

## 1. Scope

Phase 4 completes the renderer identifiers already present in project and voicebank data, adds production-shaped playback handoff, bounds disposable PCM storage, and exposes render/voicebank information through UI-facing models.

The implemented path is:

```text
Project schema 3
→ Japanese phonemizer
→ deterministic/forced Unit selection
→ vowel-onset timing
→ Raw / PSOLA / Spectral / Stretch dispatch
→ boundary-specific Seam composition
→ Phrase PCM
→ bounded content cache
→ PlaybackTimeline
→ PlaybackFeeder
→ SPSC ring
→ allocation-free callback processor
```

A second inspection path supports Voicebank Studio work:

```text
Unit WAV
→ waveform pyramid
→ spectrogram
→ acoustic marker visuals
→ pitch-mark visuals
→ validated marker/pitch edits
→ manifest persistence
```

## 2. Concrete renderers

### 2.1 Raw

Raw keeps direct sample interpolation, stable-vowel looping, source pitch-shift character, and minimal de-clicking. It remains the reference implementation and never disappears behind a quality mode.

### 2.2 Classic PSOLA

PSOLA uses voicebank pitch marks to replace voiced sustain while preserving direct consonant and release material. It remains appropriate when periodic source material has trustworthy marks.

### 2.3 SpectralClassic

SpectralClassic uses a radix-2 FFT/STFT path over the stable vowel only.

Per analysis frame it:

1. samples a source window from the declared loop;
2. applies a Hann window;
3. computes a complex spectrum;
4. estimates a deliberately coarse spectral envelope;
5. moves harmonic-bin energy according to target MIDI and pitch automation;
6. applies bounded formant-envelope correction;
7. advances or resets phase according to `phaseReset`;
8. performs IFFT and weighted overlap-add.

The implementation does not mix the unshifted spectrum back into the output. Doing so would retain a second source fundamental. Consonant/transition and release are copied from the original sample and remain outside the spectral stage.

Supported controls:

```text
FFT size
hop size
formant follow
phase reset
additional gain
PitchCurve
```

This backend is currently treated as a quality/fallback renderer. Debug-build benchmark results are captured as evidence rather than presented as real-time guarantees.

### 2.4 StretchUnit

StretchUnit is a deterministic granular overlap-add renderer. It is first-party and has no external runtime dependency.

It:

1. preserves source consonant and release ranges;
2. repeatedly samples stable-vowel grains;
3. changes within-grain source rate according to target pitch;
4. advances source centers according to explicit `sourceDrift`;
5. attenuates grain contribution near transients;
6. normalizes overlap weights;
7. destroys all renderer state at the Unit boundary.

The last property is essential: a stretch operation cannot smooth through a seam that the user intended to hear.

## 3. Dispatcher contract

`UnitRendererDispatcher` produces a `RendererDispatchResult` containing:

```text
RenderedUnit
requested RendererHint
actual RendererHint
usedFallback
Diagnostic
```

The dispatcher does not pre-emptively fall back merely because a renderer was historically unavailable. Each implemented backend is called directly. Raw fallback occurs only after an explicit backend failure and only when `allowRawFallback` is enabled.

## 4. Unit Lane model

`UnitLaneModel` combines:

- generated phonemes;
- deterministic Unit plan;
- solved destination timing;
- actual render placement data;
- timeline coordinate conversion.

Each visual exposes:

```text
PhonemeKey
selected Unit ID
alternative Unit IDs
pixel bounds
forced selection state
target MIDI
requested and actual renderer
fallback state
seam amount and curve
diagnostic
```

The model is independent of Skia and can be tested or rendered by another frontend.

## 5. Sample Microscope model

`SampleMicroscopeModel` builds UI-ready data without owning the native window:

- bounded waveform columns;
- STFT spectrogram data;
- acoustic marker positions;
- pitch-mark positions and lock state;
- frame↔pixel conversion;
- marker and pitch-mark hit testing.

Editing calls the existing `MarkerEditor` and `PitchMarkEditor`, then refreshes the visual state. Marker order and pitch-mark ranges are therefore validated by domain tools rather than by ad-hoc UI checks.

## 6. Playback timeline

`PlaybackTimeline` stores immutable clip references in absolute sample-frame space. A clip contains:

```text
ID
CachedPcm reference
gain
fade-in frames
fade-out frames
enabled state
```

Mixing is deterministic, finite-value guarded, and independent of the project object graph. Vocal phrases and backing audio use the same representation.

## 7. Feeder and callback

`PlaybackFeeder` is a non-real-time producer. It handles:

- play/pause state;
- absolute playhead;
- seek with ring flush;
- loop wrapping;
- block mixing;
- ring watermark filling;
- statistics.

`RingBufferAudioProcessor` is the callback consumer. It reads available mono frames, zero-fills shortages, applies an atomic gain, duplicates to two output channels, and updates atomic counters.

The callback path contains no dynamic allocation, file I/O, project traversal, JSON, SQLite, FFT creation, or mutex acquisition.

## 8. Cache governance

Memory and disk limits are independent. Memory eviction uses the internal access counter; disk eviction uses the file timestamp updated on disk access. Cache usage can be queried without exposing canonical project state.

The on-disk format remains `.spcm` version 2 with:

```text
magic
version
sample rate
absolute start frame
frame count
stable checksum
little-endian float payload
```

## 9. Native-adapter boundary

Phase 4 intentionally does not claim a physical audio-device integration or a native editor screenshot. The callback processor has been executed through the repository's callback simulator. The iPlug2/Skia window and OS IME adapters remain dependency-gated until exact source revisions and their transitive closure are approved.
