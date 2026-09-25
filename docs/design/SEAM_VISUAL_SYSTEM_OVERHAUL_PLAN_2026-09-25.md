---
title: Project SEAM visual system overhaul — Emo and Scene design modes
date: 2026-09-25
status: approved visual direction; implementation details superseded by fidelity specification
baseline_commit: 7e23fb6041fd3e84995d9841165f792ee769c706
supersedes: the visual-direction parts of docs/design/NATIVE_EDITOR_DESIGN_SYSTEM.md and the character-direction parts of docs/brand/CHARACTER_BIBLE_DRAFT.md, once P4 lands
language: English
---

# Project SEAM visual system overhaul

> Both EMO and SCENE concept directions are owner-approved. For implementation, use [the source-reviewed fidelity specification](SEAM_UI_FIDELITY_REVIEW_2026-09-25.md) and its geometry contract. They correct this draft's Cocoa scaling, transparency diagnosis, layout sizes, font policy, renderer/memory assumptions, character migration and delivery sequence. This document retains the visual rationale; it is not evidence that the redesign has been implemented.

## 1. Goal

Project SEAM should look and feel like a futuristic virtual-singer instrument with a recognizable protagonist, instead of a grid of flat rectangles. The owner selected two switchable design modes:

- **Emo mode**: a theatrical mid-2000s emo mood, drawn from the image of a pale, black-haired, eyeliner-heavy post-hardcore frontman: ink black, bone white, blood red, stage-light drama, comic-book ink.
- **Scene mode**: the late-2000s scene-kid mood: neon pink, cyan and lime, zebra and checker patterns, pixel stars and hearts, jelly bracelets, glitter and loud color.

Both modes must feel *futuristic*: dark glass surfaces, light that glows from edges, smooth animated curves, holographic highlights and precise instrument-grade readouts. The retro subculture supplies the attitude; the futuristic rendering supplies the finish.

The protagonist in `docs/design/references/protagonist-reference-sheet-2026-09-25.png` becomes the default singer and face of the product, in the same product role a flagship virtual singer plays for its engine. The two uploaded copies of the sheet were byte-identical (SHA-256 `63df5a68c4429d41fb1d40fe2e2008c9263e09f521da82ac185b3f8ec85cad4f`).

### Non-negotiable rule: no real brand names

No shipped string, asset, icon, font name, file name, manifest, theme name, or character element may contain or imitate an existing brand, band, album, song, retailer, footwear or apparel mark, social network, messenger, phone maker, TV show, or competing voice product. The mood references are for internal direction only and are not stored in this repository because they contain third-party logos. Section 9 turns this rule into an automated check and a visual review step.

## 2. Diagnosis: why the editor feels like boxes

The current look comes from the drawing layer itself, so recoloring alone cannot fix it.

| Cause | Evidence | Effect on screen |
|---|---|---|
| The canvas has only square primitives | `RasterCanvas` exposes `fillRect`, `strokeRect`, `line`, `drawText`, `drawVerticalGradient` and `drawImageNearest` only (`libs/seam-native-ui/include/seam/native_ui/pixel_surface.hpp:65`) | Every note, button, panel and lane is a hard-edged rectangle. |
| Lines have no anti-aliasing | `RasterCanvas::line` is a Bresenham walk that stamps square blocks (`libs/seam-native-ui/src/pixel_surface.cpp`) | Pitch curves, vibrato and automation look jagged and technical rather than vocal. |
| No transparency pipeline for art | Character states and mouth shapes load through `PixelSurface::loadPpm` (`libs/seam-native-ui/src/character_presentation.cpp:55`, `:69`); PPM carries no alpha channel | The character can only appear as an opaque rectangle and can never be layered into the scene. |
| Images scale with nearest-neighbor only | `drawImageNearest` | Portraits alias and look like placeholders at non-integer scales. |
| Type is too small | Scene text sizes include 5, 6, 7 and 8 px (`editor_scene.cpp:1157` draws phoneme text at 6 px; `editor_scene.hpp:371`, `:546`, `:559` set 7 px sizes) | Combined with the plug-in scale defect below, text became unreadable in FL Studio. |
| One flat color struct | `EditorSceneTheme` (`editor_scene.hpp:70`) is 58 fixed colors with no roles, type scale, radii, elevation, textures or variants | A second design mode would mean copying and hand-editing the whole struct, and nothing but color can change. |
| The plug-in editor is inverted and under-scaled on macOS | FL Studio test F03 FAILED (`/Users/lhs/Documents/SEAM-FL-Test-2026-09-25/evidence/F03-inverted-tiny-editor-2026-09-25.png`). `embedded_view_appkit.mm:247` draws the CGImage without the vertical flip that `native_window_appkit.mm:499` applies, and `updateSurface()` (`embedded_view_appkit.mm:452`) sizes the pixel surface with the backing scale while the `RasterCanvas` receives only `scale_` | The plug-in, which is how most musicians will meet the product, currently looks broken regardless of theme. |
| The layout is one piano roll with thin lanes | Current frame: toolbar, ruler, piano roll, four technical lanes, status (`NATIVE_EDITOR_DESIGN_SYSTEM.md`) | There is no arrangement overview, no voice/part inspector, and no place where the singer feels present. |

