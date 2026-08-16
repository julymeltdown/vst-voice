# Phase 2 Phoneme, Voicebank, and Raw Synthesis Pipeline

## Implemented data flow

```text
VocalRegion
  ├─ Notes
  ├─ Lyrics
  └─ PhonemeOverrides
          │
          ▼
JapaneseKanaPhonemizer
          │ generated PhonemeToken[] + warnings
          ▼
UnitCandidateGenerator
          │ exact phoneme sequence matches
          ▼
DeterministicUnitSelector
          │ minimum-score complete cover
          ▼
TimingSolver
          │ vowel onset aligned to note-on
          ▼
RawLoopRenderer (per unit)
          │ pitch/time transform + sustain loop + release
          ▼
SeamComposer
          │ explicit audible overlap character + de-click
          ▼
PhraseAudio
```

## Canonical versus derived state

Canonical project state:

- notes and lyrics;
- phoneme symbol/timing overrides;
- lock state;
- voicebank reference.

Derived state:

- generated phoneme tokens;
- unit candidates;
- selected unit plan;
- timing plan;
- rendered PCM;
- waveform and spectrogram views.

The derived state can be discarded and regenerated. The Phase 2 demonstration verifies project schema 2 round-trip equality before rendering.

## Phonemizer

The Japanese implementation supports:

- Hiragana and Katakana normalization;
- base mora;
- contracted mora such as `きゃ`;
- sokuon `っ` as `cl`;
- moraic nasal `ん` as `N`;
- long-vowel continuation notes;
- punctuation as pause tokens;
- unsupported-character warnings;
- symbol, timing, and lock overrides.

It is intentionally rule-based and deterministic. It does not invoke a model or network service.

## Unit selection

The selector searches enabled units in the requested style whose `phones` exactly match a token span. It uses dynamic programming to find a complete sequence cover. The score currently includes:

- nominal pitch distance;
- preference for longer matching units;
- explicit priority;
- take order.

Ties are resolved by token coverage and stable unit ID ordering. Random variation is not used.

Phase 2 does not yet persist explicit user unit overrides. The UI proof exposes alternatives returned by the selector; canonical unit override support remains a later application use case.

## Timing

For each selected unit:

```text
unit destination start = note-on frame - transformed vowel-onset offset
```

The solver reports, rather than hides:

- negative preutterance;
- transition regions longer than the destination;
- adjacent unit overlap;
- references to missing notes.

Adjacent overlap is expected in a concatenative bank and is passed to `SeamComposer`.

## RawLoopRenderer

The first renderer performs:

- mono conversion;
- pitch shift through source-position resampling;
- transition preservation;
- stable-vowel looping;
- optional loop-boundary smoothing controlled by `loopPrint`;
- release placement;
- unit gain;
- DC removal;
- one-millisecond edge de-click.

It deliberately does not perform spectral envelope matching, neural reconstruction, phrase-wide smoothing, or source-F0 flattening. Raw source pitch variation remains present by construction; a user-controllable source-residual curve belongs to the later PSOLA/spectral renderers, not this implementation.

## SeamComposer

Overlapping rendered units are mixed on an explicit `seamAmount` axis:

- `0`: smooth cubic overlap;
- `1`: hard midpoint handoff;
- intermediate values: interpolation between the two.

The composer preserves unit boundaries while applying a minimal phrase-edge fade. It does not move unit boundaries or analyze spectral similarity.

## Voicebank analysis foundation

Implemented services:

- RIFF/WAVE PCM and float input with bounded file size;
- mono PCM16 output;
- peak, RMS, DC, and clipping statistics;
- multilevel waveform summaries;
- radix-2 FFT spectrogram generation;
- autocorrelation F0 analysis with first-near-global peak selection to avoid subharmonic octave errors;
- marker normalization and validation;
- manifest JSON codec;
- complete bank validator;
- CLI validation, inspection, and WAV analysis.

## Current boundaries

Not implemented in this phase:

- actual native iPlug2/Skia window;
- operating-system IME overlay adapters;
- recording transport;
- PSOLA or spectral synthesis;
- persistent user unit override commands;
- render scheduler, cancellation, and PCM cache;
- ZIP bank packaging and signatures;
- official recorded voicebank.

The evidence application is an end-to-end portable vertical slice, not a claim that the production desktop editor is complete.
