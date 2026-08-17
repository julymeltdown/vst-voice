# Phase 11 acceptance criteria

## Implemented and executable in the Linux validation environment

- CLAP GUI extension with a real X11 child window.
- Embedded piano roll and phoneme, unit, seam and pitch lanes.
- Note drag, selection, seam edit and Unicode lyric input via the reused editor controller.
- Revision-based asynchronous preview rendering outside `process()`.
- Lock-free bounded preview publication to the audio callback.
- CLAP note input with sample-accurate note-on/off handling and 16-voice live human-sample playback.
- State save/load with a 16 MiB bound, checksum and active-load restart policy.
- Dynamic first-party host lifecycle, GUI, state and live-note smoke test.
- Provenance-preserved public-domain human voice fixture.

## Source-ready and target-platform gated

- Win32 child editor and native text overlay.
- Cocoa `NSView` child editor and native text overlay.
- macOS `.clap` bundle metadata and packaging.
- Windows package and Authenticode scripts.
- macOS codesign, notarization, stapling and PKG scripts.
- VST3 and AUv2 wrapper build entry through pinned clap-wrapper v0.15.1.

## External validation gates—not reported as PASS without evidence

- Official `clap-validator` execution.
- REAPER, Bitwig Studio, Cubase, Ableton Live, Studio One, FL Studio and Logic Pro host certification.
- Apple notarization and Windows Authenticode signing with release credentials.
- VST3 validator and `auval` on target systems.
- Contracted and directed recording of Official Voicebank 01.