## 3. What to take from the reference editor, and what to leave

The reference screenshots show a mature vocal editor. SEAM should adopt its *information architecture*, which is common to DAWs, while inventing its own visual language and never copying its icons, badges, names or colors.

Adopt:

1. A track/arrangement strip above the note editor, with colored track headers and part blocks that preview their notes.
2. A right-hand inspector with Part, Voice and Style sections, a searchable voice list, and a large voice card that shows the singer.
3. Notes that carry the lyric on the note and the phoneme beneath it.
4. The rendered waveform drawn *inside* each note once audio exists, with the pitch curve drawn over the notes as a smooth colored line.
5. An expression lane at the bottom with a smooth filled curve and a lane selector.
6. A translucent full-body singer presence in an empty corner of the editor.
7. A compact, high-contrast transport readout for bars:beats:ticks, BPM and meter.

Leave: the reference product's names, icons, track-type badges, voice names, blue/green/pink track palette, and window chrome.

## 4. Shared design system (both modes)

### 4.1 Layout

    ┌──────────────────────────────────────────────────────────────────────────────┐
    │ TOP BAR 44px: mark · menus · tools · grid · [TRANSPORT READOUT] · EMO|SCENE │
    ├───────────────┬────────────────────────────────────────────┬─────────────────┤
    │ TRACK HEADERS │ ARRANGEMENT STRIP (collapsible, 96–220px)   │                 │
    ├───────────────┴────────────────────────────────────────────┤  INSPECTOR      │
    │ EDITOR TOOLBAR: tools · grid · voice · style · take         │  320px          │
    ├──┬─────────────────────────────────────────────────────────┤  Part           │
    │K │ PIANO ROLL                                              │  Voice card     │
    │E │  notes = capsules, lyric + phoneme, waveform inside,    │  (protagonist)  │
    │Y │  pitch curve overlay, vibrato envelope                  │  Voice list     │
    │S │                               [singer stage, optional]  │  Style chips    │
    ├──┴─────────────────────────────────────────────────────────┤                 │
    │ LANE TABS: PIT · DYN · BRE · FOR · PHN · UNIT · SEAM        │                 │
    │ EXPRESSION LANE (smooth filled curve)                       │                 │
    ├────────────────────────────────────────────────────────────┴─────────────────┤
    │ STATUS: render state meter · diagnostics · export                            │
    └──────────────────────────────────────────────────────────────────────────────┘

The existing technical lanes (phoneme, unit, seam, pitch, dynamics) become tabs in one lane area, with one lane expanded at a time and a two-lane split option. This replaces five thin stacked lanes, which was a main source of tiny text. The inspector collapses to an icon rail below 1180 px width; the arrangement strip collapses below 720 px height. The CLAP editor uses the same layout with the arrangement strip collapsed by default, because the host already owns the arrangement.

### 4.2 Type

