# Native Editor Design System

Status: describes the shipped EMO/SCENE design shell at master `74a59b32` (2026-09-27). It is
the document §16 of [the redesign plan](SEAM_UI_REDESIGN_CODE_PLAN_2026-09-25.md) asks for. Where
this text and a plan disagree, this text says what the code does and the plan says what was
intended. Anything the plans describe that the code does not do is marked **Not shipped**.

This is a description of source, not an acceptance claim. Native visual acceptance, FL Studio
host rows, VoiceOver, the owner rubric and release readiness remain as recorded in
[the fidelity review](SEAM_UI_FIDELITY_REVIEW_2026-09-25.md) and its evidence packets.

**Since `74a59b32`** (branch `codex/review-round5-fixes`): the project's default character
display mode is Full, the SINGER menu has a Full/Minimal/Off switch, and MIX and the Edit menu
have an Add Region action. The sections below describe the code at `74a59b32` with these
included.

## 1. What the shell is

The SING shell is the only editor surface in the standalone app and the CLAP plug-in. The classic
raster painter (`EditorScenePainter`) and its toggle were removed at step 21 of the plan
(`b8841295`, `dd39f71d`, `65efed34`). The shell paints around the existing editing engine:
note, lyric, vibrato, lane and ruler gestures are forwarded to `NativeEditorController` in its own
coordinates, so undo history, validation and render invalidation are the controller's own.

| Concern | Source |
|---|---|
| Tokens (color, type, shape, light), WCAG helper | `libs/seam-native-ui/include/seam/native_ui/design/design_tokens.hpp`, `libs/seam-native-ui/src/design/design_tokens.cpp` |
| Layout snapshot and breakpoints | `libs/seam-native-ui/include/seam/native_ui/design/sing_layout.hpp`, `libs/seam-native-ui/src/design/sing_layout.cpp` |
| Shell: header, SING, rack, status, EXPORT, routing, semantics | `libs/seam-native-ui/include/seam/native_ui/design/sing_shell.hpp`, `libs/seam-native-ui/src/design/sing_shell.cpp` |
| Workspace interface; TUNE; MIX | `libs/seam-native-ui/include/seam/native_ui/design/shell_workspace.hpp`, `libs/seam-native-ui/src/design/tune_workspace.cpp`, `libs/seam-native-ui/src/design/mix_workspace.cpp` |
| VOICE | `libs/seam-native-ui/include/seam/native_ui/design/voice_workspace.hpp`, `libs/seam-native-ui/src/design/voice_workspace.cpp` |
| Overlays, sheets, singer menu, About | `libs/seam-native-ui/include/seam/native_ui/design/shell_overlays.hpp`, `libs/seam-native-ui/src/design/shell_overlays.cpp` |
| Character surfaces | `libs/seam-native-ui/include/seam/native_ui/design/character_surface.hpp`, `libs/seam-native-ui/src/design/character_surface.cpp` |
| String table | `libs/seam-native-ui/include/seam/native_ui/design/shell_strings.hpp`, `shell_strings.def`, `libs/seam-native-ui/src/design/shell_strings.cpp` |
| Vector canvas (CoreGraphics/CoreText) | `libs/seam-native-ui/include/seam/native_ui/paint/canvas2d.hpp`, `libs/seam-native-ui/src/paint/canvas2d_coregraphics.mm`, `libs/seam-native-ui/src/paint/path.cpp` |
| Frame recorder, layer cache, damage | `libs/seam-native-ui/include/seam/native_ui/paint/display_list.hpp`, `libs/seam-native-ui/include/seam/native_ui/paint/layer_cache.hpp`, `libs/seam-native-ui/include/seam/native_ui/frame_damage.hpp` |
| Preferences (look, contrast, motion) | `libs/seam-native-ui/src/design/design_preferences_appkit.mm` |
| Evidence export (geometry, semantics) | `libs/seam-native-ui/include/seam/native_ui/design/shell_evidence.hpp`, `libs/seam-native-ui/src/design/shell_evidence.cpp` |

The plan's separate `kit/`, `layout/frame_tree`, `workspaces/`, `character/` and
`scene_painter_v2` directories are **Not shipped** as such. The same responsibilities live in the
files above: `SingLayout` is the geometry snapshot, the kit is a set of painter functions inside
`sing_shell.cpp` and `shell_overlays.cpp`, and the frame pipeline is `LayerCache` driven by
`SingShell::paint`.

## 2. Design tokens

A painter asks `tokensFor(mode, contrast)` for roles; it never invents a color. Four token sets
exist: {EMO, SCENE} × {Standard, High}. The look and the contrast are application preferences and
never enter a project file, a render request or a cache key, so switching them cannot change audio
(`design_tokens.hpp`). The look is stored under `mode` in the `com.project-seam.design`
defaults suite, shared by the standalone app and the plug-in, and switched with the header's
EMO | SCENE control.

### 2.1 The two looks

