# Phase 9 Implementation Report — Production CJK Text Rendering

## 1. Scope

Phase 9 replaces the diagnostic ASCII-only 5×7 raster text path with a bounded,
first-party Unicode text service for the native editor and Voicebank Studio.
The existing bitmap font remains available only as a deterministic emergency
fallback.

The phase target is Korean, Japanese, Chinese, and Latin display. It does not
claim full Arabic, Indic, Southeast Asian, or arbitrary OpenType shaping.

## 2. Implemented architecture

```text
UTF-8 UI string
→ strict Unicode scalar decoder
→ trusted system-font resolver
→ TTF/TTC face selection and glyph fallback
→ kerning and bounded line layout
→ antialiased alpha bitmap
→ RasterCanvas color compositing
→ X11 / Win32 / AppKit software surface
```

The new `seam_text` library contains:

- strict UTF-8 decoding and encoding;
- Hangul, Kana, Han, Latin, symbol, and replacement-glyph lookup;
- TrueType and TrueType Collection face enumeration;
- locale-aware system font candidate ordering;
- explicit trusted-file loading for deterministic tests and tools;
- antialiased glyph rasterization;
- same-face kerning;
- newline, bounded character-level wrapping, and ellipsis;
- combining-mark zero-advance placement;
- a 512-entry / 32 MiB whole-text cache with LRU eviction;
- metrics, loaded-font fingerprints, and cache diagnostics.

## 3. Security boundary

Fonts are executable-adjacent binary parsers and are therefore not accepted
from projects, voicebanks, `.seambank` packages, network content, or character
packages.

Accepted sources are limited to:

1. absolute files explicitly supplied to the trusted-file API; or
2. known operating-system font locations selected by the native application.

Every candidate must resolve to a regular file and fit the configured byte
limit. The default maximum is 64 MiB. Text input, code-point count, bitmap
dimensions, bitmap pixels, line count, and cache memory are also bounded before
or during allocation.

Project SEAM does not redistribute any font files.

## 4. Native integration

`RasterCanvas` now accepts an optional `TextEngine*`. Native X11, Win32, and
AppKit windows create a system text engine at startup and pass it to every paint
operation. If no usable font is available or a render operation fails, the
old deterministic ASCII renderer remains available rather than terminating a
`noexcept` paint path.

This closes the previous gap where native IME input could preserve Korean and
Japanese strings in the domain model but the editor canvas displayed UTF-8
bytes as placeholder glyphs.

## 5. Dependency

The only new distributed third-party source is `stb_truetype.h`, pinned to:

```text
repository  https://github.com/nothings/stb
revision    f58f558c120e9b32c217290b80bad1a0729fbb2c
SHA-256     ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab
license     MIT selected from the upstream dual offer
```

The implementation translation unit suppresses warnings only for the vendored
header. All first-party wrapper and layout code remains under the repository's
warnings-as-errors policy.

## 6. Evidence

`seam_phase9_demo` renders Korean, Japanese, Chinese, and Latin text into the
same `PixelSurface` used by the native editor and writes:

```text
phase9-cjk-text.ppm
phase9-summary.json
```

The X11 smoke path also captures the actual native editor with Japanese lyric
labels rendered from the system CJK font.

## 7. Verification

The checked-in Phase 9 evidence records:

```text
Named tests                            122 PASS / 0 FAIL
Debug CTest                             15/15 PASS
Release CTest                           15/15 PASS
ASan + UBSan core CTest                 13/13 PASS
ASan + UBSan X11 editor smoke           PASS
ASan + UBSan Voicebank Studio smoke     PASS
Native X11 CJK screenshot               PASS
License audit                           PASS
Master-only branch policy               PASS
```

The native sanitizer smokes are run separately from aggregate sanitizer CTest
because the Xvfb wrapper can retain CTest process handles under ASan in the
headless verification environment. Each binary is guarded by an external
process-group timeout, exits successfully, and writes a non-empty screenshot.

## 8. Explicit non-goals

Phase 9 does not claim:

- iPlug2 integration;
- Skia integration;
- HarfBuzz or full OpenType shaping;
- bundled or downloadable fonts;
- font loading from untrusted product data;
- CLAP, VST3, or AU plug-in products;
- signed Windows/macOS release installers.

The first-party scene/controller contract remains intentionally renderer
independent so a later audited Skia adapter can replace `RasterCanvas` without
moving editing behavior into the graphics SDK.