| Role | Size (logical px) | Weight | Use |
|---|---|---|---|
| Display | 20–28 | Condensed heavy, uppercase, wide tracking | Mode logo, splash, voice card name |
| Title | 14 | Semibold | Panel headers |
| Body | 12 | Regular | Inspector values, lists, dialogs |
| Label | 11 | Medium | Note lyrics, lane labels, buttons |
| Micro | 10 | Medium, tabular figures | Ruler numbers, phoneme tags only |
| Readout | 18–22 | Monospaced or tabular | Transport time, BPM |

Nothing below 10 px is allowed. Current 5–9 px sizes move to the Label or Micro roles, and labels that do not fit use the existing `EditorLabelPolicy` compact/hidden modes instead of shrinking. UI text uses the system face through the existing `TextEngine`; the display face must be an original or permissively licensed (for example OFL) font with its license recorded, and its name must pass the Section 9 check.

### 4.3 Shape, depth and light

- Radii: notes 4 px (capsule when height allows), buttons and chips 6 px, panels 10 px, voice card 14 px.
- Surfaces are dark "glass": a base fill, a 1 px inner highlight on the top edge, a 1 px outer border at low alpha, and an optional 8–16 px soft glow in the mode accent for focused or active elements.
- Three elevations: canvas, panel, floating (menus, detail popovers). Elevation changes border brightness and glow and does not add heavy drop shadows.
- Curves (pitch, vibrato, expression) are anti-aliased 1.5–2 px strokes with a 6 px accent glow; expression lanes fill under the curve with a vertical gradient fading to transparent.
- Every decorative texture stays behind content at 3–8% opacity and never sits under text inside notes.

### 4.4 Motion (all honor Reduce Motion from `accessibility_preferences.hpp`)

| Event | Motion | Duration |
|---|---|---|
| Note added | scale 0.92 → 1 with glow flash | 120 ms |
| Selection | glow ring fades in | 90 ms |
| Render progress | mode-specific meter (Sections 5 and 6) | continuous |
| Render complete | brief accent sweep across the rendered notes, waveform fades in | 250 ms |
| Theme switch | cross-fade of cached background layer, then foreground repaint | 200 ms |
| Singer idle | blink every 4–7 s, subtle 2 px breathing drift | continuous, paused when window inactive |

### 4.5 Accessibility floors

Text contrast at least 4.5:1 against its actual background in both modes; selection and warnings never rely on color alone (glow ring plus outline weight plus icon); a High Contrast variant of each mode removes textures and glow; hit targets at least 24 px; the singer stage is decorative, excluded from hit-testing and exposed to assistive technology only through the voice card.

## 5. Emo mode

**Mood.** A dark stage right before the lights hit. Ink black and bone white carry the interface; blood red is rare and therefore loud. Comic-book ink, halftone, stitched seams and torn-paper edges supply texture. The futuristic layer is black glass with red hairline light and a vital-monitor style render meter.

| Token | Value | Use |
|---|---|---|
| canvas | #0B0A0C | window and piano-roll background |
| surface | #141216 | panels |
| surfaceRaised | #1C1A1F | popovers, voice card |
| border | #2E2A31 | panel borders |
| gridWeak / gridStrong | #1A171C / #2B272E | grid lines |
| textPrimary | #EDE8E3 (bone) | primary text |
| textSecondary | #9C9499 | secondary text |
| accent | #D1143A (blood red) | selection, playhead, active controls |
| accentDeep | #7A0C22 | pressed states, meters |
| accentCold | #B8BCC6 (steel) | secondary highlights, pitch curve base |
| noteFill / noteStroke | #221619 / #5A2630 | notes |
| noteSelected | #D1143A with 10 px glow | selected notes |
| pitchCurve | #F2EEEA stroke with #D1143A glow | pitch overlay |
| waveInNote | #EDE8E3 at 55% | waveform inside notes |
| warning / error / success | #E0A040 / #FF3355 / #B8D0C0 | diagnostics |