| Role | EMO Standard | SCENE Standard |
|---|---|---|
| canvas / surface / surfaceRaised | `#0B0A0C` / `#131115` / `#1B181D` | `#0D0716` / `#160D23` / `#1F1131` |
| textPrimary / textSecondary | `#EDE8E3` / `#A39A9F` | `#FFF4FB` / `#C9A8D8` |
| accent (selection, active tab) | `#D1143A` blood red | `#FF2E9A` hot pink |
| accentCurve / accentTime | `#F0E6E2` / `#FF2D4F` | `#1DE9FF` cyan / `#B8FF3B` lime |
| pitchCurve | `#FF2D4F` | `#1DE9FF` |
| focusRing | `#F4D9A0` | `#B8FF3B` |
| warning / error | `#E0A040` / `#FF3355` | `#FFE14D` / `#FF4D6D` |
| track colors (cycle) | red, steel, bone, rust, ash | pink, cyan, lime, violet, yellow |
| textureAlpha | 0.10 | 0.08 (plan §15 cap) |

The full role list (note fills and strokes, knob body and pointer, lane fill, meter bands, grid and
key colors) is `ColorRoles` in `design_tokens.hpp`. Shape tokens: note 6, control 6, card 12,
hero 14, hairline 1. Light tokens: glow radii 6/12/22, glow alpha 0.20 at rest and 0.55 active,
highlight 0.07.

Mode-specific material that is not a token lives in the painters (`SingShell::paintBackground`,
`paintStatus` and `paintKnobs` in `sing_shell.cpp`):

- EMO: stage light from the upper left, a red bloom behind the rack, seeded procedural ink threads,
  a torn-paper seam under the header, a line pointer on knobs, and a heartbeat render meter that
  lights in red up to the rendered fraction.
- SCENE: seeded star sparkles in the look's four accents, a checker strip along the ruler, an
  iridescent edge on the SINGER card, a dot pointer on knobs, and a render meter of 24 bead
  segments in the track colors.

### 2.2 High Contrast

High Contrast raises borders, secondary and disabled text, the knob track and the strong grid. EMO
lifts its accent to `#FF4D6A` and puts dark text on it, because no single red is both a 4.5:1
text color on ink black and a 4.5:1 background for bone-white text. SCENE lifts the violet end of
the selection gradient. Both turn texture and glow off (`textureAlpha`, `glowAlphaRest` and
`glowAlphaActive` are 0), and the frame recorder drops every glow while High Contrast paints
(`RecordingCanvas::setGlowless`).

Contrast has two sources (`design_preferences_appkit.mm`): the system's Increase Contrast, re-read
every frame while `contrastFollowsSystem` is set, or an explicit `standard`/`high` stored under
`contrast` in the defaults suite, which wins for this app alone. The shell API has `setContrast`
and `followSystemContrast`. An in-app menu or control for contrast is **Not shipped**; today the
shell follows the system unless the stored key is set.

### 2.3 Measured WCAG ratios

`tests/test_design_system.cpp` checks every foreground/background pair the painters use: nine text
roles on the four surfaces, disabled text, text on the accent and on both selected-note stops, note
and phoneme text on note fills, key labels, and the non-text pairs (focus ring, borders, the accent
as an indicator, note stroke, pitch and accent curves, knob pointer and track). High Contrast must
meet 4.5:1 for text and 3:1 for non-text on every pair. Standard must meet 4.5:1 for text except
four named exceptions, and always 3:1 for the focus ring.

The ratios below are those pairs evaluated with the test's own formula and pair list against the
token values at `74a59b32`. The weakest-text column is the line the test prints; the test binary
was not rebuilt for this document.

| Set | Weakest text pair | Weakest non-text pair | Selected pairs |
|---|---|---|---|
| EMO Standard | 3.24:1 accent on surfaceRaised (named exception) | 1.19:1 knobTrack on surfaceRaised (not held to AA in Standard) | textPrimary/canvas 16.23, textSecondary/surface 6.87, textOnAccent/accent 5.09, focusRing/surface 13.66 |
| EMO High | 4.71:1 textDisabled on surfaceRaised | 3.33:1 knobTrack on surfaceRaised | accent/surface 5.83, border/surface 5.44, noteStroke/gridWeak 6.01, focusRing/surface 15.18 |
| SCENE Standard | 4.95:1 textOnAccent on noteSelectedB | 1.16:1 knobTrack on surfaceRaised (not held to AA in Standard) | textPrimary/canvas 18.49, textSecondary/surface 9.03, accent/surface 5.47, focusRing/surface 15.60 |
| SCENE High | 5.03:1 textDisabled on surfaceRaised | 3.27:1 knobTrack on surfaceRaised | border/surface 7.76, pitchCurve/gridWeak 12.68, focusRing/surface 15.60 |

