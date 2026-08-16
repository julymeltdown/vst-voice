# Phase 5 Acceptance Criteria

## Native editor contract

- [x] Native editor controller uses the existing Project, Session, and PianoRoll models.
- [x] Pointer drag commits exactly one command on release.
- [x] Box selection, double-click note creation, keyboard movement, delete, undo, and redo are connected.
- [x] Native lyric editing supports Unicode begin/update/commit/cancel.
- [x] Character presentation is not required for editing or playback.

## Native window and rendering

- [x] A real X11 window opens and paints under Xvfb.
- [x] Window resize updates logical editor coordinates.
- [x] Pointer, wheel, and keyboard events are translated into backend-neutral DTOs.
- [x] Process locale and XIM environment modifiers are initialized before native UTF-8 text commits.
- [x] 1× and 2× logical scenes produce valid deterministic pixel surfaces.
- [x] Native smoke test writes a final window screenshot.
- [x] Graphics never execute on the audio callback.

## Playback runtime

- [x] A dedicated service thread is the only producer calling `PlaybackFeeder` fill methods.
- [x] UI transport methods enqueue commands and wake the service.
- [x] The callback remains the only SPSC consumer.
- [x] A physical Linux PulseAudio adapter exists and reports open/write failures explicitly.
- [x] A callback-clock fallback runs the same callback contract and reports `physical=false`.
- [x] Platforms without an implemented native window or physical device receive explicit unavailable adapters.
- [x] Native app pre-buffers before callback start.
- [x] Dedicated feeder, callback device, seek, loop, and play-state changes pass concurrent tests.

## Verification

- [x] Warnings-as-errors Debug build.
- [x] Named test suite has zero failures.
- [x] Phase 2, 3, and 4 regression demos pass.
- [x] Phase 5 headless runtime demo passes.
- [x] X11 native-window smoke test passes.
- [x] Release build and CTest pass.
- [x] ASan + UBSan build and CTest pass.
- [x] ThreadSanitizer named suite passes.
- [x] Master-only and license policies pass.
- [x] Final ZIP is extracted, rebuilt, and retested.

## Explicitly outside Phase 5

- [ ] Windows shell and TSF adapter.
- [ ] macOS AppKit shell and native composition adapter.
- [ ] WASAPI and CoreAudio adapters.
- [ ] Audited iPlug2 + Skia production shell.
- [ ] Production CJK font rasterization.
- [ ] Full Phoneme/Unit/Automation lanes in the native window.
- [ ] Complete graphical Voicebank Studio and microphone recording.
- [ ] True multichannel project routing.
- [ ] Licensed human voicebank and signed `.seambank` package.
- [ ] CLAP, VST3, and AU.
