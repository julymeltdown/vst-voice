---
title: SEAM approved concepts — implementation fidelity review and binding corrections
date: 2026-09-25
baseline_commit: 7e23fb6041fd3e84995d9841165f792ee769c706
status: independently approved implementation specification; native visual match NOT_RUN
language: English
---

# SEAM approved concepts: implementation fidelity review

## 1. Verdict and authority

Both approved images are technically achievable on macOS: the composition, character portrait, translucent figure, illuminated notes, smooth curves, arc controls, and distinctive EMO/SCENE materials have no fundamental platform obstacle. **The current implementation does not reproduce them, and the previous plan alone was insufficient to guarantee the result.** The decisive remaining proof is a real native SING screen with the production artwork, fonts, data bindings, and host presentation.

This document supplies the binding corrections to `SEAM_UI_REDESIGN_CODE_PLAN_2026-09-25.md` and `SEAM_VISUAL_SYSTEM_OVERHAUL_PLAN_2026-09-25.md`. Where they disagree, this document and `ui-fidelity-contract-v1.json` govern. The owner has approved both visual directions; do not reopen the style choice. Verification of a working application remains separate from that approval.

| Question | Current finding |
|---|---|
| Can the visual language be implemented? | YES, subject to the explicit work below. This is an engineering assessment. |
| Is the existing renderer sufficient unchanged? | NO: anti-aliased geometry, clips, translucent layers, and image sampling need work. |
| Are the approved character assets ready to ship? | NO: the approved images are flattened concepts; runtime portraits/layers and exact asset identities are missing. |
| Does the current plug-in match either image? | NO evidence of that; native fidelity is NOT_RUN. The previous FL Studio F03 failure remains unresolved by this review. |
| Can we honestly promise literal equality to both complete PNGs? | NO: they contain different arrangements, invented text, incompatible layout proportions, exterior decoration, and illustrative parameter values. |
| What will count as success? | Matched visual identity and measured geometry in the real product, with documented corrections to the concepts and independently verified interaction. |

This review changes specifications and adds an executable specification check. It does not repair, rebuild, install, or replace the plug-in. It does not advance the audio-quality or Beta GO gates.

## 2. Source findings that change implementation

Paths below are relative to the repository at the baseline commit.

| Finding | Source evidence | Required correction |
|---|---|---|
| Cocoa scale advice was wrong | `libs/seam-clap-editor/src/embedded_view_appkit.mm:141`, `:221`, `:452`; `plugin_entry.cpp:1155` | Cocoa uses logical points. Do not multiply a CLAP host scale by the backing scale. See §5. |
| The upside-down presenter remains in source | `embedded_view_appkit.mm:247`, `:737`; reference correction at `libs/seam-native-ui/src/native_window_appkit.mm:490` | Share or duplicate the tested final image-presentation transform, preserving input coordinates. A software-raster screenshot cannot prove this fix. |
| Existing compositing forces opacity | `libs/seam-native-ui/src/pixel_surface.cpp:261`, `:380` | Both pixel blending and text blending set final alpha to 255. They cannot paint translucent cached layers correctly. Keep the legacy path opaque; add an alpha-correct compositor. |
| Transparency is partially present already | `pixel_surface.cpp:486`; `character_presentation.cpp:69` | Nearest sampling already honors source alpha, and mouth sprites use a corner-color key. The missing feature is robust alpha artwork/layers and filtered resampling, not all transparency. |
| Font roles do not automatically enter the cache | `libs/seam-text/src/text_engine.cpp:155`, `:263`, `:312`; `text_engine.hpp:40` | Cache fields are manually serialized and face selection is independent of the proposed role. Change both, not just `TextStyle`. |
| A 340-pixel wide rack undercuts the images | Approved 1672×941 concepts | The rack occupies roughly 28% of the image; freeze a 440-point rack at the canonical 1600-point client width. |
| The proposed layer budget fails arithmetic | Four full BGRA layers plus final surface | At 1440×900@2× these require 98.88 MiB before masks, images, text, or transition buffers. At 1600×900@2×: 109.86 MiB. Replace the allocation strategy (§8). |
| Cache invalidation was incomplete | Planned L0–L3 table; `RenderStatusView`, character performance | Mode, contrast, fonts, data bindings, scroll, playback-dependent knob values, clip geometry, and live text all need explicit dependencies. A playing frame is not necessarily only a playhead update. |
| The character-v4 sketch loses existing semantics | `libs/seam-character/include/seam/character/character.hpp:28`, `:53`; `src/character.cpp:72` | Retain schema-three exact singer kind/id/version/content identity, required legacy identity fields and states. Preserve schemas 1, 2 and 3. Do not put package validation only in native UI. |
| A sample layer is not a singer's supported range | `libs/seam-authoring-runtime/include/seam/authoring/voicebank_browser.hpp:13`; `character/performance.hpp:32` | `rootPitchLayers` describes recorded sample roots, not a qualified vocal range. Display “Sample roots” or a separately supported range with provenance. Handle procedural and neural singers too. |
| Concept numbers do not match domain units | `libs/seam-editor-ui/src/expression_lane.cpp:23` | Formant is semitones; the other channels have normalized or bipolar domains. SCENE's pictured formant 32.5 is outside the current ±24 range. Use §7 conversions. |
| Repeated thin labels would reproduce the overflow bug | `libs/seam-editor-ui/include/seam/ui/note_visual_layout.hpp:23` | Apply label allocation after the existing overlap-band calculation, with clipping and density rules. Never place every lyric above its note unconditionally. |
| Arrangement functionality already exists | `editor_scene.hpp:244`, `:1299`; `editor_scene.cpp:438` | Improve and re-home the existing arrangement UI; the earlier statement that no arrangement/inspector exists was too broad. |
| The proposed benchmark number is not a new-UI measurement | `benchmarks/phase5_benchmark.cpp:70` | The current workload paints 1280×720 at 1×. It does not establish 1600×900@2× performance with textures and blur. |
| The optional graphics adapter is not usable by toggling a flag | `CMakeLists.txt:1371` | Enabling `SEAM_ENABLE_IPLUG2_SKIA` currently raises a fatal error. Do not describe it as a ready backend. |