High Contrast meets WCAG AA for every listed pair in both looks. Standard is the owner-approved
palette and is not an AA claim: EMO's blood red reads 3.24–3.71:1 as small text on the four dark
surfaces (the four named exceptions), and Standard borders, strong borders, EMO's note stroke and
both knob tracks are below 3:1 as non-text. High Contrast is the accessible setting.

## 3. Typography

Font roles are `Ui`, `UiMedium`, `UiSemibold` and `UiBold` (the macOS system font at those
weights), `Mono` (the system monospaced font) and `Display` (Avenir Next Condensed DemiBold,
falling back to a heavy system weight), mapped in `canvas2d_coregraphics.mm`. The type scale in
`TypeScale` is, in logical points: body 13, label 12, lyric 13, small label 11, panel title 12
with 2.2 tracking, knob value 16, transport 22, and ruler micro 10. Nothing essential is smaller
than 11 points; 10 is reserved for ruler ticks and captions.

A label that does not fit is first tightened to 0.4 tracking, then stepped down in half points to a
10-point floor (`fitted()` in `sing_shell.cpp`), then ellipsized by the canvas at its box, never
drawn past it. The elided text stays whole on the accessibility node at that place (§8).

Bundled, pinned font files are **Not shipped**. The shell uses system faces, and the capture script
records their hashes (`/System/Library/Fonts/SFNS.ttf`, `SFNSMono.ttf`, `Avenir Next
Condensed.ttc`).

## 4. Component kit

The kit is a set of painter functions that read tokens and the layout snapshot. Each control's
painted rectangle is its hit rectangle and its accessibility bounds.

| Component | Shipped form | Where |
|---|---|---|
| Card | Glass panel (gradient fill, 1-point highlight, card radius 12, hero radius 14 for the header) with a tracked uppercase title strip | `glassPanel`, `cardHeader` in `sing_shell.cpp` |
| Arc knob | 52-point dial: 270° track, a value arc from the neutral point (bipolar channels start at the center) with a glow, radial body, EMO line pointer or SCENE dot pointer, large value, unit caption, and a marker when points are stored. A refused channel draws dimmed with an em dash and "Unavailable". Dragging is 12 points per step; a gesture is one undo step and is bound to the document, region and playhead it started on | `SingShell::paintKnobs` and the knob gesture in `sing_shell.cpp` |
| Chips | STYLE card: up to two rows of four 24-point capsules from the selected voice's real `styles`; with more than eight, the eighth reads "+N"; an empty list says the voice publishes no style | `SingShell::paintRack` |
| Tabs | Header workspace tabs (original icon glyphs, with labels when the tab strip is at least 320 points wide); lane tabs (Dynamics, Formant, Breath, Tension, Air, Gender, Growl, Phonemes), each at most 104 points | `paintHeader`, `singLaneTabWidth` |
| Segmented control | The EMO / SCENE switch, published as two radio buttons | `paintHeader` |
| Meters | Header output meter (measured bus level, mono or pair rows, a clip light); status render meter (heartbeat or beads, §2.1); MIX master meter; the singer ring (§10) | `paintHeader`, `paintStatus`, `mix_workspace.cpp` |
| Transport display | Play button, bars:beats:ticks position, tempo and meter readouts; the tempo readout opens the time map | `paintHeader` |
| Overlay control skin | Buttons, rows and fields shared by every re-homed surface | `paintOverlayControl` in `shell_overlays.cpp` |
| Signal rail | A decorative line with node dots in the gap left of the rack; no hit targets | `paintBackground` |

Tooltips are **Not shipped**. Popovers, scrolling row lists and text fields exist only as the
overlay implementations in §7, not as reusable kit components.

## 5. Layout

`solveSingLayout(width, height, inspectorOpen)` returns one `SingLayout` per frame. Paint, pointer
routing, native text input anchors and accessibility all read the same rectangles
(`SingShell::prepareFrame`). At 1600×900 logical points it reproduces
[`ui-fidelity-contract-v1.json`](ui-fidelity-contract-v1.json): header `(16,16,1568,80)`, grid
`(80,172,1040,504)`, lane `(16,696,1112,148)`, singer card `(1144,108,440,360)` with a 288-point
portrait ring, expression `(1144,480,440,248)`, style `(1144,740,440,104)` and status
`(16,856,1568,28)`. The lane's musical plot (`laneTimePlot`) shares the grid's x-axis exactly; the
24-point value gutter sits left of it.

Sizes are logical points after UI zoom; the backing scale (1×, 1.5×, 2×) is independent. The layout
floor is 480×320, and smaller sizes are clamped to it. The CLAP plug-in opens at 1280×800 and
accepts 720×480 to 4096×2160 (`libs/seam-clap-editor/src/plugin_entry.cpp`).

| Width (pt) | Rack | Header |
|---|---|---|
| ≥ 1180 | Full rack, width clamp(0.275 × W, 320, 440): 440 at 1600, 396 at 1440, 352 at 1280 | 208-point wordmark and the output meter |
| 1100–1180 | Full rack | 132-point wordmark; no output meter |
| 860–1100 | 56-point rail holding a 44-point portrait button | as above |
| 720–860 | 44-point drawer button | as above |
| 480–720 | 44-point drawer button | No wordmark and no mode switch: a "SEAM ▾" menu button lists the five workspaces and an EMO/SCENE row |

