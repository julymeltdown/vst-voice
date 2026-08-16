# Phase 9 Acceptance Criteria

## Functional

- [x] Strict UTF-8 decoder rejects overlong, truncated, surrogate, and
      out-of-range sequences.
- [x] Korean Hangul, Japanese Kana/Kanji, Chinese Han, and Latin glyphs render
      through the native software canvas.
- [x] TTF and TTC containers are supported.
- [x] Missing glyphs search fallback faces and then replacement glyphs.
- [x] Text measurement and rendered output use the same layout path.
- [x] Newlines, bounded wrapping, and ellipsis are implemented.
- [x] Native X11, Win32, and AppKit source paths pass the text engine to their
      shared canvas.
- [x] ASCII diagnostic rendering remains a non-fatal fallback.

## Security and resource limits

- [x] Only absolute trusted/system font files are accepted.
- [x] No font can be loaded from a project, voicebank, character package, or
      `.seambank`.
- [x] Font file size, text bytes, code points, output dimensions, output
      pixels, lines, and cache memory are bounded.
- [x] No font files are redistributed.
- [x] Exact third-party revision, hash, license, notice, and SBOM entry exist.

## Verification

- [x] Named tests include UTF-8 validity, system font resolution, CJK alpha
      output, metrics, wrapping, ellipsis, cache hits, trusted-path rejection,
      and RasterCanvas integration.
- [x] Phase 9 deterministic demo succeeds.
- [x] X11 native editor smoke succeeds with real Japanese glyph display.
- [x] Debug and Release warnings-as-errors builds succeed.
- [x] Named suite succeeds with 122 tests.
- [x] Debug and Release CTest each succeed with 15/15 tests.
- [x] ASan + UBSan core CTest succeeds with 13/13 tests.
- [x] ASan + UBSan X11 editor and Voicebank Studio smokes succeed separately.
- [x] License audit and master-only policy succeed.

## Deferred

- [ ] Full complex-script shaping.
- [ ] iPlug2 and Skia production adapter.
- [ ] CLAP, VST3, and AU.
- [ ] Platform code signing/notarization/installers.
- [ ] Contracted production human voicebank.
