# ADR 0021 — Trusted system-font rasterization

## Status

Accepted for Phase 9. Amended by the status note below.

> **Status note, 2026-10-18.** The SING shell bundles four OFL-licensed font families (seven files under `assets/fonts/`, listed in [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)) and draws its text with CoreText, so "do not package font files" no longer holds for the main editor. The bounded `stb_truetype` path described here still renders text on the raster surfaces (`libs/seam-text`).

## Context

Native IME paths already preserve Korean and Japanese lyrics, but the
first-party canvas used an ASCII-only diagnostic font. Bundling CJK fonts would
increase package size and introduce a separate font-license and redistribution
surface. Accepting fonts from user projects would expose the parser to
untrusted downloaded data.

## Decision

Use the pinned permissively licensed `stb_truetype` rasterizer behind a
first-party bounded API. Load only known operating-system fonts or explicit
absolute paths controlled by the application/test operator. Do not package font
files and do not allow projects, voicebanks, `.seambank`, or character packages
to select fonts.

## Consequences

- Korean, Japanese, Chinese, and Latin native display now matches the already
  implemented Unicode input/persistence path.
- UI rendering remains independent from synthesis and graphics SDKs.
- A later Skia adapter can reuse the same scene state while replacing the text
  backend.
- Full Arabic/Indic shaping remains out of scope until a separately audited
  shaping stack is selected.