The full rack also needs a body tall enough for its three cards (180 + 248 + 104 points plus gaps);
a shorter window gets the rail or drawer instead of clipped cards. Below 720 points of height the
header is 64 points and the lane takes 34% of the body, clamped to 72–112 points.

The header is solved right to left so the transport and the meter keep their size before the tabs
shrink. Workspace tab labels show only when the tab strip is at least 320 points wide (so the
approved 1440×900 header shows icons only), and the tabs fold into a menu button whenever fewer
than 180 points remain for them.

The rail and the drawer keep every rack control reachable: the portrait button opens a singer
inspector beside the rack column holding the singer row, Change voice and all six knobs in
96-point cells (two rows of three, or one row of six when the window is too short). Knobs, style
and Change voice are published only while they are on screen.

The header avatar (28 points) takes the gap between the workspace tabs and the mode switch only
when that gap holds it with 12 points on each side, which happens from about 1594 points wide. The
canonical 1600×900 header has it; the approved 1440×900 composition and every compact header do
not.

## 6. Shell structure

### 6.1 Header

Left to right: the mode's wordmark image, five workspace tabs (SING, VOICE, TUNE, MIX, EXPORT), the
optional avatar, the EMO/SCENE switch, the transport display, the output meter, and a settings
button that opens the audio settings sheet.

### 6.2 SING

The piano-roll card holds the tool strip (track chip, grid label), the ruler (with a time-map button
at its right end when there is room), the keyboard and the grid. Notes are capsules with the
note-fill gradient, lyric and phoneme text, and the region's own rendered audio drawn as a min/max
waveform inside each note in 2-point columns, bounded by the visible span. A host that cannot supply
region audio says so, and audio that belongs to another region is not drawn. The score pitch line
is one stroke through note targets with short glides, broken at rests. Overlap bands and the "+N"
badge follow `note_visual_layout`; the badge is a hit target that opens the overlap popover.

The lane card shows the selected expression channel over the shared time axis, with its refusal (or
"no curve stored") as the plot's first line at every size, and a Review button that opens phoneme
review. The Phonemes tab hosts the phoneme, unit and seam lanes as three bands. A band can collapse
to a label strip, and the collapsed state is the project's own lane presentation.

The rack holds the SINGER card (portrait ring, voice state chip, identity footer, Change voice and a
"⋯" menu button), the EXPRESSION card (six knobs: Formant, Breath, Tension, Air, Gender, Growl) and
the STYLE card. The status bar carries the audio device or the most important diagnostic on the
left, an export-progress segment once an export has reported files, and the render meter and
state on the right. A failed render always names its reason. A diagnostics toast stacks above the
status bar with a DIAGNOSTICS opener, and the character's error toast stacks above that (§10).

### 6.3 VOICE, TUNE, MIX, EXPORT

These four cover the score with their own body while the header, rack and status remain. While one
is shown no key or accessibility action reaches the notes, and Escape returns to SING after closing
any transient menu the workspace has open. TUNE and MIX implement `ShellWorkspace`
(`shell_workspace.hpp`), VOICE extends it, and EXPORT is painted by the shell.

- **VOICE** shows the host's Voice Designer session as SOURCE (phonation and modulation knobs),
  RESONANCE (pose chips, a spectral envelope editor with a draggable handle per formant, and the
  nasal coupling knob) and NOISE (frication sources and their exact seed) modules on a signal rail,
  then OUTPUT (audition play, A/B against a pinned reference, measured level). The protagonist
  listens in a hero ring lit by the measured audition level. Every edit is a session command, and
  Undo and Redo belong to the designer while VOICE is shown. The CLAP plug-in has no designer, and
  VOICE says that voice design runs in the standalone app.
- **TUNE** draws the six expression channels over one graph on the region's bar and beat axis. The
  chosen channel is editable (add, drag or remove points; one undo step per gesture; nothing on
  Escape). The pitch view edits pitch points, a vibrato card edits the selected note with a preview
  from its stored parameters, and a strip of small knobs uses SING's commands.
- **MIX** shows an arrangement overview (a bar ruler over one lane per track; a click selects a
  region and a double-click opens it in SING), a strip per track (gain fader, pan, mute, solo,
  output route) through track-targeted commands that leave the editor's selection alone, a master
  strip with the measured output level, and a device card with only what the host reported. In the
  plug-in, MIX track and region choices move the plug-in's own render selection, and the master card
  steps the host-owned output channels (1/2/4/6/8). The header's Add region button (and Edit ▸ Add
  Region in the standalone app) adds a four-bar region after the selected vocal track's last one;
  it is disabled, with its reason, while no vocal track is selected.