Motifs, all original: **stitched seam lines** (dashed stitches as section dividers, tying directly to the product's seam concept); halftone dot fields in empty areas; a torn-paper edge under the top bar; a cracked-heart and safety-pin icon family for status; an ink-splatter illustration only on empty states. Render meter: a thin red EKG line that flatlines on error and beats on completion.

Singer presentation: the protagonist in monochrome ink, with red used only as a spot color (eyeliner shadow, a ribbon or thread on the bracelet).

## 6. Scene mode

**Mood.** A bedroom covered in neon, glitter and stickers, rendered as a hologram. Several saturated accents coexist, so structure has to come from consistent roles: pink for selection, cyan for curves, lime for time.

| Token | Value | Use |
|---|---|---|
| canvas | #0D0716 | background |
| surface | #170E24 | panels |
| surfaceRaised | #22123A | popovers, voice card |
| border | #3A2358 | panel borders |
| gridWeak / gridStrong | #1A1028 / #2E1C46 | grid lines |
| textPrimary | #FFF4FB | primary text |
| textSecondary | #C9A8D8 | secondary text |
| accent | #FF2E9A (hot pink) | selection, active controls |
| accentCurve | #1DE9FF (cyan) | pitch and expression curves |
| accentTime | #B8FF3B (lime) | playhead, loop region |
| accentViolet / accentSun | #9B5CFF / #FFE14D | track colors, badges |
| noteFill | gradient #3A1A4F → #2A1540 | notes |
| noteSelected | gradient #FF2E9A → #9B5CFF with cyan 1 px outline | selected notes |
| waveInNote | #FFF4FB at 50% | waveform inside notes |
| warning / error / success | #FFE14D / #FF4D6D / #7CFFB2 | diagnostics |

Motifs, all original: zebra stripes on header bars at low opacity; a checkerboard strip along the arrangement ruler; pixel stars and hearts as status icons; track colors assigned like a stack of jelly bracelets; holographic iridescent sheen on the voice card border; a glitter burst when a render completes (Reduce Motion: a static sparkle icon). The transport readout uses a pixel-style LCD face. Render meter: a progress bar made of stacked bracelet segments.

Singer presentation: the same protagonist in a **scene outfit variant**: pink or cyan hair streak in place of the pale streak, colored bracelets, sticker-covered bag. Until that art exists, Scene mode shows the Emo art with a duotone pink/cyan tint and the asset manifest labels it a temporary variant.

## 7. The protagonist as default singer

### 7.1 Product role (amends the character bible)

`docs/brand/CHARACTER_BIBLE_DRAFT.md` describes a low-poly violet Character 01 and forbids covering editing areas. The owner has now chosen the ink-drawn protagonist as the face of the default voicebank. P4 rewrites the bible to:

- replace the low-poly canonical direction with the protagonist sheet;
- keep Full / Minimal / Off, and add **Stage**: a translucent singer figure behind the note layer in the lower right of the piano roll. It renders below grid, notes and curves; drops to 6% opacity whenever notes or the pointer enter its area; never receives input; and can be disabled per user;
- keep the rule that character presentation never affects rendering, cache keys or exported audio;
- keep the public name unapproved until `CHARACTER_01_NAMING_CLEARANCE.md` is satisfied.

Default presentation: Stage on in the standalone at 16% opacity, voice card always on, Minimal portrait in the CLAP editor with Stage available.

### 7.2 Where the singer appears

| Surface | Presentation |
|---|---|
| Splash and welcome | full-body key art per mode, product mark, new/open project |
| Inspector voice card | three-quarter portrait with state expression and name plate |
| Piano roll Stage | full-body translucent figure (optional) |
| Toolbar | circular portrait with render-state ring |
| Render states | expression changes: neutral, focused, singing, complete, warning, error |
| Lip sync during playback | six existing mouth shapes, driven by the existing performance snapshot |
| Empty project | seated pose with "double-click to write the first note" |
| Error dialogs | head-in-hand panel from the sheet |

The reference sheet already contains most needed expressions: the neutral front face, the soft smile, the side glance (warning), the eyes-closed tilt (singing), and the head-on-hand pose (error or empty state).

### 7.3 Art pipeline requirements

1. **Provenance and rights first.** Record who created the sheet, with what tools and under what rights, in `assets/character-01/PROVENANCE.md` before any derived asset ships. If generative tools were involved, record the tool, terms and human edits.
2. **Remove brand-like details.** The sneakers in the sheet show a star ankle patch and a toe-cap and sole design associated with a real footwear brand. Redraw the patch as an original mark (a stitched seam glyph fits the product) and alter the toe and sole design. Check the shirt graphic, belt and bag buttons for any readable real mark.
3. **Clean, layered production art.** Redraw at 4× target size with separate layers for back hair, body, face, eyes (open, half, closed), mouth (six shapes), front hair and accessories, so blink, lip sync and breathing are compositing operations.
4. **Two outfit variants.** Emo (canonical) and Scene.
5. **Transparent runtime format.** Replace PPM with an alpha-capable format (Section 8, P2).

## 8. Implementation plan

### P0 — make the plug-in editor usable (blocker; about 1 day)

- `libs/seam-clap-editor/src/embedded_view_appkit.mm`: in `draw()`, apply the same translate/scale flip used by `native_window_appkit.mm:499` around `CGContextDrawImage`.
- In `updateSurface()` and `draw()`, pass the *effective* scale (host scale × backing scale) to `RasterCanvas`, so logical layout matches the physical surface. Clamp host scale to the CLAP range and verify `guiSetScale` behaviour in FL Studio.
- Raise every font size below 10 px to the Section 4.2 roles.
- Add a test that paints a known asymmetric frame through the AppKit embedded path (or a shared presentation helper) and asserts row order, plus a scale test asserting the logical-to-physical ratio.
- Rebuild, install a distinct candidate, and re-run FL tests F02–F05.

### P1 — design tokens and the mode switch (about 2 days)

- New `libs/seam-native-ui/include/seam/native_ui/design_tokens.hpp`: `enum class DesignMode { Emo, Scene }`, `enum class ContrastVariant { Standard, High }`, and a `DesignTokens` struct grouped as `ColorRoles`, `TypeScale`, `ShapeTokens` (radii, stroke widths), `LightTokens` (glow radii and alpha), `TextureSet` and `MotionTokens`.
- `design_tokens.cpp`: `tokensFor(DesignMode, ContrastVariant)` returning the Section 5 and 6 values. Keep `EditorSceneTheme` temporarily as a view derived from tokens so the 2,344-line `editor_scene.cpp` can migrate call site by call site.
- Persist the choice as an application preference beside `AccessibilityPreferences`. Keep it out of the project file and render identity, so a project looks the same to collaborators in either mode and switching modes never re-renders audio. The CLAP editor reads the same user preference.
- Add the EMO | SCENE segmented switch to the top bar and to the View menu on AppKit, Win32 and X11.
- Tests: every role meets its contrast floor in both modes and both contrast variants; switching mode does not change project bytes or render identity.

### P2 — paint engine upgrade (about 5–7 days)

Extend the in-house `RasterCanvas`, which keeps rendering deterministic and hash-testable on all three platforms, with:

- coverage-based anti-aliased `fillRoundedRect`, `strokeRoundedRect` and capsules (signed-distance coverage);
- anti-aliased `strokePolyline` with width, round joins and caps, and cubic-curve flattening for pitch and expression curves;
- `fillPath` for area-under-curve fills with linear gradients;
- linear and radial gradients in any direction, plus a vertical "glass" preset;
- `drawGlow`: a separable blur of a coverage mask, cached per shape size and radius;
- `fillPattern` for halftone, stripe, zebra and checker textures from small tiles;
- `drawImage` with bilinear filtering, premultiplied alpha and opacity;
- a clip stack and offscreen layers, so the static background (grid, textures, stage figure) is cached and only notes, curves and the playhead repaint per frame.

Image format: add a bounded RGBA loader for character packages. Recommended: QOI (tiny, MIT-licensed, trivial to bound) or a SEAM raw premultiplied RGBA format converted at build time. Update the Character Package format to v4 with alpha assets and layer metadata, keeping the existing size limits and validation.

Performance gate: the existing 10,000-note paint benchmark (2.19 ms p95 today) must stay under 8 ms p95 at 2× scale in both modes, with glow and textures enabled.

Spike first (half a day): compare this in-house route with ThorVG (MIT) or Blend2D (zlib) on output quality, binary size, determinism across platforms and the benchmark. Choose the in-house route unless a library wins clearly on quality at equal determinism.

### P3 — layout and component rebuild (about 6–9 days)

- `editor_frame_layout.*`: new regions for arrangement strip, editor toolbar, lane tabs and inspector; responsive collapse rules from Section 4.1.
- Notes: rounded capsules, lyric centered, phoneme tag underneath in Micro size, waveform inside the note once audio for that revision is published, and pitch curve overlay across notes. The waveform comes from a downsampled envelope cache owned by the runtime from published PCM; paint never computes it.
- Lanes: one tabbed lane area; expression curves drawn with P2 curves and fills.
- Inspector: Part (name, region, start), Voice (search, list, voice card, language), Style (chips grouped by type and color), with the existing renderer applicability and refusal text from the U1.5 row.
- Transport readout component with tabular figures.
- Keep `EditorSceneLayout` as the single owner of bounds so painting, hit-testing and accessibility stay aligned, as the existing design system requires.

### P4 — protagonist integration (code about 3–4 days; art is the dependency)

- Character Package v4 loader, layer compositor, blink and breathing scheduler driven by the injectable UI clock.
- Stage presentation mode with the rules in 7.1, voice card, toolbar ring, splash and empty-state art.
- Map states and mouth shapes to the new layers.
- Update `CHARACTER_BIBLE_DRAFT.md`, `CHARACTER_01_ASSET_READINESS.md` and the manifest; keep `developmentOnly: true` until provenance, rights and naming clearance pass.

### P5 — mode motifs and motion (about 2–3 days)

Texture tiles, icon families (cracked heart and safety pin for Emo; pixel star and heart for Scene), EKG and bracelet render meters, glitter burst, theme cross-fade, all behind Reduce Motion and High Contrast.

### P6 — verification (about 2 days, then per phase)

- Deterministic visual packets: both modes × both contrast variants × existing viewports × 1× and 2× scale, reproduced twice with identical hashes.
- Automated contrast check over painted text regions.
- Paint benchmark gate from P2.
- FL Studio manual rows F02–F05 re-run, plus a screenshot of each mode inside FL Studio.
- Owner review against this document, then one independent visual review.

The original 20–28-day estimate is withdrawn. Use units A–E in the fidelity specification and re-estimate after the native SING proof establishes artwork, rendering and host costs.

## 9. Name and trademark safety

- Add `scripts/check_brand_terms.py` with a curated deny list (bands and albums from the reference era, retailers, footwear and apparel brands, social networks and messengers, phone makers, competing voice products and their characters) and run it over `assets/`, UI string tables, manifests, installer metadata and `docs/manual/`. Wire it into the source-contract tests so a match fails the build.
- Allow the words "emo" and "scene", which are generic genre terms.
- Visual review checklist for every art asset: no readable real logos, no copied logotype styles, no footwear ankle patches or stripes that imitate a known mark, no band shirts.
- Theme names shown in the product are **EMO** and **SCENE**. Any flavor name added later goes through the same clearance as the character name.

## 10. Order and acceptance

| Phase | Depends on | Done when |
|---|---|---|
| P0 | — | FL Studio F03 PASS: upright, readable at 1× and 2× |
| P1 | P0 | mode switch persists, contrast tests pass, project bytes unchanged by switching |
| P2 | P1 | primitives tested, benchmark gate met, RGBA character assets load |
| P3 | P2 | new layout in standalone and CLAP, visual packets reviewed |
| P4 | P2, protagonist production art, provenance record | Stage, voice card and states live with alpha art; bible updated |
| P5 | P3 | motifs and motion in both modes, Reduce Motion verified |
| P6 | each phase | packets, benchmark, FL rows and owner review recorded |

The owner has approved the two concept images. The next proof is the actual native SING screen in both modes at the canonical 1600×900 client size, followed by responsive/host checks described in the fidelity specification. More generated mockups are not required to re-approve the direction.
