# Project SEAM Design System

## 1. Atmosphere & Identity

Project SEAM is a dark native audio workstation: compact, precise, and calm under technical load. Its signature is a plum-and-teal signal language over layered charcoal surfaces. Plum marks selection and editable vocal data; teal marks time, pitch, and live system truth. The UI favors persistent context over decorative framing.

The current system is extracted from `EditorSceneTheme`, `VoicebankStudioTheme`, `EditorSceneLayout`, and the shared `RasterCanvas`. Design variance is 3, motion intensity is 2, and visual density is 8.

## 2. Color

| Role | C++ token | RGBA | Usage |
|---|---|---:|---|
| Surface / primary | `background` | 16, 15, 19, 255 | Editor canvas |
| Surface / studio | `VoicebankStudioTheme::background` | 15, 14, 18, 255 | Voicebank Studio canvas |
| Surface / panel | `panel` | 24, 22, 28, 255 | Toolbars and inspectors |
| Surface / alternate | `panelAlternate` | 20, 20, 24, 255 | Lists and secondary regions |
| Text / primary | `primaryText` | 241, 235, 242, 255 | Titles and selected values |
| Text / secondary | `secondaryText` | 170, 159, 171, 255 | Labels and metadata |
| Accent / vocal | `accent` | 170, 77, 116, 255 | Editable vocal state and focus |
| Accent / temporal | `playhead`, `pitch` | 98, 192, 190, 255 | Pitch, playhead, and live host state |
| Focus | `focusRing` | 255, 221, 101, 255 | Keyboard focus only |
| Status / critical | `diagnosticCritical` | 130, 42, 58, 255 | Blocking diagnostics |
| Status / warning | `diagnosticWarning` | 104, 76, 38, 255 | Recoverable warnings |
| Status / ready | `runtimeOverlayReady` | 153, 178, 169, 255 | Verified readiness |

Rules:

- Extend the existing theme structs before adding a raw color to a reusable surface.
- Plum and teal carry meaning. Do not add decorative status colors.
- Focus yellow is reserved for keyboard focus and must remain visible against every panel.
- Production readiness uses explicit text as well as color.

## 3. Typography

The native renderer uses the trusted system text engine and one sans-serif family. A bitmap fallback exists only when the system engine is unavailable.

| Level | Logical size | Usage |
|---|---:|---|
| Surface title | 14 | Application and major workspace title |
| Empty-state title | 16 | Singular high-priority empty state |
| Lane label | 11 | Persistent technical lane identity |
| Control and row label | 8-10 | Interactive and dense working text |
| Metadata | 7 | IDs, status, keyboard hints |
| Micro label | 6 | Marker labels in bounded plots only |

Rules:

- User-facing body or action text should not be introduced below logical size 8.
- Size 6-7 is restricted to bounded technical metadata already supported by tests.
- Text must use bounded `Rect` drawing when the owning region can narrow.
- Long identifiers must truncate or move to a detail view; they must not overlap adjacent columns.

## 4. Spacing & Layout

The base unit is 4 logical pixels. Existing geometry uses these multiples, with 2-pixel optical corrections inside plots.

| Token | Value | Usage |
|---|---:|---|
| `space-1` | 4 | Plot and label inset |
| `space-2` | 8 | Compact row and lane inset |
| `space-3` | 12 | Panel content inset |
| `space-4` | 16 | Separation between major plot regions |
| `space-6` | 24 | Dense section rhythm |
| `space-8` | 32 | Major group separation |

Layout contracts:

- Native minimum surface is 720 by 520 logical pixels.
- The main editor toolbar is 64 high; Voicebank Studio currently uses a 72-high identity and status band.
- Voicebank Studio uses a 252-wide inventory rail, a fluid center canvas, and a 238-wide inspector.
- The center canvas owns contraction. Rails keep labels bounded and never overlap the waveform or inspector.
- Production status belongs in the inspector or top status band, not as an overlay on the waveform.

## 5. Components

### Native Status Band

- Structure: title and identity at left, operational state and backend at right.
- Variants: idle, dirty, recording, recoverable warning, blocking error.
- States: every state includes text; recording and errors may also change semantic color.
- Accessibility: exposed through the semantic tree where interactive; status text must remain readable without color.
- Motion: none. State replacement is immediate.
- Layout: horizontal cluster with fixed-height ownership and bounded text rectangles.

### Inventory Rail

- Structure: section label, visible window of rows, one selected row.
- Variants: populated and empty.
- States: default, selected, keyboard focused, missing, retake, review, approved.
- Accessibility: row identity, pitch, and queue state must be readable and addressable by keyboard.
- Motion: none.
- Layout: vertical list with stable row height and virtualized visible range.

### Technical Canvas

- Structure: waveform, spectrogram, acoustic markers, pitch marks.
- Variants: ready, unavailable audio, staged output preview.
- States: default, selected marker, locked mark, invalid edit.
- Accessibility: an equivalent textual inspector exposes values and actions.
- Motion: pointer-driven edits only; no automatic animation.
- Layout: fluid center region bounded by the inventory rail and inspector.

### Inspector Group

- Structure: title followed by grouped identity, workflow status, and available actions.
- Variants: unit, take, production project, and recovery candidate.
- States: ready, missing, review, blocking, empty.
- Accessibility: short plain-language labels, ordered keyboard actions, and visible focus.
- Motion: none.
- Layout: vertical stack using 12-pixel inset and 16-24-pixel group rhythm.

### Queue Summary

- Structure: six explicit counts for missing, rejected, retake, marker review, pitch review, and approved.
- Variants: no project, active project, complete synthetic fixture.
- States: empty, actionable, blocked, approved.
- Accessibility: status names are written in full and do not rely on colored dots.
- Motion: none.
- Layout: two compact columns when width permits, otherwise a vertical stack.

## 6. Motion & Interaction

- Native editing feedback is immediate. Automatic decorative motion is not part of the workstation language.
- Existing geometry transitions may use the shared 150-millisecond lane transition when they communicate a layout state change.
- Reduced-motion mode commits the final geometry immediately.
- Keyboard focus, pointer selection, recording state, and save state must remain distinct.
- Hover alone never carries required information.

## 7. Depth & Surface

The strategy is tonal shift with sparse one-pixel structural dividers. Panels are separated by neighboring charcoal values. Shadows and glass effects are not part of the native raster language. Selected rows use a plum tonal fill; critical overlays may add a one-pixel semantic border.

## 8. Accessibility Constraints & Accepted Debt

Constraints:

- Target WCAG 2.2 AA contrast for working text and controls.
- Every action must have keyboard and semantic-tree parity.
- Status, queue, rights, and recovery state must be expressed in text, not color alone.
- Text, Korean/Japanese labels, and long unbroken identifiers must remain inside their owning rectangles at 720 by 520 and at 2x scale.
- Recording, destructive replacement, and export actions require explicit state feedback.
- Reduced-motion behavior must preserve the final state and focus location.

Accepted debt: none for U56.

Existing inconsistencies to consolidate only through a separately approved visual change:

- Voicebank Studio spectrogram colors are currently computed inline rather than owned by `VoicebankStudioTheme`.
- Voicebank Studio has several point-based text draws where bounded rectangles would improve long-identifier behavior.
