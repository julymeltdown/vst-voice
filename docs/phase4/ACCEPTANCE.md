# Phase 4 Acceptance Criteria

## Multi-renderer synthesis

- [x] `SpectralClassicRenderer` is a concrete backend, not a metadata placeholder.
- [x] Spectral processing is confined to stable-vowel material.
- [x] Spectral pitch movement is verified by harmonic-energy tests.
- [x] `StretchUnitRenderer` is a concrete deterministic granular backend.
- [x] Stretch state is Unit-scoped and cannot cross a sample seam.
- [x] Raw, Classic PSOLA, SpectralClassic, and Stretch can coexist in one Phrase.
- [x] Requested renderer, actual renderer, fallback state, and diagnostics remain visible.
- [x] Raw fallback still requires an explicit policy and a backend rejection.

## Playback

- [x] Vocal and backing clips mix in absolute sample-frame space.
- [x] Clip gain and edge fades are applied outside the callback.
- [x] Playback seek clears queued stale audio.
- [x] Loop playback wraps deterministically.
- [x] PlaybackFeeder fills a preallocated SPSC ring by watermark.
- [x] Callback processor allocates no memory and performs no blocking I/O.
- [x] Callback underflow produces zero-fill and an inspectable counter.
- [x] Phase 4 end-to-end callback simulation completes with zero underflow.

## Cache governance

- [x] Memory payload bytes are bounded.
- [x] Disk payload bytes are bounded.
- [x] Disk entry count is bounded.
- [x] Memory and disk eviction counters are inspectable.
- [x] Version, checksum, path, finite-value, and size validation remain active.
- [x] Cache deletion or eviction cannot damage a project.

## Editor/Voicebank inspection foundation

- [x] Unit Lane exposes selected Unit, alternatives, destination bounds, renderer, fallback, and seam.
- [x] Unit Lane hit testing returns a stable `PhonemeKey`.
- [x] Sample Microscope exposes waveform, spectrogram, acoustic markers, and pitch marks.
- [x] Sample-frame and pixel conversion are reversible within rounding tolerance.
- [x] Marker edits pass through `MarkerEditor` validation.
- [x] Pitch-mark edits pass through `PitchMarkEditor` validation.
- [x] Edited voicebank metadata can be persisted.

## Verification

- [x] Debug build with warnings as errors.
- [x] Direct tests report zero failures.
- [x] End-to-end Phase 4 demo executes all four renderers.
- [x] Phase 4 demo writes editor and microscope evidence from real models.
- [x] Stereo playback and callback-preview WAV evidence is produced.
- [x] Benchmark covers Spectral, Stretch, playback, and bounded cache.
- [x] Branch policy reports only `master`.
- [x] License audit passes.
- [x] Release and sanitizer results are recorded after the final source commit.
- [x] Delivered ZIP is extracted, rebuilt, and retested after final packaging.

## Explicitly outside Phase 4

- [ ] Native iPlug2 + Skia editor window.
- [ ] Windows TSF and macOS native IME adapters.
- [ ] Physical audio-device adapter and dedicated feeder thread.
- [ ] True multichannel project routing.
- [ ] Complete Voicebank Studio application shell.
- [ ] Official human-recorded voicebank.
- [ ] Signed `.seambank` distribution package.
- [ ] CLAP, VST3, or AU targets.
