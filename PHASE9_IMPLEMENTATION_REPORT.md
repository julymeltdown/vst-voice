# Project SEAM Phase 9 — Production CJK Text Rendering

Phase 9 adds a bounded Unicode text subsystem and integrates it with every
first-party native software-raster window.

Implemented:

- strict UTF-8 scalar validation;
- trusted system TTF/TTC discovery;
- Korean, Japanese, Chinese, Latin, symbol, and replacement-glyph fallback;
- antialiased rasterization and same-face kerning;
- wrapping, line limits, ellipsis, metrics, and bounded caching;
- `RasterCanvas` alpha compositing with deterministic ASCII fail-safe;
- X11, Win32, and AppKit shared text-engine integration;
- `seam_phase9_demo` and native X11 screenshot evidence;
- exact vendored dependency manifest, MIT notice, and SPDX SBOM update.

Project SEAM redistributes no font files and does not accept fonts from
projects, voicebanks, character packages, or `.seambank` archives.

See:

- `docs/phase9/IMPLEMENTATION_REPORT.md`
- `docs/phase9/ACCEPTANCE.md`
- `docs/architecture/UNICODE_TEXT_RENDERING.md`
- `docs/adr/0021-trusted-system-font-rasterization.md`
## Verification

```text
Named tests                            122 PASS / 0 FAIL
Debug CTest                             15/15 PASS
Release CTest                           15/15 PASS
ASan + UBSan core CTest                 13/13 PASS
ASan + UBSan native X11 smokes          2/2 PASS
Native CJK editor screenshot            PASS
License and master-only policy          PASS
```

Full complex-script shaping, audited iPlug2/Skia integration, plug-in formats,
platform signing/installers, and a contracted production human voicebank remain
separate later phases.
