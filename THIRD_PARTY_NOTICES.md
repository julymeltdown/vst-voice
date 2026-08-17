# Third-Party Notices

## Distributed source code

### stb_truetype

Project SEAM vendors `stb_truetype.h` from the official `nothings/stb`
repository at immutable revision
`f58f558c120e9b32c217290b80bad1a0729fbb2c`.

It is used only to rasterize glyphs from trusted, operating-system-installed
TrueType and TrueType Collection files. Project SEAM does **not** redistribute
font files and does not accept fonts embedded in projects, voicebanks, or
`.seambank` packages.

`stb_truetype.h` is offered under the MIT License or public domain dedication.
Project SEAM selects the MIT License. The complete upstream license text is
included at `third_party/stb/LICENSE`.

## System libraries and APIs

### OpenSSL 3 Crypto

Phase 7 and later builds link the system OpenSSL 3 Crypto library for Ed25519
signing and verification. OpenSSL 3 is available under the Apache License 2.0.
The repository archive does not redistribute libcrypto.

### Linux

The Linux build uses Xlib/XIM headers and links the operating system's X11
client library. Physical output and recording adapters load the operating
system's PulseAudio Simple shared library at runtime when available. Project
SEAM does not redistribute X11 or PulseAudio source or shared libraries.

### Windows

The Windows build uses operating-system Win32, COM, Text Services Framework,
WASAPI, and MMCSS APIs. These APIs and system libraries are supplied by Windows
and are not redistributed in this repository package.

### macOS

The macOS build uses operating-system AppKit, Foundation, CoreGraphics,
CoreAudio, AudioToolbox, and CoreFoundation frameworks. These frameworks are
supplied by macOS and are not redistributed in this repository package.

The callback-clock output fallback and synthetic input fallback are first-party
code. They report `physical=false` and do not represent physical speaker output
or microphone capture.

## Reference-only projects

Projects listed under `plannedOrReferenceOnly` in
`third_party/manifest.yml` were studied or selected as future candidates but
are not included in the build or archive as dependencies. This includes iPlug2,
Skia, OpenUtau, and vLabeler.

## Development tools

CMake, compilers, Python, Ninja, Git, Xvfb, GitHub Actions runners, and optional
ImageMagick/Pillow are development, CI, or evidence-generation tools and are not
incorporated into Project SEAM binaries by this repository.

## First-party concept assets

The images under `assets/character-01/concepts` are internal concept material
generated for this project. They are not third-party band artwork, merchandise,
logos, voicebanks, or release-ready commercial character assets. Their
production replacement and rights review remain required before a public
product release.

### CLAP 1.2.10 public ABI editor subset

Project SEAM vendors a mechanically consolidated subset of the official CLAP
1.2.10 public C ABI from the `free-audio/clap` repository at immutable revision
`195b42a004144fab0b3cf95e9c067187d15365b7`. Declarations required by the Phase 10 render player and Phase 11 embedded editor are included; the included structure layouts and
function signatures preserve the upstream ABI. The consolidation is documented
at `third_party/clap/README.md` and is distributed under the upstream MIT
License at `third_party/clap/LICENSE`.


### Sonic public-domain human voice fixture

Phase 11 retains `talking.wav` from the Sonic sample repository and a derived
550 ms vowel-like engineering sample. The upstream sample README states that
all samples in that directory are public domain and identifies `talking.wav`
as the repository author's father speaking. Exact source/derived SHA-256 values,
retrieval date and processing are stored in
`assets/demo-human-voicebank-public-domain/provenance.json`. The fixture is not
Official Voicebank 01 and is not described as a contracted-singer product.
