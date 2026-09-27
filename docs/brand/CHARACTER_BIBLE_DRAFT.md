# Character 01 Bible

Status: living document. It describes the protagonist as she ships in the EMO/SCENE design shell
at master `74a59b32` (2026-09-27), and her lasting product rules. The file keeps its `_DRAFT`
name because other documents link to it by that name (`docs/design/SEAM_VISUAL_SYSTEM_OVERHAUL_PLAN_2026-09-25.md`
and the phase `FILE_TREE.txt` listings). The draft label also still describes her legal status:
"Character 01" is an internal name, and every runtime asset is development art that is not cleared
for release (§9).

In progress on another branch while this was written (not in `74a59b32`): the project's default
display mode changes from Minimal to Full, and the SINGER menu gains a Full/Minimal/Off switch.

## 1. Product role

Character 01 is the protagonist of Project SEAM and the visual avatar of the default voicebank,
Official Voicebank 01, and of SEAM's concatenative model. She stands for cut samples, repeated
vowels, explicit seams and controlled discontinuity. She is not given a fictional band career or a
singer biography, and she is not a desktop assistant or a talking mascot. On screen she reacts to
real application state and to nothing else (§5).

## 2. Who she is

The owner's reference sheet (`docs/design/references/protagonist-reference-sheet-2026-09-25.png`,
SHA-256 `63df5a68…ad4f`) defines her. The runtime art keeps these features in every state and
both looks:

- black, messy, layered hair with one pale streak falling over her face;
- heavy eyeliner and a mole under the left eye;
- an ear cuff and a small pendant necklace;
- a dark short-sleeved zip hoodie over a mesh top, with grey-and-black striped arm warmers;
- a studded belt with a hanging chain, and dark worn jeans;
- plain, original high-top sneakers whose only mark is a stitched seam at the ankle (visible in the
  seated splash art).

She is drawn in a hand-inked, monochrome manga style with a faint crimson rim light in EMO. She
never holds a microphone or an instrument, and no art carries text, logos, brand marks, stars, band
names or watermarks.

### Personality range

```text
reserved ↔ sudden overreaction
careful organization ↔ visible imperfect repair
emotional distance ↔ intense focus
quiet exterior ↔ loss of composure
```

She is not permanently sad, hostile, fragile or elitist. She does not police other people's musical
knowledge. Her poses (§4) stay inside this range: guarded, absorbed, briefly pleased, wary, and
overwhelmed.

### Product-system motifs

- Seams correspond to real garment construction; the sneaker's stitched seam is the one mark she
  carries.
- Splice marks appear as restrained stitch breaks or fabric boundaries.
- Small unit-index graphics may refer to `CV`, `VC` and `VV` without becoming slogans.
- Asymmetry (the single pale streak) represents sample branching.
- Different dark fabric values represent different source units.

## 3. The two looks

EMO and SCENE are styling of the same person; the design mode chooses which set of art she wears
(`CharacterSurface::setOutfit` in `libs/seam-native-ui/src/design/character_surface.cpp`).

- **EMO** uses the package's shared set: the monochrome ink rendering with a crimson rim light.
- **SCENE** uses the package's `outfits.scene` set: the same six poses, framing, face and clothes,
  with hot-pink and cyan streaks through her hair beside the pale one, stacked neon bead bracelets
  (pink, cyan, lime) on her wrists, and a pink and cyan rim light.

A package without an outfit for the current mode draws its shared set. The look's own portrait,
Stage figure, wordmark and splash key art live in `assets/ui-design/<mode>/`. The EMO and SCENE
splashes show her seated with her knees drawn up, with a clear area on the left for text.

## 4. States

The shell resolves one state per frame from the read models it already holds
(`resolveCharacterState` in `character_surface.cpp`), in this order of precedence:

| Shell state | When | Package art | Pose in the art |
|---|---|---|---|
| Error | A failed render, a missing voicebank, or an identity error | `error` | Hand in her hair, head bowed, eyes closed |
| Warning | An identity warning, or audible audio that no longer matches the project | `warning` | Arms crossed, a wary side glance |
| Singing | The transport plays and the published phrase for the selected singer is performing | `focused` | Eyes closed, mouth open, a hand on her chest |
| Listening | A VOICE audition plays | `neutral` | As Idle, shown in the VOICE hero ring |
| Rendering | A render is in flight | `rendering` | Hand at her chin, thinking |
| Complete | A genuine transition to Ready, within its dwell | `complete` | A small smile, one hand raised by her face |
| Idle | Anything else | `neutral` | Standing, hands in pockets, a calm gaze |

