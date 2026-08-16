# Phase 2 Acceptance Criteria

## Scope

Phase 2 completes the first phoneme/voicebank foundation and includes an additional raw concatenative synthesis vertical slice.

## Accepted

- [x] `PhonemeToken`, stable key, timing, role, lock, and overrides.
- [x] generated phoneme state remains non-canonical.
- [x] schema 2 stores only explicit overrides and reads schema 1.
- [x] reversible lyric and phoneme-override commands.
- [x] note deletion removes and restores attached overrides.
- [x] backend-independent IME composition state model.
- [x] Japanese Kana phonemizer with warnings and continuation handling.
- [x] Phoneme Lane visual model and hit testing.
- [x] data-only voicebank domain and manifest codec.
- [x] bounded WAV reader and mono PCM16 writer.
- [x] waveform, spectrogram, pitch, and audio-statistics analysis.
- [x] marker editing and bank validation.
- [x] deterministic unit candidate generation and complete-cover selection.
- [x] vowel-onset-aligned phrase timing.
- [x] raw loop renderer with sustain and release.
- [x] explicit seam composition.
- [x] CLI bank validation, inspection, and WAV analysis.
- [x] end-to-end generated test bank → phonemes → units → timing → PCM.
- [x] inspectable SVG editor evidence and playable WAV evidence.
- [x] warnings-as-errors, unit tests, release build, sanitizers, branch policy, and license audit.

## Explicitly not accepted as complete

- [ ] native iPlug2/Skia application shell;
- [ ] Windows TSF/macOS text-input overlay adapters;
- [ ] production Voicebank Studio window;
- [ ] PSOLA and spectral renderers;
- [ ] background render scheduler and cache;
- [ ] official human-recorded commercial bank;
- [ ] CLAP/VST3/AU targets.
