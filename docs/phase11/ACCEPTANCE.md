# Phase 11 acceptance criteria

## Implemented and executable in the Linux validation environment

- CLAP GUI extension with a real X11 child window.
- Embedded piano roll plus visible phoneme, unit, seam and pitch lanes.
- Note drag, selection, seam edit and Unicode lyric input via the reused editor controller. Full direct phoneme, unit and pitch-point editing is not part of the completed Phase 11 scope.
- Revision-based asynchronous demo preview rendering outside `process()` using the embedded public-domain vowel fixture. Production voicebank pipeline integration remains.
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

## Remaining product gates discovered by release-readiness review

- Replace the single-sample preview renderer with the production voicebank/phonemizer/unit/timing/renderer pipeline.
- Add installed `.seambank` selection, state binding and missing-bank recovery.
- Complete direct phoneme-boundary, unit/renderer and pitch-automation editing.
- Implement authoritative host tempo/loop/seek synchronization.
- Integrate multi-track, multi-region and Phase 6 multichannel routing.
- Expand the live vowel sampler only if the product continues to claim live singing synthesis.

See `docs/REMAINING_TASKS_KO.md`.
