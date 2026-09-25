# ui-design asset provenance

Status: **development only** (`manifest.json` sets `developmentOnly: true`). These files let the
EMO and SCENE SING shell render with the protagonist while the design is implemented. They are not
cleared for a public release.

| File | Role | Origin |
|---|---|---|
| `emo/portrait.png`, `scene/portrait.png` | Singer card portrait (768×768) | Generated with the built-in image generator on 2026-09-25 from the protagonist reference sheet (`docs/design/references/protagonist-reference-sheet-2026-09-25.png`), then cropped. |
| `emo/stage.png`, `scene/stage.png` | Stage figure behind the grid (RGBA, trimmed) | Generated with the same tool from the same reference, background haze removed and trimmed to the figure. |
| `emo/wordmark.png`, `scene/wordmark.png` | Header wordmark | Generated with the same tool; lettering reads only "SEAM". |

`manifest.json` records each file's SHA-256 and the SHA-256 of the generator output it was cut from.

Rules that hold for every revision:

- No real brand, band, retailer, messenger, or product name or logo appears in any asset.
- The protagonist is an original character. The EMO and SCENE looks are styling of the same
  character, not likenesses of real people.
- Before a public release, each asset must be replaced or re-cleared by the art owner, and
  `developmentOnly` must be set to `false` only with that sign-off recorded here.