Listening has no asset of its own, so it borrows the neutral portrait; the shell never invents a
package state. Idle, Singing and Rendering are the only states that move: Idle blinks on a seeded
4–7 second interval and breathes up to 2 points at 0.25 Hz, Singing shows the mouth the published
performance reports, and Rendering turns the ring's spinner once per 1.2 seconds. The other states
hold still and request no frames.

The blink draws lids only over the eye boxes the package declares for the drawn state, in the skin
tone sampled just under the eyes; without declared eyes no lid is drawn. The mouth is drawn only
from a sprite the package declares, at its declared placement; a status-only package has none.

## 5. Where she appears

Every surface is described with its geometry in
[NATIVE_EDITOR_DESIGN_SYSTEM.md §10](../design/NATIVE_EDITOR_DESIGN_SYSTEM.md). In short:

- **Singer ring** (SINGER card, compact inspector): her state portrait inside 64 ticks lit by her
  measured energy while she sings, by render progress while rendering, and fully when audio is
  ready; amber for Warning and red for Error.
- **VOICE hero ring:** she listens to the audition, and the ring follows its measured level.
- **Stage:** the look's full-body figure faintly behind the notes (16% at rest, 6% when notes or
  the pointer reach her), only with the full rack, never over editing and never clickable.
- **Header avatar:** a 28-point circle with a state ring where the header has room.
- **Empty project:** the mode's splash with "Double-click the grid to write the first note." in its
  clear area; on a small grid, her portrait with the same line.
- **Error toast:** a crop of her head from the error portrait, with the reason, for a failed render
  or a missing voicebank.
- **About sheet:** the mode's splash behind the product name, version and build.

She never takes part in DSP, rendering, PCM cache keys or exported audio, and her art never changes
singer identity. The Stage sits beneath the notes and curves at low opacity and is excluded from hit
testing and the accessibility tree, so it never blocks the piano roll, lanes or technical editing
areas. The singer's state is always available as text through the SINGER card and status nodes.

## 6. Display modes, Reduce Motion and High Contrast

The project's `CharacterDisplayMode` (`libs/seam-domain/include/seam/domain/project.hpp`) stays a
user choice:

- **Full** draws every surface.
- **Minimal** drops the Stage and keeps the compact identity (ring portrait, avatar, splash, toast).
- **Off** draws no character artwork in the editor. The ring ticks, the avatar's state ring, the
  toast text and the empty-project line still carry her status. The About sheet is application
  chrome and keeps its art.

The `C` key cycles Full → Minimal → Off while the score has focus. At `74a59b32` a new project
defaults to Minimal; the switch to Full and a SINGER menu control are in progress.

Reduce Motion stops the blink, the breathing and the spinner and makes the Stage fade immediate;
her state still changes. High Contrast turns the Stage off and removes glows. A static mouth under
Reduce Motion is **Not shipped**: the singing mouth still follows the performance.

## 7. Art assets and their pipeline

The character package is `assets/character-01/`. Its `manifest.json` is schema 2 (the
performance schema), version 0.3.0, `developmentOnly: true`:

| Entry | Format |
|---|---|
| `states` (neutral, focused, rendering, complete, warning, error) | Binary PPM (P6), 320×480, half-body with the head in the top two fifths, in `runtime/` |
| `mouths` (closed, narrow, nasal, open, wide, round) | 24×24 PPM sprites cut from the singing face; the flat corner color is keyed to transparent at load |
| `mouthPlacement` | The sprite's box, normalized to the portrait |
| `eyes` | Per-state eye boxes, normalized to the portrait (the error pose declares one) |
| `outfits.scene` | The SCENE set's own states, mouths and placement in `runtime/scene/`; it inherits the shared eye boxes because the poses match |

The loader (`libs/seam-character/src/character.cpp`) accepts status-only schema 1, performance
schema 2 and resource-bound schema 3, which binds the package to an exact singer resource identity.
A refused package is treated as absent with its reason kept, and the shell then draws the look's own
portrait; it never mixes package art with look art in one surface. Decoding is lazy and per state,
and changing the outfit drops the decoded art. The shell finds the package through
`SEAM_CHARACTER_ASSETS`, then `character-01` beside the design assets, then the source tree
(`locateCharacterAssets` in `sing_shell.cpp`).

The look art in `assets/ui-design/<mode>/` is PNG in sRGB, premultiplied on load: `portrait.png`
(768×768), `stage.png` (trimmed RGBA, 362×1152 EMO and 354×1152 SCENE), `wordmark.png` (reading
only "SEAM") and `splash.png` (1600×1000, opaque). Its `manifest.json` records each file's SHA-256
and the SHA-256 of the generator output it came from.