Upstream confirms the Cocoa logical-pixel contract and that ignored `set_scale` calls return false: [CLAP GUI contract](https://github.com/free-audio/clap/blob/main/include/clap/ext/gui.h). Quartz provides bitmap contexts and compositing needed by the proposed macOS implementation: [Apple graphics contexts](https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/drawingwithquartz2d/dq_context/dq_context.html).

## 3. Freeze the approved design instead of approximating it repeatedly

### 3.1 Immutable source references

| Reference | Size | SHA-256 |
|---|---|---|
| `concepts/concept-emo-sing-2026-09-25.png` | 1672×941 | `62ad02f76b7d80921cb18e59d28d5b4d897f2ac19561216987d1ff36bbf0bc16` |
| `concepts/concept-scene-sing-2026-09-25.png` | 1672×941 | `803c7031eeba3d1e30954dc9f19d1045bca0c51c44c33972a2c8256ae3125476` |
| `references/protagonist-reference-sheet-2026-09-25.png` | 1055×1491 | `63df5a68c4429d41fb1d40fe2e2008c9263e09f521da82ac185b3f8ec85cad4f` |

Compare the plug-in **client area**, excluding host title bars, desktop wallpaper, and the concept's exterior composition. Approximate source frame crops are EMO `(25,29,1623,886)` and SCENE `(72,24,1528,892)`. These are visual framing guides, not native pixel baselines. Do not stretch these rectangles over a working editor and call that fidelity.

The two concepts differ in header height, data, note placements and portrait treatment. Use the shared canonical layout below in both modes. Mode switching must preserve note locations, scroll, zoom, control bounds and keyboard focus. Mode-specific wordmarks, materials, icons, portraits and color are allowed to change.

### 3.2 Changes allowed when translating to production

1. Replace invented slogans, singer metadata, style names and parameter readings with actual application state. Add the required EMO/SCENE switch.
2. Keep host/window controls outside the client layout. Exterior chains, glitter and handwriting are reference presentation only.
3. Keep the large portrait. Put the stage figure behind the grid at the specified opacity; keep empty regions for writing and reading notes.
4. Use one canonical geometry rather than reproducing the inconsistent dimensions of the two images.
5. Increase functional text to the measured roles below; preserve the expressive lettering in the product wordmarks only.
6. Match waveforms, curves and progress to real data. Missing/stale audio has an honest empty/stale state.

These are the only default deviations. A blank substitute portrait, generic system-font logo, plain square notes, or a thin flat card border in place of the approved materials is a visual failure, even if automated tests pass.

### 3.3 Canonical client-area geometry

Coordinates are logical points at 100% UI zoom. Reference client size: **1600×900**. Backing scale is independent (1× or 2×). The same geometry applies inside standalone and CLAP at this size; host embedding alone must not collapse the singer rack.

| Region | x | y | width | height |
|---|---:|---:|---:|---:|
| Header | 16 | 16 | 1568 | 80 |
| Piano-roll card including tools and ruler | 16 | 108 | 1112 | 576 |
| Tools | 24 | 116 | 1096 | 28 |
| Ruler | 80 | 148 | 1040 | 24 |
| Keyboard | 24 | 172 | 56 | 504 |
| Musical grid | 80 | 172 | 1040 | 504 |
| Lane card | 16 | 696 | 1112 | 148 |
| Lane tabs | 24 | 704 | 1096 | 28 |
| Lane plot | 56 | 740 | 1064 | 96 |
| Singer card | 1144 | 108 | 440 | 360 |
| Expression card | 1144 | 480 | 440 | 248 |
| Style card | 1144 | 740 | 440 | 104 |
| Status bar | 16 | 856 | 1568 | 28 |

The lane plot's x=56 includes a 24-point Y-axis gutter. Its **musical plot** is `(80,740,1040,96)`, sharing the ruler/grid x=80..1120 and the exact same tick-to-x transform. Draw lane automation and its playhead in that musical rectangle; never stretch the timeline across the gutter. This relationship is encoded as `laneTimePlot` and `sharedTimeAxis` in the contract.

Singer portrait outer ring: `(1220,148,288,288)`, centered at `(1364,292)`. Reserve the title strip and the footer for actual voice identity/status. The portrait must not become a small thumbnail merely to accommodate metadata: detailed metadata belongs in the voice popover.

Expression card body: three columns, two rows; each dial has a 56-point diameter and a 24-point minimum hit region. Labels have an 18-point line box; values use fixed-width figures. The STYLE card holds two rows of 28-point chips where possible; overflow is a list/popover, never a third row painted outside the card. The signal rail occupies the gap at x=1136 and has no independent hit targets.

The header reserves a 208×48 wordmark, five workspace tabs, a mode switch, transport, output meter, and settings. The contract fixes their exact canonical rectangles: tabs `(264,24,400,64)`, mode `(720,42,132,28)`, transport `(880,34,432,44)`, output `(1336,34,160,44)`, settings `(1528,40,32,32)`. At constrained width, the title/secondary transport information collapses before musical content or typography does. Additional workspace tabs appear only when their commands are functional; the first proof may keep one active SING tab and clearly unavailable tabs.

### 3.4 Responsive rules

Use a small `SingLayoutMetrics` structure over the existing `EditorFrameLayout` first; a general flexbox solver is not a prerequisite for the first proof. Produce the same snapshot for paint, hit-testing and semantics. Later extraction into `FrameTree` must preserve these bounds. The snapshot needs stable domain identity and generation, semantic parent, effective clip, local-to-client transform, z order and hit policy as well as the rectangle. Index-only widget IDs cannot identify an in-progress gesture after a list is reordered.

```cpp
struct SingLayoutMetrics {
  double edge = 16, header = 80, gap = 12, status = 28;
  double rackGap = 16, minTimeline = 480;
};
// W and H are viewport dimensions after applying the user's UI-zoom inverse.
// Musical zoom is separate and does not change these shell metrics.
double rackWidth(double W) {
  if (W < 860) return 44;     // accessible inspector drawer button
  if (W < 1100) return 56;    // inspector rail / popovers
  return std::clamp(W * 0.275, 320.0, 440.0);
}
```

At 1440×900 the rack is 396 points; at 1280×800 it is 352 points. Keep functional font sizes constant. When the rack's 736-point full content is taller than its viewport, it scrolls; do not squeeze portraits and knobs into unreadable strips. At compact widths the portrait appears in the singer drawer; Stage is off. Below 720 logical points after UI zoom, use horizontal shell scrolling or an explicit minimum-size message with reachable scale controls; do not silently apply a second shrink transform.

At height <720 use a 64-point header, a 112-point lane, and overflow menus for secondary controls. Test 720×480, 860×640, 1100×720, 1280×800, 1440×900 and 1600×900. Add 3840×2160 as a stress case, with bounded caches rather than full-size duplicate layers. Expanded technical lanes and the arrangement are separate persisted views with explicit budgets; the approved reference fixture has the arrangement collapsed.

Internal track/region selection remains available in CLAP: the DAW does not own SEAM's internal arrangement. Keep a track/region switcher and the existing arrangement actions even when the overview is collapsed. Exclude parent-child overlap, intentional overlays, and note overlap from the sibling-layout check; instead test effective clips and the winner of a hit at each occluded location.

## 4. Art and typography are implementation dependencies

### 4.1 Assets that must exist for the first matched SING proof

| Asset | Runtime requirement | Matching requirement |
|---|---|---|
| EMO wordmark | Original vector paths or alpha atlas; 208×48-point slot | Bone/ink serif treatment with distressed edges; a generic font substitution fails. |
| SCENE wordmark | Original vector paths or alpha atlas; same slot | Brush/angular lettering and controlled neon edge; preserve the same SEAM name. |
| Two portrait assets | At least 768×768 RGBA each | Same protagonist, face silhouette, fringe, gaze, striped sleeve and ink density as approved; SCENE accents are authored separately. |
| Two Stage assets | 1024×1800 RGBA each, trimmed transparent bounds and anchor metadata | Preserve silhouette, accessories and posture. Never stretch an entire white reference sheet behind the grid. |
| EMO material atlas | Tileable ink fibers, torn seam, halftone | Seeded procedural noise alone is not accepted as an exact reproduction of the approved art. |
| SCENE material atlas | Controlled stars, zebra/checker tiles, highlights | Preserve dark reading surfaces; high-intensity glitter stays on the outer decorative margin. |
| Original icons | Shared 24-point path grid, optical adjustments per icon | Match stroke weight and alignment, with EMO/SCENE accent variants. |
| UI fonts | Pinned font files, face indexes, coverage and license notices | Stable measured metrics for Latin, Japanese and Korean fixtures. |

Keep the images in `docs/design/concepts` as references. Do not ship a cutout of a whole flattened concept as an interactive UI. Export independent art assets without baked-in labels, controls or backgrounds. Avoid regenerating the protagonist every time a state is needed: align eyes/mouth to one approved pose, then derive states from that master.

Add an asset manifest listing source hash, runtime hash, dimensions, alpha mode, color space, content bounds, focal point, and UI role. Existing `assets/character-01/manifest.json` remains a development package until replaced deliberately. Keep provenance and original brand-free character treatment required by the owner. Third-party license notices remain intact in legal/dependency files; they are not product branding and must not be deleted by a term scanner.

For the first proof, a neutral portrait and static Stage figure per mode are sufficient. Blink, six mouth shapes, state portraits and outfits follow only after the static rendering is visually accepted. A temporary duotone EMO image may support engineering, but it does not pass SCENE portrait fidelity.

### 4.2 Exact text implementation

| Role | Font size / line box in points | Use |
|---|---|---|
| Body | 13 / 18 | Menus, inspector values, instructions |
| Label | 12 / 16 | Controls, note labels when inside a capsule |
| Lyric | 13 / 18 | Note lyrics when space permits |
| Small label | 11 / 14 | Tabs, secondary metadata, phoneme lane |
| Ruler micro | 10 / 14 | Nonessential ticks only |
| Panel title | 12 / 16 | Tracked uppercase Latin headings |
| Knob value | 16 / 20 | Signed value with units nearby |
| Transport | 22 / 28 | Stable-width time display |

`TextStyle::pixelHeight` remains physical pixels at rasterization. Convert logical size exactly once using the backing/UI transform. Measurements and rendering must use the same role, face and scale. A 13-point label must occupy the same logical height on a 1× and 2× screen.

Implementation targets:

1. `libs/seam-text/include/seam/text/text_engine.hpp`: add `FontRole` and a digit-spacing policy with backward-compatible defaults.
2. `libs/seam-text/src/text_engine.cpp`: select the preferred face by role before glyph fallback; compute baseline/line height from that role's face; retain per-glyph fallback. Do not leave `faces.front()` as the primary face for every role.
3. Extend the explicit `cacheKey` serialization for role and digit policy. If fonts can reload, clear the cache or include an immutable font-set generation/hash. Do not serialize raw padded structs.
4. Transport/readout roles use pinned monospaced digits or explicitly equalized digit advances, including minus, decimal and colon alignment. A `tabularNumbers` boolean alone provides no glyph-layout behavior.
5. Render text as an alpha mask through the new compositor for translucent layers. The old `RasterCanvas::drawText` forces opaque destination alpha and cannot be reused there unchanged.
6. Test real Japanese and Korean lyrics plus long Latin labels. Track glyph fallback and clipping explicitly. Keep display lettering out of general controls.

## 5. Repair coordinates before evaluating style

The Cocoa contract is: view size and input are logical points; backing pixels come from the view/window; `clap_plugin_gui::set_scale` is ignored for Cocoa and reports false. Any user-selected UI zoom is a separate presentation preference with a matching inverse for input and accessibility.

Suggested macOS-only changes, subject to the presenter tests:

```cpp
// AppKitEmbeddedView: conceptual code; adapt error handling to existing Result APIs.
bool setScale(double) override { return false; } // Cocoa logical-pixel API

void updateSurface() {
  // Use convertRectToBacking or the backing scale after attachment to a window.
  // Quantize dimensions once; retain logical bounds from NSView.
  backingScale_ = validBackingScale(view_); // detached view defaults to 1
  resizeBackingSurface(view_.bounds.size, backingScale_);
  runtime_.resize(view_.bounds.size.width, view_.bounds.size.height);
}
void draw() {
  RasterCanvas canvas{surface_, backingScale_, textEngine_.get()};
  runtime_.paint(canvas);
  presentTopLeftBgraImage(context, surface_, view_.bounds);
}
```

Also change `plugin_entry.cpp::guiSetScale` so the macOS pre-view case does not claim an ignored scale was applied. Do not change Win32/X11 scaling semantics. A view move between screens must invalidate/recreate scale-dependent buffers and glyphs. Keep native text-field bounds, hit regions and accessibility rectangles in the same logical coordinate system as the scene.

`presentTopLeftBgraImage` saves the context, translates by view height, applies `(1,-1)`, draws once, and restores, matching the existing standalone correction. The painter never flips note coordinates to compensate. Test an asymmetric top-left label, four different corner colors, a bottom-right label, a click target and the native lyric field. Capture both the software buffer and the actual embedded AppKit view. Re-run FL Studio F02–F05; only an actual host capture can close F03.

Keep default window sizing separate from scale repair. The proof uses 1600×900 where the display permits; the normal default can remain 1280×800 with the responsive layout. Do not force a host resize it declines.

## 6. Rendering implementation selected for macOS

Use the proposed `Canvas2D` API boundary, but implement the first native proof with a **CoreGraphics bitmap backend**. `seam_native_ui` already links CoreGraphics on Apple platforms (`CMakeLists.txt:729`). This replaces the earlier requirement to implement an entire fixed-point path rasterizer before seeing the new design. The custom rasterizer remains a possible later portable backend, not a dependency of this macOS delivery. Windows stays TODO; preserve the existing non-Apple painter while it is deferred.

New implementation files:

```text
libs/seam-native-ui/include/seam/native_ui/paint/canvas2d.hpp
libs/seam-native-ui/include/seam/native_ui/paint/image_rgba.hpp
libs/seam-native-ui/include/seam/native_ui/paint/layer_cache.hpp
libs/seam-native-ui/src/paint/canvas2d_coregraphics.mm
libs/seam-native-ui/src/paint/image_rgba.cpp
libs/seam-native-ui/src/paint/layer_cache.cpp
libs/seam-native-ui/src/paint/alpha_compositor.cpp
```

Create an explicit sRGB, 8-bit premultiplied BGRA bitmap context over a bounded surface. Use Quartz paths for rounded rectangles, circles, cubic strokes, clipping, dashes, linear/radial gradients and state save/restore. Turn on anti-aliasing. Keep a single logical top-left transform inside the backend; final view presentation is a separate boundary. The four-corner test proves both boundaries independently.

For SCENE's ring, use explicit colored arc segments, which match the image and avoid making a conic-gradient engine a prerequisite. Render low-resolution glow masks into cached bounded images; the cache key includes shape, stroke width, clip, radius, scale, mode and color, or caches only coverage and tints at composite time. A style change cannot reuse stale colored glow.

Premultiplied source-over is `Cout = Cs + Cd*(1-As)`, `Aout = As + Ad*(1-As)`. After multiplying image opacity `o`, multiply **both** premultiplied RGB and alpha by `o`. Never reinterpret current straight-alpha `Color::bgra()` as premultiplied pixel data. Bilinear filtering operates on premultiplied colors; otherwise black/white halos appear around hair. Preserve an opaque final surface for the existing presenters.

The reference color convention is explicit sRGB. If a custom gradient path interpolates in linear light, it must convert to/from sRGB deliberately and match its approved swatches; “float math” alone is not a color-space specification. Do not alternate color spaces between background textures and painted controls.

Keep `legacy()` restricted to opaque destinations. It must assert/refuse use on alpha layers until converted. Add pixel tests for a half-transparent red over transparent black, two stacked half-transparent layers, text on transparent layers, tinted/filtered hair edges, and the final opaque composite. These catch errors that a whole-screen average can conceal.

Use transparent PNG as the authoring format. QOI may remain the first bounded runtime format from the existing plan, with dimensions, encoded-byte and decoded-byte limits checked before allocation and premultiplication after decode. The QOI/PNG choice does not change the art contract. Do not change color/alpha conventions during conversion. Track decoder source/license if reusing a reference implementation.

Cross-platform bit-identical pixels are not promised. Repeatability is checked on one pinned macOS/backend/font/asset environment; portable tests check geometry, alpha, bounds and state semantics. The old fixed-point proposal would not by itself guarantee identical fonts, curves, float gradients or blur across all CPUs either.

## 7. Musical and interaction fidelity

### 7.1 Notes and labels

Use `layoutNoteVisuals` first. Keep note x/start, width/duration and pitch row from the project. Round the resulting paint rectangle; do not lengthen a short note merely to make it a prettier capsule. Use a minimum visible hairline for subpixel notes and the existing expanded hit bounds/overlap-detail mechanism.

Run a second label-allocation pass in screen space, with a per-row occupied-rectangle index. Priority: edited note, selected notes, hovered note, remaining notes in time/id order. Intersect each candidate with its note clip, band and viewport. Never place text outside an allocated label slot.

Rules: at row height ≥36, allow a reserved lyric slot and waveform body; at 22–35, keep one text line inside the note and reduce waveform contrast behind it; at <22, use compact note fills and reveal the full lyric on selection/hover or the linked lyric inspector. A phoneme tag gets its own reserved slot only at ≥48; otherwise show it in the phoneme lane/inspector. Labels that do not fit ellipsize or hide with full text available semantically. The hit target may be larger than the paint rectangle, but ties between overlapping notes must retain deterministic priority and the overlap chooser.

For a selected tiny note, the popover/inspector supplies a usable lyric field; a microscopic inline field is not acceptable. A 24-point target floor applies to ordinary controls, not to independently enlarging every dense note until all hit regions conflict.

### 7.2 Curves and audio envelopes

Draw two separately identified pitch layers when available: score/compiled-target pitch from the project/render target, and optional measured rendered F0 from the matching publication. Authored `pitchAutomation` points are region-relative; convert them through the region's project start. Measured F0 needs its own sample origin, sample rate, confidence and voiced gaps. Only authored points/controls are editable. `buildPitchContour` in `pitch_contour.hpp:12` analyzes a source sample in cents relative to one target MIDI pitch; it is used for raw voicebank candidate inspection, not a ready project-singing curve. Do not splice it onto authoring points or label a target curve “measured audio”.

The displayed target curve samples the actual target evaluator. Adaptive flattening may smooth geometry with ≤0.25 physical pixel error, but it must not invent pitch overshoot with an unconstrained spline. Split at rests, discontinuities, unvoiced measured intervals and region boundaries; do not connect unrelated phrases simply because the concept has a continuous line. Pitch-edit handles correspond to real edit points and units.

Build `PcmEnvelopeCache` on a worker from **the selected region/track's published audio**, not the mixed master. Key it by project identity, region/track, publication identity, quality, PCM identity, sample rate, origin frame and tempo/timing mapping. A Preview and Final publication can share a requested revision yet carry different bytes. Revision alone is not sufficient.

Reuse `ProjectRenderResult::performanceTrackId`, `performanceRegionId`, `performanceAudioStartFrame` and `performanceAudioMono` in `libs/seam-rendering/include/seam/rendering/project_renderer.hpp:77`; the isolated active-region audio already exists. Bind it to `PublishedProjectAudio::{projectId,requestId,sourceIdentity,sourceProject,quality}` in `render_coordinator.hpp:69` and the rendered singer identity. It is region audio clipped into note visuals, not a claim of isolated per-note stems. Do not create another synthesis pipeline to obtain it. Verify backing-only changes, active-region/singer switches, same-revision Preview/Final replacement and stale publication.

Start with min/max pairs per 64 or 256 samples plus a pyramid for zoomed-out queries. Keep signed min/max values; do not invent a symmetric waveform if the source is asymmetric. Map every visible x interval through the published tick-to-time mapping, subtract the PCM origin, and query the corresponding sample interval. Cover note lead-in/tails deliberately and clip to the declared note/phrase presentation. When mapping or source attribution is unavailable, omit the in-note waveform and explain it in the inspector.

Edits invalidate the visible waveform immediately. During an outstanding render, show a stale state only if its provenance is retained; successful replacement clears that state. A render failure must never reuse an older waveform as “current”. Do not allocate, scan PCM, generate art or blur inside the audio callback.

### 7.3 Controls and truthful status

Keep a presentation conversion separate from persisted units:

```cpp
// Examples; source limits come from describeExpressionChannel().
displayFormant = semitones;          // -12.5 st stays -12.5 in the domain
displayBreath = normalized * 100.0;  // 0.68 -> 68.0%; drag writes display/100
displayGender = bipolar * 100.0;    // -0.38 -> -38.0%; drag writes display/100
```

Clamp in domain units through existing validation. Show units in the control/accessible value. Style chips and control availability come from the resolved singer route; missing/unsupported controls display the current refusal. A sample-bank `VoicebankCard` is not the universal identity model for procedural and neural singers.

A flat-segment expression edit must retain automation outside the intended interval, preserving boundary values/interpolation. Noncontiguous selected notes edit the union of their own intervals, not the silent gap between the earliest and latest note. At integer-tick resolution, preserve evaluator values at the immediately exterior ticks and define half-open selection intervals `[start,end)`; report any unavoidable boundary-ramp change. If an unselected note overlaps a selected interval, a region-scoped curve cannot isolate it: refuse the selected-note edit and offer an explicitly labeled time-range edit. Never silently alter that unselected note. Bind through the existing expression command/draft path and capture region ID plus document generation/revision when the gesture begins. Cancel if the document/target changes; never commit against a new selection by index. Pointer capture survives leaving the window; Escape, focus loss, hide/destroy and cancellation restore the draft. Repaint during drag, commit one undo command on release. Native keyboard/numeric editing is part of acceptance.

Preserve the existing editable **Dynamics** lane and ordered **StyleBlend** workflow. The six timbral knobs do not replace Dynamics, and choosing one style chip does not implement blend ordering/weights. Add Dynamics to the lane selector and a capability-aware StyleBlend entry point to the STYLE card. Preserve its current commands and refusal behavior through a resolved-singer view adapter over sample, procedural, recipe and neural sources.

`RenderStatusState` contains Idle, Queued, Rendering, Ready, Stale, Cancelled, Failed. A “complete” animation is triggered by a transition to Ready for the matching current publication, not by a nonexistent enum or any arbitrary 100% value. `fraction` is meaningful only with known work; unknown total renders use a clearly indeterminate indicator.

The signal rail represents the relationship of controls; it cannot imply measured per-stage DSP progress when only aggregate render status exists. The singer ring may use the existing measured character-energy snapshot with the exact singer/revision binding; label it as singer activity. The output meter requires actual output measurement, separate from render progress. Label the measured bus: the header shows the selected output pair, duplicates the single meter visibly as mono for a mono bus, and offers pair selection for multichannel routes. It must not imply that two bars represent all eight channels. Resting/error states never show fabricated activity.

### 7.4 Semantics and workspace parity

`editor_semantics.hpp:11` currently lacks slider, radio, tab and progress roles and numeric range/increment/decrement actions. Add the semantic enum values, numeric metadata, value validation and actions before calling the new knobs accessible. Update the standalone per-node mappings in `libs/seam-native-ui/src/native_window_appkit.mm:968` and value/action handlers around `:1015`, plus embedded mappings in `libs/seam-clap-editor/src/embedded_view_appkit.mm:561` and their handlers. `accessibility_appkit.mm` only sets the root view role; changing that file alone does not add native slider support. Feed native IME/text-field anchors and AX frames from the same transformed/clipped geometry. Test focus restoration after closing a drawer and after reorder, and cancel edits on capture loss or document replacement.

The EXPORT redesign initially exposes the real `ExportSettings` contract, including format, channels, master/stems and receipts. `export_service.hpp:37` currently has no range selection; omit that proposed control until a separate backend change defines timing, tails, stems and receipt validation. Preserve the existing export behavior during the visual migration.

## 8. Layer, memory and timing contracts

One 1600×900@2× BGRA surface consumes `1600*900*4*4 = 23,040,000` bytes = 21.97 MiB. Four full cached layers plus one final buffer consume 109.86 MiB. That excludes art and temporary effects. The previous ≤80 MB claim is withdrawn.

First implementation allocation plan:

| Allocation | Policy at canonical size |
|---|---|
| Opaque final surface | One full buffer, 21.97 MiB |
| Static base/chrome cache | At most one full buffer, 21.97 MiB |
| Content/lane tiles | Bounded LRU, ≤24 MiB |
| Glow/dirty-region scratch | Reused, ≤8 MiB |
| Active decoded character assets | ≤32 MiB; lazy-load current mode/state, evict unused states |
| Text/icon masks | ≤12 MiB |
| Remaining UI raster/cache headroom | To a total retained budget of 128 MiB |

The budget covers retained application-owned pixel buffers; track it separately from process RSS and transient framework copies. Do not sum this as an asserted measurement. At larger viewports, cap cache allocation and fall back to clipped tiles/repainting; never allocate four entire 4K-at-2× surfaces. Loading the entire future character inventory eagerly would violate this policy. Keep whole-package validation separate from bounded lazy decode.

Dynamic repaint restores the union of old/new dirty rectangles from the base/content cache, expanded by stroke/glow radius and intersected with clip bounds. Otherwise moving playheads and Stage fades leave trails. When animated Stage is beneath notes, invalidate affected composed tiles or composite it between base and note layers; calling it a static grid layer does not make it static.

Include theme, contrast, pixel scale, UI zoom, font-set generation, viewport, scroll/zoom, clip, project revision, audible publication, selection/hover/focus, route/capability, render state and character state in the appropriate dependency keys. At-playhead values and time readouts repaint during playback. Freeze the clock, idle seed, playhead and publication for deterministic reference captures.

On the available Apple Silicon machine, target p95 ≤16.7 ms for interactive frames at 1600×900@2× and p95 ≤8 ms for steady playback. Record cold frame, scrolling, dragging, mode switching, peak allocation and process RSS separately. The earlier 3 ms playback target is an aspiration until measured. Run a real populated viewport with 10,000 total notes, plus dense visible overlap, actual fonts, both artworks, glow and text. State visible-note count as well as total count. Do not time only an already-cached empty frame.

Time controller/read-model work, glyph/layout work, dirty-region restoration, composition, `CGImage` creation/presentation and accessibility invalidation separately, as well as the total callback. Existing `INativeWindow::requestRepaint()` has no damage rectangle and AppKit's presenter draws a full image. Start by measuring that actual path; if it misses the budget, add a bounded damage-region API and use it in both native and embedded presenters. Cache savings inside the painter alone do not establish end-to-end latency. Record OS/host frame observations separately from callback CPU time.

## 9. Character package and preference compatibility

Implement schema v4 in `libs/seam-character/include/seam/character/character.hpp` and `src/character.cpp`, then adapt `CharacterPresentation`. Preserve `displayName`, `style`, `defaultState`, all required states, `developmentOnly`, and exact `SingerResourceIdentity` kind/id/version/content digest. Enforce the v3 binding invariant in v4 too. New per-mode/layer mappings extend the schema; the old illustrative JSON is not a valid complete migration contract.

Validate package paths, encoded and decoded dimensions, total decoded budget and every declared role. Canonicalize paths inside the package and reject escape/symlink escapes according to existing resource rules. On missing art, use an honest fallback without changing singer identity or voice rendering. Support status-only v1, performance v2, exact-resource v3 and layered v4 fixtures.

Persist EMO/SCENE, contrast, UI zoom and Stage overlay as application preferences. Keep the existing project `CharacterDisplayMode {Full, Minimal, Off}` compatible; do not silently insert a new enum and alter serialized values. Full enables the presentation where space allows, Minimal suppresses Stage and uses compact identity, Off suppresses character artwork while retaining accessible singer identity/status. Stage overlay is an additional user preference when Full is allowed. Its default can remain off in compact CLAP views and on at full desktop width.

Stage alpha: 0.16 at rest and 0.06 when the pointer or visible note bounds intersect it. Draw below the grid and notes. Disable Stage for High Contrast, compact layout or explicit Off. Reduce Motion makes state changes immediate and removes looping effects. Head/body movement, blink and lip sync are later enhancements, not excuses to change the approved facial identity.

## 10. Delivery sequence with an early visual proof

The previous 21-step table was an inventory, not a reliable critical path or time estimate. Use these larger deliverable units, each with working source and evidence. Windows and GitHub CI remain deferred as requested.

| Unit | Code and assets | Exit evidence |
|---|---|---|
| A — correct native presentation | Cocoa sizing/flip, shared transform contract, logical text roles, text input and hit geometry | Software + actual AppKit corner/label test; FL F02–F05 results; no claim of redesign completion |
| B — matched native SING proof | Static assets for both modes, CoreGraphics Canvas2D, canonical layout, cards, notes, one expression lane, singer portrait/Stage, working mode switch, one undoable knob and lyric input with native semantics | The same native executable renders both modes at 1600×900@1× and @2×, using real scene data and production art; dense, ready/stale/error states, source truth, IME/AX and presenter timing included in the packet below |
| C — interactive SING | Note/lyric/pitch/overlap behavior, expression knob transactions, real PCM envelopes, capability states, native semantics and compact layout | User can write/edit a phrase; undo/cancel/refusal and dense-overlap evidence; both modes work in FL Studio |
| D — remaining workspaces and character states | VOICE, TUNE, MIX, EXPORT, re-homed overlays, layered performance art, preference/package migration | Existing behaviors preserved and each new workspace works through real commands; approved material treatment reused |
| E — final visual and host acceptance | Performance, screen moves, sizing, localization, High Contrast/Reduce Motion, host reopen, final art | Native comparison and interaction checklist closes; old macOS painter removable only after parity |

**Do B before expanding D.** B must include actual compiled native drawing, not a browser mockup or another generated image. Development data may be a deterministic fixture, but its status must be explicit and it does not count as vocal quality. If a portrait or backend misses the visual target, resolve it at B while changes are localized. Keep the existing editor runnable behind the migration switch.

For each unit, request review from the existing second-developer task, resolve actionable findings, and record evidence. No new task or agent pool is required. Do not remove the Win32/X11 fallback painter while its replacement is deferred.

## 11. How we will prove the visual match

### 11.1 Three different comparisons

1. **Approved concept → native candidate:** compare composition, silhouette, material, typography, light and reading priority. Record the allowed changes in §3.2. Raw whole-image pixel equality is meaningless here because artwork/text/data and host chrome differ.
2. **Canonical geometry → native geometry:** dump logical bounds from the *same snapshot used to paint, hit-test and expose semantics*. Compare to `ui-fidelity-contract-v1.json`; main region edges/centers must be within 2 logical points, with no unintended overlaps or clipping. Handwritten metadata disconnected from the painter cannot pass.
3. **Accepted native frame → regression frame:** after B passes, freeze actual native captures with fonts/assets/data hashes. On the same pinned environment, identical frozen software inputs should repeat. For tolerant comparisons, report error and changed-pixel area per component; start with max channel delta 2 for flat interiors and ≤0.5% pixels over delta 8 within each non-art ROI. Treat anti-aliased edges separately and inspect all outliers. Do not average a broken knob into a mostly black canvas.

No numeric image-similarity percentage is currently established. Thresholds above are initial regression targets and require calibration using a verified native baseline; they are not a claim that the current application resembles the concept by a particular percentage.

### 11.2 Required comparison packet

```text
build/evidence/ui-fidelity/<candidate>/
  manifest.json                 # source, binary, macOS, backend, scale, font and asset hashes
  emo-software.png              # exact client render, frozen fixture
  scene-software.png
  emo-appkit.png                # native presentation of the same state
  scene-appkit.png
  emo-flstudio.png              # actual embedded host; external chrome separately identified
  scene-flstudio.png
  geometry.json                 # emitted by shared layout snapshot
  semantic-bounds.json          # matching focus/AX targets
  roi-comparison.json           # header, portrait, notes, expression, lane, footer separately
  performance.json              # frame timings and memory scope
  acceptance.md                 # deviations, failures, reviewer verdict, owner result
```

At the first proof there is no native golden image. Use the concept review plus geometry/primitive checks; then establish the golden. Record binary and dirty source identity, since HEAD alone does not identify an uncommitted candidate. Preserve Retina captures at native resolution; avoid screenshot resizing that hides small text. Use a fixed sRGB export and store both raw software and OS-composited captures.

### 11.3 Acceptance checklist

- Both mode identities are immediately recognizable with the approved protagonist and distinctive wordmark/material assets.
- Same musical state, viewport and UI zoom remain geometrically unchanged when modes switch.
- No upside-down presentation; pointer, native lyric editor and accessibility focus align at 1×/2× and after moving screens.
- Essential type is readable at 100% on the canonical view and at compact size; lyric overflow and overlapping notes have deliberate behavior.
- Every note, waveform, curve, readout, chip and meter derives from the correct state; unsupported actions explain why.
- All major regions meet geometry tolerances. Hair/portrait edges have no matte box or resampling halo.
- Stage and textures never obscure note editing. Reduce Motion/High Contrast preserve operation and meaning.
- Core note/lyric/pitch/knob actions work with keyboard, undo, cancel, dense songs and unavailable voices.
- Performance/memory evidence is measured with final art and fonts; a static mockup score does not satisfy it.
- An independent reviewer checks each component, not just the overall screenshot. Visual acceptance remains open until actual native captures exist.

### 11.4 Checks executable now

Run from the repository root:

```sh
python3 scripts/verify_ui_fidelity_contract.py
python3 -B -m unittest discover -s tests/design -p 'test_ui_fidelity_contract.py'
```

This verifies reference hashes/PNG dimensions, the canonical rectangle hierarchy, required nonoverlapping sibling groups, the shared musical time axis, text floors, required 1×/2× logical-to-pixel arithmetic, and memory-budget arithmetic. It explicitly prints `native_visual_match: NOT_RUN`. It does not compare screenshots or prove host rendering. Negative regression cases check that deleting required checks cannot silently produce PASS.

### 11.5 Native capture packet (implemented 2026-09-26)

The §11.2 packet now has a runner on macOS:

```sh
python3 scripts/capture_sing_fidelity_packet.py            # -> build/evidence/ui-fidelity/<candidate>/
python3 -B -m unittest discover -s tests/design -p 'test_*.py'
```

It launches the release app with `--evidence-dir` and `--window-id-file` for the empty, ready, rendering, failed and dense-overlap states in both looks at 1600×900, plus the ready state at every contract viewport. Each capture keeps the software frame and the OS-composited window (`screencapture -l`, title bar removed, both in sRGB). Geometry and semantic bounds come from the snapshot that painted the frame and are checked against the contract (±2 pt edges), against each other, and for EMO/SCENE parity. The candidate id carries the dirty-tree hash when the source is uncommitted. FL Studio captures, the stale state (it needs an edit after a published render), VoiceOver, and the reviewer and owner verdicts stay NOT_RUN in the packet.

First results, recorded in [evidence/ui-fidelity-11938f8f](evidence/ui-fidelity-11938f8f/acceptance.md):

- **Presentation colour (fixed in 72bef8af).** Both presenters drew the sRGB-authored frame with a device RGB colour space, so a Display P3 screen showed every EMO/SCENE colour oversaturated. With the sRGB presentation space, the worst per-region share of window pixels over channel delta 8 against the software frame fell from 9.9% to 0.01%.
- **Failed-render status line (fixed in 11938f8f).** It now names the reason.
- **Geometry.** All 20 captures pass the contract and semantic checks, and EMO/SCENE geometry is identical for every state and viewport.
- **Compact rack deviation (resolved after review, see below).** Below 860 pt the rack was a 56-pt rail holding the 44-pt portrait button, where §3.4 asks for a 44-pt drawer button.

Developer 2's review of \`4623bec4\` (CHANGES_REQUESTED) kept §3.4 authoritative. It ruled that the same-run ROI is presentation-path consistency evidence only, not the accepted-native regression baseline. It raised two P2 findings, both repaired:

- **Checks fail closed (\`c7c4e700\`).** Missing or empty regions, controls and nodes now fail. So do a wrong viewport, mode, scale or presentation, a note count that differs from the fixture, and a parity partner that is missing or has a different identity.
- **Bounded captures (\`c7c4e700\`).** One monotonic deadline covers startup, the window capture and shutdown. A stuck app is killed and reaped.

The compact behaviour now follows §3.4:

- **Drawer and rail.** Below 860 pt the rack is a 44-pt drawer button; from 860 to 1100 pt it is a 56-pt rail.
- **Singer inspector.** In both, the portrait button opens an inspector over the body: voice, state and style, Change voice, and all six knobs in 96-pt cells, two rows at 720×480 and one row on shorter windows. A press outside closes it without touching the score. Escape closes it and returns focus to the button. Activate on \`shell.inspector\` opens it, and Tab walks into its knobs.
- **Semantics.** Knobs, style and Change voice are published only while on screen.
- **Stage.** The Stage is off without the full rack.

The packet adds open-inspector captures at 720×480 and 860×640. It judges a capture's state from the presented frame and accepts a logged exit state only if it is that state or a later stage of it.

The specification's successful validation is useful: an implementer now has unambiguous inputs and measurable exits. It is not a substitute for producing the working SING screen.

## 12. Independent review record

The existing second-developer task `01a0a066-1eba-71f2-8c0d-e21f9419cbcc` independently reviewed both original plans, both concepts and relevant source at the baseline commit. Its original verdict was **NEEDS CHANGES for implementation readiness**, with the visual direction feasible in principle. It found pitch-domain confusion, an existing isolated-audio source, missing Dynamics/StyleBlend parity, unsafe selected-note expression semantics, incomplete geometry/AX contracts, incomplete presenter/memory budgets, host/export scope errors, and conflicting verification promises.

Those findings are incorporated above. The follow-up found two narrow corrections: the standalone accessibility adapter path, and specification checks that could be omitted while retaining PASS. Both were corrected; the musical lane's shared time axis was also clarified and checked.

**Final independent verdict: APPROVED — corrected plan/specification only.** The reviewer independently verified the checker and regression tests, and reported no remaining plan blockers. The reviewed specification before this administrative result entry had SHA-256 `23821a1c7043b4a6bada60bda2b99ca8370ca5d887cc9a2eb097b525a7ade781`.

Recorded validation: 3 reference hashes/dimensions; 22 canonical regions; required sibling checks; shared musical time axis; 1× and 2× dimensions; planned retained raster allocation 119.945 MiB against 128 MiB; 2 unittest methods including 18 negative subcases. All passed. These are specification and guard results. Actual native rendering, measured memory/timing, host behavior, vocal quality and Beta readiness remain outside this approval; native visual match and host verification are **NOT_RUN** for the redesign.
