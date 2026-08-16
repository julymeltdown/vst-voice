# Phase 5.1 Acceptance Criteria

## Native editor product surface

- [x] Native editor renders Phoneme Lane from generated phonemes.
- [x] Native editor renders Unit Lane from persistent Unit overrides.
- [x] Native editor renders Pitch Automation lane.
- [x] Technical lanes have independent geometry and cannot be interpreted as piano-roll note drag targets.
- [x] Character Full mode uses a dedicated right-side dock instead of overlaying technical lanes.
- [x] Character Minimal mode provides compact identity presentation.
- [x] Character Off mode removes presentation without changing editing or synthesis.
- [x] Canonical low-poly character asset is versioned in the repository and packaged into bounded runtime portraits.

## Character/voicebank product binding

- [x] Character package has a data-only manifest and bounded pre-rendered state assets.
- [x] Character package loader rejects unsafe paths and missing assets.
- [x] Voicebank Manifest schema 3 supports optional `characterId` + `characterVersion` binding.
- [x] Schema 1 and 2 manifests migrate with no character binding.
- [x] A partial character binding is invalid.
- [x] Character metadata remains outside synthesis and PCM cache identity.

## Graphical Voicebank Studio

- [x] Real native X11 Voicebank Studio window exists.
- [x] Unit list and selected Unit inspector are visible.
- [x] Waveform and spectrogram are drawn from the selected WAV.
- [x] Acoustic markers can be dragged through the validated MarkerEditor path.
- [x] Pitch Marks are visible and editable.
- [x] Ctrl+S persists the edited manifest.
- [x] Native Xvfb smoke test runs against the real Phase 2 synthetic bank.

## Recording transport

- [x] Shared `IAudioInputDevice` contract exists.
- [x] Linux physical capture adapter uses runtime-loaded PulseAudio Simple API.
- [x] CI/headless threaded input fallback reports `physical=false`.
- [x] RecordingSession preallocates bounded storage and performs callback copies without resizing.
- [x] Recorded takes can be exported as mono PCM16 WAV.
- [x] Voicebank Studio exposes record/stop transport and stores takes below the bank root.

## Verification

- [x] Warnings-as-errors Debug build.
- [x] Named test suite has zero failures.
- [x] Native editor X11 smoke passes with Character 01 package.
- [x] Voicebank Studio X11 smoke passes.
- [x] Release CTest refreshed after final Phase 5.1 changes.
- [x] ASan + UBSan CTest refreshed after final Phase 5.1 changes.
- [x] ThreadSanitizer suite refreshed after final Phase 5.1 changes.
- [x] Clean clone configure/build/retest completed after the Phase 5.1 implementation and evidence commits.

## Explicitly not claimed complete in Phase 5.1

The following require separate platform/product phases and are not faked with placeholders:

- Windows native shell, TSF, WASAPI verification;
- macOS AppKit, NSTextInputClient, CoreAudio verification;
- audited iPlug2 + Skia production integration;
- production CJK shaping/rasterization;
- true stereo/multichannel project bus routing;
- signed/installable `.seambank` distribution format;
- a contracted and human-recorded commercial voicebank;
- CLAP, VST3, and AU host targets.
