# Phase 8 Build and Runtime Guide

## Common requirements

- CMake 3.25 or later
- C++20 compiler
- Python 3 for source-contract, branch, and license checks
- OpenSSL 3 Crypto development package
- `master` as the only local branch

## Windows

### Toolchain

- Windows 11 or a supported Windows 10 SDK
- Visual Studio 2022 with Desktop development with C++
- OpenSSL 3 x64 development installation

### Configure and build

```powershell
cmake -S . -B build/windows `
  -DSEAM_BUILD_TESTS=ON `
  -DSEAM_BUILD_BENCHMARKS=OFF `
  -DSEAM_WARNINGS_AS_ERRORS=ON `
  -DSEAM_RUN_NATIVE_GUI_TESTS=ON `
  -DOPENSSL_ROOT_DIR="C:\Program Files\OpenSSL-Win64"
cmake --build build/windows --config Debug --parallel 2
ctest --test-dir build/windows -C Debug --output-on-failure
```

### Native runtime acceptance

Run both native applications with synthetic/threaded I/O first:

```powershell
build\windows\Debug\seam_editor_native.exe `
  --force-threaded-audio `
  --character-package assets\character-01 `
  --auto-close-ms 1000 `
  --screenshot build\windows\phase8-editor.ppm

build\windows\Debug\seam_voicebank_studio_native.exe `
  --manifest build\windows\phase2-smoke\synthetic-voicebank\manifest.json `
  --force-synthetic-input `
  --auto-close-ms 1000 `
  --screenshot build\windows\phase8-studio.ppm
```

Then test the physical adapters without force flags. Acceptance must cover:

- Korean and Japanese TSF composition, conversion candidates, commit, cancel, Backspace, cursor movement, and empty lyric commit;
- 100%, 150%, 200%, and mixed-monitor DPI movement;
- repeated window open/close and focus transfer;
- mono, stereo, quad, 5.1, 7.1 request behavior on available endpoints;
- WASAPI endpoint absence, endpoint replacement, and sample-rate conversion;
- microphone start/stop, silent packet, discontinuity, and device-unplug handling;
- 30-minute playback/recording soak with no callback allocation or unbounded growth.

## macOS

### Toolchain

- current supported macOS and Xcode command-line tools
- Homebrew OpenSSL 3, or another discoverable OpenSSL 3 development package

### Configure and build

```bash
cmake -S . -B build/macos \
  -DSEAM_BUILD_TESTS=ON \
  -DSEAM_BUILD_BENCHMARKS=OFF \
  -DSEAM_WARNINGS_AS_ERRORS=ON \
  -DSEAM_RUN_NATIVE_GUI_TESTS=ON \
  -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"
cmake --build build/macos --parallel 2
ctest --test-dir build/macos --output-on-failure
```

### Native runtime acceptance

Run the editor and Voicebank Studio first with non-physical fallbacks, then without force flags. Acceptance must cover:

- Korean and Japanese marked-text composition, conversion candidates, commit, cancel, deletion, cursor movement, and empty lyric commit;
- candidate-window placement from `firstRectForCharacterRange`;
- Retina and non-Retina backing-scale behavior and resizing;
- window close, re-open, activation, focus, and repeated application lifecycle;
- one through eight requested output channels on available CoreAudio devices;
- output/input sample-rate and channel negotiation failures;
- microphone permission denied, granted, revoked, and device-unplug behavior;
- 30-minute playback/recording soak with no callback allocation or unbounded growth.

## Linux regression

The existing Linux path remains:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

X11 smoke tests run through Xvfb when `xvfb-run` is available. Physical PulseAudio output/input open failures remain explicit, and tests may deliberately select the threaded/synthetic fallbacks.

## Static source contract

On any host:

```bash
python3 scripts/verify_phase8_platform_sources.py --root .
```

This verifies that the platform source closure and CMake selection markers remain in the package. It is not a replacement for native compilation or physical runtime acceptance.
