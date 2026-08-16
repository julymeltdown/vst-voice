# Project SEAM Phase 8 Implementation Report

Phase 8 follows the requested sequence after Phase 5.1 packaging, Phase 6 multichannel routing, and Phase 7 signed/installable `.seambank` distribution. It adds first-party Windows and macOS native platform adapters without changing the cross-platform editor, voicebank, routing, signing, or Character 01 domain boundaries.

## 1. Platform selection and capability reporting

`seam-platform` now owns a compile-time capability record identifying the active window, text-composition, output-audio, input-audio, and channel contract. `seam_phase8_demo` writes the selected values to `phase8-platform-capabilities.json` so packaged builds can report what was compiled rather than inferring it from UI strings.

The unavailable factories are guarded so exactly one physical audio implementation and one native-window implementation own each exported factory on a supported target.

## 2. Windows implementation

### Win32 shell

`native_window_win32.cpp` implements:

- Unicode Win32 class/window creation;
- per-monitor DPI awareness and `WM_DPICHANGED` handling;
- resize-backed `PixelSurface` presentation through `StretchDIBits`;
- pointer capture, buttons, dragging, double clicks, wheel and keyboard events;
- timer-driven automatic closure and deterministic PPM snapshot output;
- repaint requests posted from backend-neutral controllers.

### TSF-backed lyric composition

The UI thread initializes COM and activates `ITfThreadMgr`. An active lyric cell creates and positions a Unicode native `EDIT` child. The OS-native editor remains the TSF/IME host, while Project SEAM mirrors its text and selection into the shared composition model. Enter commits through the existing undoable lyric path; Escape cancels. This is materially different from fabricating individual key events or attempting to implement Korean/Japanese composition rules in product code.

### WASAPI output

`wasapi_audio_device.cpp` implements a shared-mode, event-driven float stream:

- default render endpoint;
- 1–8 channel `WAVEFORMATEXTENSIBLE` layout;
- system PCM conversion/sample-rate conversion where needed;
- MMCSS `Pro Audio` worker;
- preallocated planar processor buffers and interleaving into the endpoint buffer;
- explicit start/stop/reset lifecycle and diagnostic counters.

### WASAPI recording input

`wasapi_audio_input_device.cpp` opens the default capture endpoint as mono float, drains event-driven capture packets, handles silent/discontinuity flags, and forwards a bounded span to the pre-existing recording processor. Oversized packets are rejected rather than growing a vector in the worker.

## 3. macOS implementation

### AppKit shell

`native_window_appkit.mm` implements:

- `NSApplication`, `NSWindow`, and a custom flipped `NSView`;
- Retina backing-scale tracking and shared `PixelSurface` presentation through CoreGraphics;
- pointer, drag, wheel, keyboard, resize, repaint, auto-close and snapshot paths;
- ARC-managed Objective-C objects within the C++ native-window contract.

### `NSTextInputClient`

The AppKit view implements the complete composition surface needed by the editor:

- marked and selected ranges;
- `setMarkedText`, `unmarkText`, and `insertText`;
- attributed substring lookup;
- deletion and command selectors;
- candidate-window screen rectangle;
- composed-character-safe deletion;
- shared Unicode composition publication and final lyric commit.

### CoreAudio output

`coreaudio_audio_device.mm` uses the DefaultOutput AudioUnit with 32-bit, non-interleaved float channels. Callback channel spans are preallocated in a fixed array, zeroed, passed to `IAudioProcessor`, and written directly to CoreAudio buffers.

### CoreAudio recording input

`coreaudio_audio_input_device.mm` uses the HAL Output AudioUnit with output disabled and input enabled on the default input device. The input callback calls `AudioUnitRender` into a preallocated mono buffer and forwards the resulting span to the existing recording processor.

## 4. Cross-platform CI and smoke path

The CI workflow is now an Ubuntu/Windows/macOS matrix. Windows and macOS jobs install OpenSSL 3 for the Phase 7 Ed25519 implementation, compile all platform-selected sources with warnings as errors, run CTest, and execute the Phase 8 source-contract checker. The target-host jobs enable native editor and Voicebank Studio timed smoke tests through `SEAM_RUN_NATIVE_GUI_TESTS`.

The source-contract checker exists because a Linux-only package verification cannot compile platform-gated files. It checks that the platform implementation files, native API integration points, CMake selectors, and audio selectors survive packaging. It does not falsely represent static inspection as runtime certification.

## 5. Character 01 on Windows and macOS

No platform-specific Character 01 logic was added. The shared native scene continues to own:

```text
Full    dedicated character dock
Minimal compact voicebank identity
Off     no character presentation
```

The Win32 and AppKit shells display the same software-raster scene and runtime portraits. Character state remains a UI response only and cannot alter synthesis, routing, package signatures, cache identity, or exported PCM.

## 6. Verification performed in the current environment

The current Linux environment verifies:

- all prior domain, editor, synthesis, routing, distribution, Character 01 and native-Linux tests;
- Linux native X11/PulseAudio regressions;
- Phase 8 capability output;
- the complete Windows/macOS source and CMake contract;
- warnings-as-errors Debug/Release builds;
- ASan/UBSan tests;
- master-only and license policies;
- clean-package extraction and rebuild.

It cannot execute Windows or macOS frameworks. Those runtime results must come from the target-host jobs defined in `.github/workflows/ci.yml` or from corresponding physical development machines.

## 7. Remaining product work

Phase 8 deliberately does not claim completion of:

- iPlug2 + Skia production-shell adoption;
- production CJK font shaping in the diagnostic software painter;
- Windows Authenticode signing, macOS app signing/notarization, or GUI installers;
- a contracted/release-cleared human voicebank;
- CLAP, VST3, or AU targets.