- **EXPORT** is a card with what the host says an Export Set will write (rate, channels, format,
  master and stems), a run button, live progress and the last committed receipt. A host that cannot
  export from the editor (the plug-in) states why and instead offers the final bounce's timing
  choice. The plan's "complete" protagonist pose after a successful export is **Not shipped**.

## 7. Overlays and sheets

Every surface that once handed the frame to the classic painter is a panel, popover or sheet inside
the shell (`OverlayKind` in `shell_overlays.hpp`):

| Overlay | Opens from |
|---|---|
| Sample microscope | Double-clicking a unit in the Phonemes lane |
| Phoneme review | The lane's Review button or the singer menu |
| Time map | The transport's tempo readout or the ruler's button |
| Recovery and support | The controller's recovery and support view (support report preview and recovery items) |
| Overlap detail | A note's "+N" badge |
| Diagnostics | The DIAGNOSTICS opener beside the toast |
| Replacement review, and the find, cleanup, vibrato, dynamics, style and Japanese reading reviews shown in that panel | The singer menu |
| Audio settings | The header's settings button or the MIX device card |
| Voice browser | Change voice or the singer menu |
| Text field (tempo or meter, phone hint, find/replace, draft fields, renames) | The command that asked for text |
| Singer menu | The SINGER card's "⋯" button |
| About | "About Project SEAM" in the standalone app menu |

Rules that hold for every overlay:

- **One layout.** An overlay lays out its controls once (`controls()`); the shell paints, hit-tests
  and publishes exactly those rectangles. Each control keeps the controller node id it re-homes, so
  its command is the controller's own.
- **Placement.** An overlay lives in `SingLayout::overlay`: the body below the header and above the
  status bar, never over the header. Anchored popovers clamp into that slot. A window too small for
  one has no slot.
- **Modality.** A dimmed scrim covers everything under the card. A press on a control runs it, a
  press on the card is absorbed, and a press outside closes the overlay through its own close
  command. A disabled control absorbs a press and runs nothing. The covered score is neither
  published nor editable, and an open lyric field is cancelled (never committed) when a surface is
  presented over it.
- **Keyboard.** The open overlay owns the keyboard. Tab and Shift-Tab walk only its controls (the
  card itself is skipped); the singer menu also walks with Up and Down, and Enter or Space runs the
  focused item. Plain keys stop at the overlay. Command and Option chords are application commands,
  never overlay keys: the host's declared shortcuts pass through and the rest stop there. An open
  field keeps the host's text input client, so Enter and Tab commit as they always did.
- **Escape.** A surface with an inner page steps back first (the microscope's details, a review's
  draft field). Otherwise Escape closes the overlay and returns focus to the control that opened it
  when that control is still published. A plot drag inside a card is cancelled without committing.
- **First focus.** On the first frame of a newly opened overlay its first control takes the
  keyboard; a field that appears inside an open card takes it once.

The compact inspector and the workspace menu follow the same pattern: a press outside closes them
without touching the score, and Escape closes them and returns focus to their button.

## 8. Keyboard, focus and accessibility

Keyboard focus walks the tree the shell publishes, so Tab reaches exactly what is on screen. While a
shell control holds focus, plain keys belong to it: Enter and Space activate, Up and Right
increment, Down and Left decrement, Escape clears shell focus. A focused knob therefore never lets
Delete reach the selected notes. Re-homed controller controls (`toolbar.*`, `voice.identity`,
`diagnostic*`, `export.*`, `overlap-group.*`, `detail.*`) also own Enter and Space. Notes, the
timeline and vibrato handles keep the score editor's own keys. While TUNE, MIX, EXPORT, VOICE, the
inspector or the workspace menu covers the score, no plain key edits it
(`SingShell::handleShellKey`).

Accessibility tree conventions (`SingShell::rebuildSemantics`):

- **Root.** One `Window` node, id `shell`, named "Project SEAM - Sing".
- **Ids.** Controls the shell draws use `shell.` ids: `shell.workspace.<sing|voice|tune|mix|export>`,
  `shell.mode.<emo|scene>`, `shell.knob.<formant|breath|tension|air|gender|growl>`,
  `shell.lane-tab.<channel>`, `shell.lane.band.<n>`, `shell.change-voice`, `shell.singer-menu`,
  `shell.inspector`, `shell.workspace-menu`, `shell.output-meter`, `shell.render-progress`,
  `shell.status`, `shell.export.*`, and the workspace prefixes `shell.voice.`, `shell.tune.` and
  `shell.mix.`. Overlays publish `shell.overlay.<kind>.` ids plus the controller ids they re-home.
  Ids built from labels use the English text (`englishShellString`), so they do not change when a
  translation is installed. Notes keep their controller ids (`note.<id>`) so actions still reach
  the controller.
