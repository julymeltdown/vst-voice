# Phase 11 Known Limitations

## Product behavior

1. The asynchronous CLAP preview is a dedicated single-human-vowel sample loop. It does not yet call the production voicebank/phonemizer/unit-selection/timing/renderer pipeline.
2. Notes, lyrics and seam amount are directly editable. Phoneme timing, unit selection/renderer and pitch automation are displayed but do not yet have complete direct-manipulation editing.
3. The embedded editor operates on the first vocal region and stereo output. Multi-track, multi-region and Phase 6 routing are not integrated into this plug-in.
4. Host beat/tempo conversion does not rerender the project when host tempo differs from the internal project TempoMap.
5. Live note input is a 16-voice vowel sampler, not complete lyric-driven singing synthesis.
6. Note input advertises the CLAP note dialect only; broad MIDI-host compatibility remains to be validated and extended.
7. The plug-in uses the embedded public-domain demo sample and does not select an installed trusted `.seambank`.

## Platform and release evidence

- Linux/X11 child GUI is runtime verified.
- Win32 and Cocoa are source-ready, not certified here.
- Official `clap-validator`, commercial DAWs, VST3 validator and `auval` have not produced local PASS evidence.
- Signing, notarization and installer scripts require release credentials and clean-system testing.
- Official Voicebank 01 requires a contracted performer and directed recording.

These limitations are release-planning inputs, not hidden fallback behavior.
