# Phase 8 Acceptance — Windows and macOS Native Platform Sources

## Shared platform contract

- [x] `PlatformCapabilities` reports the selected native-window, composition-input, output-audio, and input-audio backend.
- [x] Native platform selection is performed by CMake; unsupported-backend factories are not linked beside a real backend.
- [x] The existing first-party editor/controller, Voicebank Studio, Character 01 presentation, multichannel callback contract, and recording session remain backend-neutral.
- [x] Character 01 remains presentation-only and does not enter synthesis, routing, cache identity, signing, installation, or exported PCM.

## Windows source implementation

- [x] Win32 native window owns the message loop, resize, DPI, pointer, wheel, keyboard, repaint, timed close, and PPM evidence path.
- [x] Unicode lyric composition uses a native `EDIT` control under activated TSF text services rather than reimplementing an IME.
- [x] Shared-mode event-driven WASAPI output supports 1–8 float channels and the existing allocation-free processor contract.
- [x] WASAPI capture provides bounded mono float input for Voicebank Studio recording.
- [x] Audio workers use preallocated buffers; the capture path rejects an oversized packet instead of resizing in the worker.
- [x] Windows CMake links the required system libraries and defines the backend selectors.

## macOS source implementation

- [x] AppKit native window owns the event loop, resizing, Retina scale, pointer, wheel, keyboard, repaint, timed close, and PPM evidence path.
- [x] The view implements `NSTextInputClient`, marked-text publication, selection, insertion, cancellation, deletion, and candidate-window positioning.
- [x] CoreAudio DefaultOutput AudioUnit provides 1–8 non-interleaved float output through the existing processor contract.
- [x] CoreAudio HAL capture provides bounded mono float input for Voicebank Studio recording.
- [x] Audio callbacks use preallocated views/buffers and do not allocate.
- [x] macOS CMake enables Objective-C++ with ARC and links AppKit/CoreGraphics/CoreAudio/AudioToolbox.

## Verification and delivery

- [x] Linux Debug build and named tests pass after adding the platform sources.
- [x] Linux Release and ASan/UBSan regressions pass.
- [x] A static Phase 8 source-contract checker verifies all platform source files and CMake selectors are present in packaged checkouts.
- [x] GitHub Actions defines Ubuntu, Windows, and macOS build/test jobs.
- [x] Optional target-host native GUI smoke tests are wired through `SEAM_RUN_NATIVE_GUI_TESTS` for Windows/macOS CI.
- [x] Phase 8 capability demo produces a machine-readable backend report.
- [x] Master-only branch and permissive dependency policies remain enforced.

## Verification boundary

The Windows and macOS implementations are complete source adapters and are selected by the target toolchain. This Linux build environment cannot execute Win32, TSF, WASAPI, AppKit, NSTextInputClient, or CoreAudio. Therefore target-host build/runtime certification is delegated to the checked-in Windows/macOS CI jobs and must not be reported as locally executed evidence until those jobs run on their respective hosts.

## Deliberately outside Phase 8

- audited iPlug2 + Skia replacement of the first-party software-raster shell;
- production CJK font shaping/rasterization in the software painter;
- a contract-recorded and release-cleared human voicebank;
- CLAP, VST3, and AU plug-in targets;
- platform installers and OS code-signing/notarization for the editor executable itself.
