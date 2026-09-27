# Bundled UI fonts

The EMO and SCENE looks draw with these faces. Every one is licensed under the SIL Open Font License
1.1 (the `OFL.txt` beside it) and was downloaded from the Google Fonts repository at commit
`23e54b51ddffbc7713c583748e3bd86f62b1fa4a`. `manifest.json` pins each file by size and SHA-256.

| Role | Face | Used for |
|---|---|---|
| Ui, UiMedium, UiSemibold, UiBold | Barlow Regular, Medium, SemiBold, Bold | Interface text |
| Display | Barlow Condensed SemiBold | EMO headings and display text |
| DisplayRounded | Fredoka at weight 600 | SCENE headings and display text |
| Mono | DM Mono Medium | Readouts: transport, values, times |

The standalone app and the plug-in register the faces for their own process only
(`CTFontManagerRegisterFontsForURL` with `kCTFontManagerScopeProcess`), after checking each file's
hash against the manifest. A face that is missing or does not match is skipped, and its role falls
back to a system face. `SEAM_UI_FONTS` overrides the directory; `SEAM_UI_FONTS=system` uses system
faces only.

After a deliberate update, run `python3 scripts/verify_bundled_fonts.py --write` and review the
diff. The OFL forbids selling the fonts by themselves and reserves some names for modified
versions; the files here are unmodified.
