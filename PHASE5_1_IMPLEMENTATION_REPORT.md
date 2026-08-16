# Project SEAM Phase 5.1 implementation report

Phase 5.1 converts the Phase 5 Linux native vertical slice into a more coherent product surface. It adds the missing native technical lanes, integrates the canonical Character 01 as an optional voicebank avatar, adds a graphical Voicebank Studio, and adds a microphone-input/recording transport contract with a Linux physical backend and deterministic headless fallback.

## Implemented

### Character product runtime

- `seam-character` data-only package parser and validation.
- Canonical low-poly source art stored under `assets/character-01/source`.
- Six pre-rendered bounded runtime states.
- Native Full/Minimal/Off presentation modes.
- Dedicated non-obstructive character dock.
- Voicebank Manifest schema 3 character binding.
- Explicit architectural separation from synthesis/cache/audio callback.

### Native editor lanes

- generated Phoneme Lane;
- persistent Unit Lane with renderer labels;
- Pitch Automation lane;
- hit-testing restricted so technical lanes do not mutate notes;
- Character 01 presentation alongside, never over, these lanes.

### Graphical Voicebank Studio

- real X11 native window;
- unit browser;
- waveform and spectrogram microscope;
- acoustic marker and Pitch Mark editing;
- manifest save;
- record transport status;
- Xvfb smoke path against a generated synthetic bank.

### Recording input

- `IAudioInputDevice` backend-neutral contract;
- `RecordingSession` bounded callback sink;
- runtime-loaded Linux PulseAudio capture device;
- threaded silence fallback for CI/headless tests;
- take export to mono WAV.

## Why the character is here

Character 01 belongs to the **voicebank product identity**, not to the audio engine. Users encounter the character where voicebank identity or product feedback matters: Welcome, voicebank cards, optional editor dock, compact toolbar identity, render status, store/package/docs. The character is absent from exported audio, Phrase render identity, PCM cache identity, and real-time callback logic.

This keeps the character commercially central without turning the synth into a mascot-driven UI or making a missing portrait break music production.

## Still future

Phase 5.1 does not claim cross-platform native completion from a Linux build machine. Windows/macOS platform adapters, iPlug2/Skia production shell, signed bank packaging, true multichannel buses, the contracted commercial human voicebank, and plugin formats remain separately gated work.
