# Character package schema 2

Schema 2 keeps the legacy ID-only sample-bank association. For a full kind/ID/version/digest binding
that supports procedural and neural singers, use [schema 3](CHARACTER_PACKAGE_V3.md).

Schema 2 declares the same six required operational portraits as schema 1, plus all six performance mouth sprites and `developmentOnly`. It may also declare `mouthPlacement`:

```json
"mouthPlacement": { "x": 0.455, "y": 0.172, "width": 0.09, "height": 0.06 }
```

The rectangle is normalized to the displayed portrait (`x`, `y` are its top-left; width and height must be positive and the rectangle must remain within `0..1`). For packages with this field, each mouth PPM must have one flat background color at all four corners. The loader uses that exact corner RGB as a transparency key, preserving the mouth pixels, and the native editor draws the sprite inside the declared face rectangle. Packages without `mouthPlacement` retain the legacy side-indicator presentation.

`assets/character-01/manifest.json` declares an anchor for its current 2:3 development portrait. The value is artwork-specific, not a universal face detector or permission to use that artwork in a release. The package remains `developmentOnly`; production art, provenance, and installed-platform review are still required.
