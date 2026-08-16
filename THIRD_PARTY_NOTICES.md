# Third-Party Notices

## Distributed source and binary code

Project SEAM does not vendor third-party source or shared libraries. Phase 7 and later builds link the system OpenSSL 3 Crypto library for Ed25519 signing and verification; OpenSSL 3 is available under the Apache License 2.0. The repository archive does not redistribute libcrypto.

Projects listed under `plannedOrReferenceOnly` in `third_party/manifest.yml` were studied or selected as future candidates but are not included in the build or archive as dependencies.

## Operating-system integrations

### Linux

The Linux build uses Xlib/XIM headers and links the operating system's X11 client library. The physical output and recording adapters load the operating system's PulseAudio Simple shared library at runtime when available. Project SEAM does not redistribute X11 or PulseAudio source or shared libraries in this repository package.

### Windows

The Windows build uses operating-system Win32, COM, Text Services Framework, WASAPI, and MMCSS APIs. These APIs and system libraries are supplied by Windows and are not redistributed in this repository package.

### macOS

The macOS build uses operating-system AppKit, Foundation, CoreGraphics, CoreAudio, AudioToolbox, and CoreFoundation frameworks. These frameworks are supplied by macOS and are not redistributed in this repository package.

The callback-clock output fallback and synthetic input fallback are first-party code. They report `physical=false` and do not represent physical speaker output or microphone capture.

## Development tools

CMake, compilers, Python, Ninja, Git, Xvfb, GitHub Actions runners, and optional ImageMagick are development, CI, or evidence-generation tools and are not incorporated into Project SEAM binaries by this repository.

## First-party concept assets

The images under `assets/character-01/concepts` are internal concept material generated for this project. They are not third-party band artwork, merchandise, logos, voicebanks, or release-ready commercial character assets. Their production replacement and rights review remain required before a public product release.
