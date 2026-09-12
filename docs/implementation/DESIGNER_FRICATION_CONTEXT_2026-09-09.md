# Frication context previews in Voice Designer

## Delivered behavior

Frication sources now support Src, CV and VC previews, alongside existing stop
previews. The CV/VC path uses the selected vowel pose/style and MIDI pitch through
the existing score-performance compiler and ArticulatedStream, not concatenated
standalone waveforms. Each preview is one second at 48 kHz, with a fixed 150 ms
unvoiced frication at the beginning or end. This is a bounded design-time audition
policy, not a learned or language-specific duration or a production timing change.

The common consonant/vowel render helper preserves existing stop closure/burst
timing. Public typed frication modes reject invalid values; isolated source mode
continues to use the original source renderer. Async preview adoption remains
bound to draft epoch/revision, pose and pitch. Switching modes drops the old noise
buffer; draft edits cancel/invalidate it. Mode-specific ready/playback labels
retain the NOT APPROVED distinction. No recipe schema or synthesis algorithm
revision changed because this exposes existing articulation functionality.

Native source buttons use the shared semantic/pointer route at minimum window
size. Primary shortcut + Space selects isolated source; Shift selects CV and Alt
selects VC, matching the stop workflow. The physical shortcut path was not
separately live-qualified in this increment.

## Evidence

- Full Release build succeeded.
- Designer and core CTest entries passed 2/2 in 22.71 seconds.
- Added tests for deterministic CV, distinct CV/VC and pitch-dependent PCM,
  finite bounded output, non-silent coda noise, invalid indices/pitch/mode,
  mismatched style, cancellation, async mode identity, edit-time invalidation,
  undo resource restoration and preserved isolated-source PCM.
- Existing stop preview tests passed after sharing the context renderer.
- Live rebuilt Studio at 720x520: created temporary draft, added `s / neutral`
  via Add noise, navigated to source parameters, clicked VC and observed
  `SELECTED VOWEL + FRICATION ready`. Clicking Play enabled Stop and reported
  `SELECTED VOWEL + FRICATION / NOT APPROVED`. Clicking CV then reported
  `FRICATION + SELECTED VOWEL ready`.

No saved bank/recipe, producer record or microphone capture was modified. The
prior temporary QA draft was discarded through its confirmation dialog, and its
process exited with input_backend=unopened and recorded_frames=0.

## Remaining scope

This is progress on U20/U22, not acceptance of either whole unit or Beta GO.
Listener-reviewed pronunciation, voiced fricatives, richer context transitions,
complete inventories and qualified singer assets remain separate requirements.
The live check proves routing and playback activation, not subjective audio
quality, cross-platform behavior or every supported source configuration.

All pre-existing dirty work was preserved. No staging, commit or push occurred.

## Articulation diagnostic follow-up (2026-09-10)

Source inspection confirms the current gesture set is oral vowels, configured
nasals, unvoiced frication and explicitly released p/t/k stops. Voiced fricatives
remain unsupported: a frication binding named `z` cannot satisfy a voiced `z`
token. Extending this requires a real voiced/noise articulation path, not changing
the token's voicing or accepting a label-only source.

The compiler now reports the affected phone and note, distinguishing missing
selected-style source bindings, unsupported voiced articulation and unsupported
noise roles. Regression assertions exercise missing `s` and voiced `z` with a
same-named noise binding. The rejection policy and rendered PCM are unchanged;
this is diagnostic work, not a newly supported consonant or U20 acceptance.
Release build and voice-design/core suites passed 2/2 in 23.31 seconds.