- **Roles.** `Slider` for knobs and faders, with value, range, step and a numeric value in display
  units; `RadioButton` for EMO and SCENE; `Tab` for workspaces and lane channels (selected marks
  the shown one); `ProgressIndicator` for render progress; `CheckBox` for mute and solo (value "On"
  or "Off"); `TextField` for fields; `Status` for read-only readouts; `Button` and `Panel`
  otherwise (`libs/seam-native-ui/include/seam/native_ui/editor_semantics.hpp`). A refused knob
  publishes its refusal as its value.
- **Bounds.** Node bounds are the painted rectangles. Notes use the geometry that was painted,
  including overlap bands. A note outside the grid is clamped to it and described "Outside the
  visible grid; scroll to show it", and a note hidden in a dense group says the group lists it.
  Notes stay virtualized in the controller's tree.
- **Elision.** Wherever a painted label is ellipsized, its full text is the node's value or
  description at that place. The layout property tests enforce this (§14).
- **Decoration.** The Stage figure, textures, the signal rail and the header avatar are not in the
  tree. The singer's state is published through the SINGER card and the status nodes.
- **Host boundary.** A host may send an action or a value to an editor element only while the shell
  publishes it (`dispatchController`, `setControllerValue`); a retained host element for the
  covered score is refused.

The AppKit bridge maps these roles in `libs/seam-native-ui/src/accessibility_appkit.mm`. VoiceOver
verification is **Not run**.

## 9. Strings and localization

All user-visible shell text is in one table: `shell_strings.def` holds 581 entries, each
`SEAM_SHELL_STRING(Key, "English")`. Painters and semantics call `tr(Str::Key)` when they draw or
publish, so installing a table (`installShellStrings`, `ScopedShellStrings`) takes effect on the
next frame. A lookup never fails: an entry a table does not translate reads as English. Text that
comes from the project, the voicebank or the host (track names, lyrics, device names) is data and is
never looked up. `ShellStringTable::pseudoLocalized(0.4)` accents, brackets and pads every entry to
at least 140% of its English length for layout testing.

Workflow: write English literals in the six design sources (`sing_shell.cpp`, `shell_overlays.cpp`,
`tune_workspace.cpp`, `mix_workspace.cpp`, `voice_workspace.cpp`, `character_surface.cpp`), then
run

```sh
python3 scripts/l10n/externalize_shell_strings.py          # moves new literals into the table
python3 scripts/l10n/externalize_shell_strings.py --check  # exits 1 if a literal is left behind
```

The script reuses keys for existing text, creates stable keys for new text, and turns literals in
`constexpr` or `static const` declarations into bare `Str::Key` so the compiler points at the uses
that need `tr()`.

**Not shipped:** any non-English table, a translation file format or loader, and a language choice
in the app. The table is English by default and translatable by key only from code.

## 10. Character surfaces

The protagonist appears only through read models the shell already holds; nothing is inferred from a
timer or a guess. The [character bible](../brand/CHARACTER_BIBLE_DRAFT.md) describes her and her art.
The surfaces, from `character_surface.hpp`:

| Surface | Behavior |
|---|---|
| Singer ring | A circular portrait inside exactly 64 ticks. EMO draws thin accent ticks; SCENE draws segments cycling the track colors. The lit count is the measured singer energy while performing, the render fraction while rendering, full when ready with audible audio, and the audition level in the VOICE hero. Tinted amber for Warning and red for Error |
| Stage | The look's full-body figure behind notes and curves in the grid layer, bottom-right, 78% of the roll's height, 24 points from the edges. Opacity 0.16 at rest and 0.06 when the pointer or a visible note intersects it, with a 180 ms fade. Off without the full rack, when the grid is 520 points wide or less, with an expanded technical lane, in High Contrast, when the display mode is not Full, and while the empty-project splash shows. Never hit-testable and not in the tree |
| Header avatar | A 28-point circle with a state ring, where the header has room (§5). It blinks and breathes |
| Empty project | For a region with no notes: the mode's 1600×1000 splash, contained in the grid when it fits at least 560×350, with "Double-click the grid to write the first note." in its left clear area over a scrim (an opaque plate in High Contrast). A smaller grid gets the state portrait at 42% opacity with the same line. Never a hit target, so the double-click still writes the note |
| Error toast | Above the status bar for a failed render or a missing voicebank only: a 40-point head crop, a title and the reason. It stacks above the diagnostics toast, never covers the lane tabs or the SINGER card, and is left out in compact windows |
| About sheet | The current mode's splash with the product name, version and build over its scrimmed left area. Close, Escape or a press outside closes it |

States (`resolveCharacterState`), in precedence order: Error (failed render, missing voicebank or
identity error), Warning (identity warning or stale audio), Singing (playing and performing),
Listening (an audition plays), Rendering, Complete (a genuine transition to Ready, within its dwell),
Idle. Idle blinks on a seeded 4–7 s interval for 0.12 s and breathes ±2 points at 0.25 Hz; Rendering
turns a spinner once per 1.2 s. The blink draws lids only over the eye boxes the package declares
for the drawn state, in the skin tone sampled under the eyes. While singing, the mouth is the
package's sprite for the shape the published performance reports.

