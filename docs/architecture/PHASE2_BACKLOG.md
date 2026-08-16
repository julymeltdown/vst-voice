# Phase 2 Backlog and Status

## Completed in Phase 2

| Item | Status | Evidence |
|---|---|---|
| `IPhonemizer` and first language implementation | Complete | Japanese Kana phonemizer tests and demo |
| `PhonemeToken` and explicit user overrides | Complete | project schema 2, command/serialization tests |
| backend-independent text composition model | Complete | composition lifecycle tests |
| Phoneme Lane | Complete | lane geometry/hit-test tests and editor SVG |
| voicebank manifest and WAV import foundation | Complete | manifest codec, bounded WAV reader/writer |
| waveform and spectrogram analysis | Complete | SVG/PGM artifacts and CLI |
| pitch analysis | Complete for foundation | autocorrelation analyzer and root-pitch validation |
| marker editor and bank validation | Complete | validation tests and CLI report |
| deterministic unit candidate generation and selection | Complete | dynamic-programming selector tests |
| phrase timing and RawLoopRenderer vertical slice | Complete | playable WAV and alignment tests |
| explicit seam composition | Complete | overlap-character tests and rendered phrase |

## Deferred—not silently counted as complete

1. Approve and vendor exact iPlug2/Skia revisions.
2. Implement the native desktop shell and Skia painter.
3. Implement Windows/macOS native text overlays around the composition model.
4. Add persistent explicit unit-selection overrides.
5. Add a production Voicebank Studio window around the analysis services.
6. Implement PSOLA with editable pitch marks.
7. Implement SpectralClassic and per-unit fallback dispatch.
8. Add background render scheduling, cancellation, dirty phrase invalidation, and PCM cache.
9. Replace development directories with signed `.seambank` containers.
10. Record and calibrate the first licensed official voicebank.
