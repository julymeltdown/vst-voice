# Unicode Text Rendering Architecture

## Boundary

`seam_text` is a UI infrastructure library. It has no dependency on Project,
Voicebank, synthesis, routing, recording, cache identity, or distribution
formats.

```text
seam_core
   ↑
seam_text
   ↑
seam_native_ui
```

Domain and application modules never include font or rasterizer headers.

## Trusted input policy

TrueType parsing is limited to operating-system-installed or explicitly trusted
absolute files. Product data cannot provide a font path. This prevents a song,
voicebank, or downloaded character package from turning native UI rendering
into an arbitrary font-parser entry point.

## Font selection

Each platform supplies an ordered list of known CJK-capable system font
locations. Additional trusted files may be supplied only through the explicit
text API used by tests and controlled tools. Each TTF/TTC candidate is checked
for:

- canonical absolute regular-file status;
- configured byte limit;
- usable font faces;
- representative Latin, Hangul, Kana, Han, ellipsis, and extended-Latin
  coverage;
- preferred locale script where detectable.

The best face per file is retained, and up to four files form the fallback
chain.

## Layout contract

Phase 9 layout is intentionally bounded and deterministic:

- Unicode scalar values only;
- same-face kerning;
- combining marks receive zero advance;
- character-level CJK-friendly line breaking;
- explicit maximum width and line count;
- optional U+2026 ellipsis;
- no hidden platform text layout service.

This is sufficient for note lyrics, technical lanes, inspector labels, menus,
and product metadata in Korean, Japanese, Chinese, and Latin. It is not a claim
of full Unicode shaping.

## Rendering contract

Glyphs are rasterized into an 8-bit alpha bitmap. `RasterCanvas` multiplies the
coverage by the requested text color alpha and composites it into the BGRA
surface. The graphics code never enters the real-time audio callback.

## Cache

The service caches complete rendered strings by UTF-8 bytes and all effective
style values. Limits are:

```text
entries  512
memory   32 MiB
```

Eviction is least-recently-used. Cache contents are presentation-only and never
enter project persistence or audio render identity.