Reduce Motion (the stored `reduceMotion` key, otherwise the system setting) stops the blink, the
breathing and the spinner and makes the Stage fade immediate; states still change. The singing
mouth is not gated by Reduce Motion, and the plan's static-mouth rule is **Not shipped**.

The character display mode is the project's `CharacterDisplayMode`
(`libs/seam-domain/include/seam/domain/project.hpp`). Full draws everything; Minimal drops the
Stage; Off draws no character artwork anywhere in the editor, while the ring ticks, the avatar's
state ring, the toast text and the empty-project line remain. The About sheet is application chrome
and keeps its art. The `C` key cycles the mode while the score has focus, and the SINGER menu's
last row is a Full / Minimal / Off switch. A new project defaults to Full.

Motion from plan §9 beyond the above (tab cross-fade, note-add scale, render-complete sweep, mode
cross-fade, toast in and out) is **Not shipped**.

## 11. Frame pipeline and damage

`SingShell::paint` records a frame into four layers (`libs/seam-native-ui/include/seam/native_ui/paint/display_list.hpp`) and composes it with
`paint::LayerCache`:

| Layer | Contents | Rasterized again when |
|---|---|---|
| L0 background | Canvas fill, textures, glass panels, the grid backdrop | Its key changes: size, scale, look, contrast (the token table), panel layout, artwork generation |
| L1 grid | Ruler, keys, grid lines, Stage | Its recorded drawing hashes differently (scroll, zoom, Stage opacity) |
| L2 content | Notes, waveforms, curves, lanes, card contents | Its recorded drawing hashes differently (edits, selection, publication) |
| L3 dynamic | Playhead, transport position, output meter, ring, avatar, hover, focus, gestures, menus, drawer, overlays | Every frame |

The cache keeps three cumulative snapshots (background; plus grid; plus content). A changed layer
rebuilds its snapshot and every one above it, and the dynamic layer is drawn over the content
snapshot into the target, so the target's bytes always equal a composition from nothing (tested).
Character art that needs the raster front is recorded as a deferred drawing with a content hash.

Damage (`FrameDamage`) compares the dynamic layer's named items with the previous frame's: an item
that changed, appeared or disappeared damages its old and new bounds. Rectangles merge on contact
and collapse to one bounding rectangle beyond eight. A change below the dynamic layer, a resize or
a first frame damages everything. The AppKit window and the CLAP view keep the painted surface,
repaint only the damaged rectangles when nothing below L3 changed, and invalidate the view per
damaged rectangle (`7982609d`). Win32 and X11 presenters are unchanged.

Budgets and measurements: `benchmarks/phase5_benchmark.cpp` paints the shell over a 10,000-note
project at 1440×900 on a 2× surface in both looks with glow on, 120 samples per case, and exits
non-zero above any §10 budget. The latest measurement of the pipeline as merged was reported on
`codex/unit-e-frame-performance` at its merge (`9ddd3538`): Apple M3 Max, load average 12–19, the
shell's prepare plus paint time.

| Case | §10 budget | EMO p95 | SCENE p95 | Layers drawn | §10 result |
|---|---|---|---|---|---|
| Cold full frame | 14 ms | 155 ms | 91 ms | L0–L3 | Not met |
| Scroll or zoom | 8 ms | 17.6–18.7 ms | 16.9 ms | L1–L3 | Not met |
| Playback | 3 ms | 4.3–4.4 ms | 4.2 ms | L3 only | Not met (p50 3.9–4.0 ms) |
| 10,000 notes, glow on | 8 ms | 24 ms | 23–27 ms | L2–L3 | Not met |
| Layer cache memory | 80 MB | 62.2 MB | 62.2 MB | — | Met |

Against the fidelity review's revised targets (p95 ≤ 16.7 ms for interactive frames and ≤ 8 ms for
steady playback), playback is met and the other timed cases are not; scroll and zoom miss by about
0.2–2 ms. Outside the shell, the host's scene-state derivation costs about 78 ms p95 per frame at
10,000 notes and is reported separately (`hostStateP95Ms`). One run during a load spike gave an EMO
cold p95 of 370 ms. Performance work after that merge (glow and ring sprite caching, a per-revision
pronunciation cache) exists on another branch and is not part of `74a59b32`.

These are shell paint numbers. End-to-end presentation, host display timing, FL Studio and process
memory are separate and **Not run** for the layered pipeline; the capture packets in
`docs/design/evidence/` predate it.

## 12. Brand rules

No real brand, band, retailer, messenger or product name or logo may appear in any asset, string or
manifest (`assets/ui-design/PROVENANCE.md`, `assets/character-01/PROVENANCE.md`). Every image
prompt forbade text, logos, brand marks, stars, band names and watermarks, and every output was
reviewed by eye. The sneakers are plain original high-tops whose only mark is a stitched seam at the
ankle; the reference sheet's star patch and brand-like toe cap and sole are not reproduced. The only
lettering in the art is "SEAM" in the wordmarks.

