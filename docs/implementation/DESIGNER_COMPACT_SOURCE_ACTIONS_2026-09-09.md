# Designer source actions at the supported minimum size

## Outcome and scope

The source and generation-preparation action bars are now visible at Studio's
720x520 minimum, using the same geometry for painting, semantic nodes and pointer
dispatch. Previously compact mode assigned those bars negative coordinates,
making normal mouse access impossible despite shortcut/semantic alternatives.

Compact mode reserves 136 pixels for its footer rather than 84, displays six
parameter rows at height 520, and retains 15-pixel parameter typography. Remaining
rows are reachable through Previous/Next and existing navigation. Only the
noninteractive reference identity line remains omitted in compact mode; reference
playback actions remain visible. Extended layouts retain their previous geometry.
The generic backend's sub-400-pixel fallback is preserved; Studio does not permit
those window sizes. Generation preparation remains correctly disabled without
an eligible producer assignment; visibility does not bypass that requirement.

## Code and regression evidence

- `libs/seam-native-ui/include/seam/native_ui/voice_designer_layout.hpp`: adaptive
  action-bar reservation and compact source/prepare coordinates.
- `tests/test_voice_designer_workflow.cpp`: updated compact contract plus a sweep
  through every integer height from 520 to 1600, checking parameter/footer
  separation, ordered action rows, and source/error bounds.
- Full Release build succeeded. Designer/core CTest entries passed 2/2 in
  22.67 seconds. This was not a full release-suite run.

## Live pointer verification

Rebuilt Studio was launched with `--designer --force-synthetic-input
--window-width 720 --window-height 520`. A temporary, unsaved QA draft was used.

1. The screenshot showed six readable parameter rows and visible audio,
   preparation and source bars without overlap in this fixture.
2. Pointer Add stop opened the source dialog. Entering `k` with style `neutral`
   added the source, advancing recipe revision from 0 to 1.
3. Pointer Next navigation exposed the stop parameters and Src/CV/VC/Play controls.
4. Pointer VC produced `Vowel and stop ready`; pointer Play enabled Stop and
   reported `SELECTED VOWEL + STOP / NOT APPROVED`.
5. Pointer CV produced `Stop and vowel ready`.
6. Pointer Src produced `Plosive source ready`.

The live observations verify routing, completion and playback activation, not
subjective listening quality or phonetic acceptance. No saved recipe, producer
take or microphone recording was changed. Other dialogs, long arbitrary labels,
all resize combinations, Windows runtime and full Beta GO remain unqualified.

## Continuity

Existing dirty changes were preserved; no staging, commit or push was performed.
This implements part of U22/R3/R11, not acceptance of a whole roadmap unit.