`scripts/build_character_state_art.py` builds all of this, deterministically for a given Pillow and
NumPy version:

```sh
# Store generator output as the committed masters (640×960 PNG states, 1600×1000 splashes)
python3 scripts/build_character_state_art.py import --state neutral=/path/a.png ... --splash emo=/path/e.png ...
python3 scripts/build_character_state_art.py import --outfit scene --state neutral=/path/s.png ...
# Derive the runtime PPMs, mouth sprites, mouthPlacement and the review contact sheets
python3 scripts/build_character_state_art.py build
```

Masters live in `source/states/` and `source/states/scene/`, with their hashes in
`assets/character-01/source/states/sources.json`. The build writes the contact sheets
`assets/character-01/previews/state-contact-sheet.png` and `assets/character-01/previews/scene-state-contact-sheet.png`, and
`assets/character-01/previews/eye-boxes.png` shows the eye boxes over both sets. `tests/test_character_state_art.cpp`
(in `seam_design_character_surface_tests`) and the asset gate `scripts/verify_ui_design_assets.py`
(`seam_ui_design_assets`) check the result.

**Not shipped** from the redesign plan §8: Character Package v4 with layered stage art, separate
eye open/half/closed sprites and separate seated, head-in-hand, soft-smile and listening pose
files; QOI assets; 512×512 ring portraits; and a dedicated 64×64 avatar asset. The shipped surfaces
crop the six state portraits and the look art instead.

## 8. Brand and provenance rules

- No real brand, band, retailer, messenger or product name or logo appears in any asset.
- The sneakers are original: the reference sheet's star ankle patch and brand-like toe cap and sole
  are not reproduced.
- Every runtime image is AI-generated development art made from the owner's reference sheet, with
  prompts that forbade text, logos, brand marks, stars, band names, watermarks, microphones,
  instruments and extra people, and each output was reviewed by eye. That review is not a legal
  clearance. Provenance is in `assets/character-01/PROVENANCE.md` and
  `assets/ui-design/PROVENANCE.md`.
- She is an original character. Neither look is a likeness of a real person, and she does not
  represent a voice provider (see
  `docs/brand/VOICE_PROVIDER_CHARACTER_SEPARATION_STATEMENT_DRAFT.md`).
- Visible text around her is real state or real instruction; no slogans.

## 9. Release status

The following are still not locked for commercial release:

- the final public character name, with trademark, domain and social-handle clearance
  (`docs/brand/CHARACTER_01_NAMING_CLEARANCE.md`);
- production art: a first-party redraw or model, a front/side/back turnaround, and final expressions
  and animation, with an artist agreement and character-IP approval;
- complete IP ownership and assignment review;
- commercial key art and merchandise rules
  (`docs/brand/CHARACTER_01_USAGE_AND_MERCHANDISE_POLICY_DRAFT.md`);
- the final relationship statement between Character 01 and the eventual voice provider.

Both manifests keep `developmentOnly: true`. The ui-design asset gate
(`scripts/verify_ui_design_assets.py`) refuses `developmentOnly: false`, so clearing that art is a
deliberate, recorded change.

## 10. Platforms

She is drawn only where the SING shell presents, which today is macOS (the standalone app and the
CLAP plug-in, with the same art and the same Full/Minimal/Off state). On Windows and Linux the
editor surface shows a notice that it needs the macOS renderer, so no character surface exists
there yet (README, Windows TODO item 5). A future platform editor draws the same package and must
not turn her into a desktop assistant, a talking mascot, a singer overlay or a mandatory panel.

## 11. History

- **Phase 1 to Phase 11:** a violet low-poly direction (`assets/character-01/source/canonical-lowpoly.jpeg`)
  was the canonical runtime look, with a Full/Minimal/Off dock in the classic editor and
  per-platform shells rendering the same pre-rendered package.
- **Phase 13B:** deterministic development key art, portrait, thumbnail, silhouette and palette were
  added under `assets/character-01/production-development/`. They remain blocked from
  commercial-release acceptance and are not used by the design shell.
- **2026-09-25 to 2026-09-27:** the owner's reference sheet defined the hand-inked protagonist, the
  EMO and SCENE looks were approved, and six distinct per-state portraits, the SCENE outfit set,
  per-state eye boxes, the splash key art and the About sheet replaced the low-poly figure at
  runtime. The classic editor and its character dock were retired; the design shell is the only
  surface that draws her.