Visible text is real state, real labels or real instructions: no invented slogans, singer metadata
or style names from the concepts. Style chips come only from the selected voice's published styles.
The asset gate (`scripts/verify_ui_design_assets.py`) rejects undeclared metadata fields,
unmanifested files, path escapes and `developmentOnly: false`, but it cannot see logos. The
plan's `scripts/check_brand_terms.py` is **Not shipped**; brand review is manual.

## 13. Non-Apple platforms

The shell draws only with the macOS CoreGraphics/CoreText backend. Where that backend is absent
(`paint::vectorBackendAvailable()` returns false; `canvas2d_unavailable.cpp`), the standalone window
and the CLAP view draw `paintEditorUnavailable` (`libs/seam-native-ui/src/editor_scene.cpp`): "The
SEAM editor is not available on this platform" and "It needs the macOS vector renderer; Windows and
Linux editors are a TODO." Pointer input does nothing and keys do not edit the score. The Win32 and
X11 windows and accessibility bridges still compile. A Windows or Linux editor surface is README
Windows TODO item 5 and is **Not shipped**.

## 14. How to verify

Test targets (`CMakeLists.txt`):

| Target | Covers |
|---|---|
| `seam_design_system_tests` | Token sets, WCAG pairs, the layout snapshot at each breakpoint, the compact inspector |
| `seam_design_layout_property_tests_{workspaces,overlay,sheets,contrast,string,pseudo}` | Every workspace, overlay and sheet at 12 sizes from 720×480 to 3840×2160 and at 1×, 1.5× and 2×: no overlapping interactive widgets, 24-point minimum targets, text whole or elided with its full text on a node; the string table and 40% pseudo-localization. Run serially |
| `seam_design_shell_input_tests` | Pointer, keyboard, overlays, focus, Escape, refusals, 480×320 frames |
| `seam_design_frame_pipeline_tests` | Recorder, layer cache and damage; cached and partial frames equal a full composition, including High Contrast |
| `seam_design_character_surface_tests` | Character states, ring, Stage, splash, toast, animator, blink lids, per-state art |
| `seam_design_tune_workspace_tests`, `seam_design_mix_workspace_tests`, `seam_design_voice_workspace_tests` | The workspaces |
| `seam_clap_design_shell_tests` | The shell inside the CLAP editor runtime |
| `seam_ui_design_assets`, `seam_ui_performance_analysis` | The asset gate; packet timing analysis |
| `seam_phase5_benchmark` (an executable, not a ctest) | The §11 budgets; `SEAM_BENCHMARK_DESIGN_ONLY=1`, `SEAM_BENCHMARK_CASE=<name>`, `SEAM_BENCHMARK_SAMPLES=<n>` |

Specification and capture scripts:

```sh
python3 scripts/verify_ui_fidelity_contract.py      # contract checks; prints native_visual_match: NOT_RUN
python3 scripts/capture_sing_fidelity_packet.py     # writes build/evidence/ui-fidelity/<candidate>/
python3 scripts/analyze_sing_ui_performance.py build/evidence/ui-fidelity/<candidate>
python3 -B -m unittest discover -s tests/design -p 'test_*.py'
```

The capture script launches the release app with `--evidence-dir` for the empty, ready, rendering,
failed and dense-overlap states in both looks, every contract viewport, the open inspector and the
VOICE, TUNE and MIX workspaces. It keeps the software frame and the OS-composited window, checks the
geometry and semantics exported from the painting snapshot (`shell_evidence.hpp`) against the
contract (±2 points) and for EMO/SCENE parity, and leaves the FL Studio, VoiceOver, reviewer and
owner verdicts as NOT_RUN. Useful options are `--canonical-only`, `--states`, `--no-appkit` and
`--no-build`. For manual captures, `SEAM_UI_DESIGN=emo|scene`, `SEAM_UI_WORKSPACE` and
`SEAM_UI_INSPECTOR` select the look, the workspace and the open inspector, and `SEAM_UI_ASSETS` and
`SEAM_CHARACTER_ASSETS` override the asset roots.

## 15. Not shipped, in one place

- Character Package v4 (layered stage, separate eye and pose assets, QOI) and the plan's 512×512
  ring portraits. The package is schema 2 with PPM state art (see the bible).
- Plan §9 motion tweens, a static singing mouth under Reduce Motion, and the export "complete" pose.
- In-app controls for contrast and Reduce Motion, and the Stage as a separate preference.
- Tooltips and reusable popover, scroll-view and text-field components.
- Bundled fonts; non-English string tables and a language choice.
- `scripts/check_brand_terms.py`.
- The §10 timing budgets (the cache memory budget is met).
- Windows and Linux editor surfaces.
- Native visual acceptance, FL Studio F02–F05, VoiceOver and the owner rubric (NOT_RUN).
