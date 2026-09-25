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

### OpenSSL 3 Crypto

Project SEAM distribution builds statically link OpenSSL 3.5.7 Crypto from the
official `openssl/openssl` repository at immutable revision
`8cf17aaeb4599f8af87fefd810b5b5fee90fe69e` for Ed25519 signing and
verification. OpenSSL is available under the Apache License 2.0. The exact
upstream `LICENSE.txt` is copied into each assembled distribution payload.
Project SEAM does not ship a dynamically loaded OpenSSL runtime.

### CMU Pronouncing Dictionary

Project SEAM incorporates the unmodified North American English pronunciation
data file `assets/pronunciation/en-us/cmudict.dict` from the official
`cmusphinx/cmudict` repository at immutable revision
`74790861f652b15e4ac49015a90074ad62a27690` (upstream archive SHA-256
`741c592660bbf10ab93fe3d5aa709b73a3b5ba91e3291a066715440f4137a58d`). The
resource is copyrighted by Carnegie Mellon University and is distributed under
BSD-2-Clause. The complete license conditions and disclaimer are reproduced
below and are also included at
`licenses/third-party/CMUdict-2026-09-24-LICENSE.txt`. Upstream requests that
redistributions acknowledge CMUdict as the source. The dictionary does not
guarantee pronunciation accuracy and is not, by itself, qualification for
singing or any particular dialect.

Copyright (C) 1993-2015 Carnegie Mellon University. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

The contents of this file are deemed to be source code.

This work was supported in part by funding from the Defense Advanced Research
Projects Agency, the Office of Naval Research and the National Science
Foundation of the United States of America, and by member companies of the
Carnegie Mellon Sphinx Speech Consortium. We acknowledge the contributions of
many volunteers to the expansion and improvement of this dictionary.

THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND ANY
EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY NOR ITS EMPLOYEES BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

### Protobuf 33.4 and Abseil 20250512.1

Project SEAM distribution builds statically link Protobuf 33.4 (release tarball
SHA-256 `bc670a4e34992c175137ddda24e76562bb928f849d712a0e3c2fb2e19249bea1`) and
its vendored Abseil 20250512.1 (release tarball SHA-256
`9b7a064305e9fd94d124ffa6cc358592eb42b5da588fb4e07d09254aa40086db`) so the
shipped neural helper resolves its Protobuf runtime from the directory it is
launched out of rather than from a host package prefix. Protobuf is available
under the BSD-3-Clause license and Abseil under the Apache License 2.0. The
archive is built from the pinned tarballs and contains no shared Protobuf or
Abseil library; Project SEAM does not ship a dynamically loaded Protobuf runtime.


## System libraries and APIs

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

Selected pronunciation test inputs and format semantics are cross-checked against
OpenUtau revision `83e02c7e4a4d9ea5fca72806b2aa27c5382be015`. The exact sources,
mapping boundaries, and MIT license are recorded in
`libs/seam-phonemizer/OPENUTAU_REFERENCE_NOTICE.md`. No OpenUtau runtime,
dictionary, voicebank, or singing audio is shipped by these tests.

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

## Phase 13A build-only plug-in format dependencies

The following exact checkouts are used only on target build/validation runners and are not vendored in this repository or included in the unsigned Linux developer package:

- **CLAP SDK 1.2.10**, commit `195b42a004144fab0b3cf95e9c067187d15365b7`, MIT.
- **clap-wrapper 0.15.1**, commit `35f524b771ec09f54c164720bb90f271273b37d3`, MIT.
- **Steinberg VST3 SDK 3.8.1**, commit `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96` with locked recursive submodules, MIT.
- **Apple AudioUnitSDK 1.4.0**, commit `bd98b31feff57a15989fcfab4cd86dc63382b1ac`, Apache-2.0.
- **OpenSSL 3.5.7**, commit `8cf17aaeb4599f8af87fefd810b5b5fee90fe69e`, Apache-2.0; compiled as a static Crypto archive and linked into shipping binaries.
- **Protobuf 33.4** (BSD-3-Clause) and **Abseil 20250512.1** (Apache-2.0), pinned by release-tarball SHA-256; compiled as static archives and linked into the shipping neural helper.

The acquisition script verifies exact revisions and licence files and forbids wrapper-time network dependency resolution. VST3 and AUv2 distributables must carry the notices required by their actual resolved source closure.


## NSIS 3.12 (build-only)

The Windows installer pipeline uses NSIS 3.12 as a build tool under the zlib
license. NSIS is not bundled in Project SEAM installers or source archives.
The Project SEAM script explicitly selects the zlib compressor rather than
shipping an LZMA-compressed installer payload.
