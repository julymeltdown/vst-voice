# Original singer pilot 01

Development-only inventory profile. This is not a qualified singer, range,
recipe, recording, model or release resource. No audio or approval is implied.

## Reproducible listening experiment

```sh
cmake --build build/release --target seam_singer_pilot -j 6
build/release/seam_singer_pilot NEW_OUTPUT_DIRECTORY
```

The executable renders Japanese `あ い う え お さ` across MIDI 60–72 using
the normal production export pipeline. It retains three scores and editable
recipes (baseline, 15% higher formants, breathier phonation), master WAVs,
unapproved baked candidates and a hash/peak/RMS report. It requires a new output
directory and preserves partial output on failure. These are listening probes,
not a complete corpus or evidence of intelligibility, female identity or quality.

The first complete retained local run is
`build/release/seam-pilot-listening-02/`. Run 01 is retained as a failed harness
attempt: metadata was incorrectly treated as WAV during measurement, then fixed.
The registered `seam_singer_pilot_cli` test repeats the actual render, checks
identical WAV hashes and nonzero finite levels, and checks no-overwrite behavior.

For the expanded articulation probe:

```sh
build/release/seam_singer_pilot NEW_OUTPUT_DIRECTORY articulation
```

This renders `ま み む め も な に ぬ ね の ぱ た か さ` with explicit
nasal resonance/antiresonance models for `m/n`, independent burst spectra for
released `p/t/k`, and `s` frication. The recipe uses existing source semantics;
the parameter choices are experimental, not phonetic qualification. All three
variants retain 28 ordered owned gestures across 14 notes, plus score-bound
steady-pitch diagnostics. Local output: `build/release/seam-pilot-articulation-01/`.
The CLI regression checks the exact phone sequence, four gesture classes,
contiguous planned boundaries, unapproved status, audio hashes and repeatability.
Markers prove planned timing only, not perceptual onset accuracy.

Still absent from this probe: voiced stops, affricates, liquids/glides,
standalone nasal, pre-onset context, melisma, short-note failure cases and
independent intelligibility judgments. Do not use these fourteen notes to claim
complete Japanese coverage or generate a qualified full bank.

Generate the proposed Japanese coverage, across three planned pitch layers:

```sh
python3 tools/voicebank-script-generator/main.py --draft --profile assets/pilots/seam-pilot-01/profile.json
```

The explicit draft path emits schema 2, style-owned assignments and a
`NOT_ASSESSED` range assessment. It does not inherit the legacy profile's PASS.
`--json-output`, `--csv-output` and `--production-assignments-output` select output
files; choose new destinations to preserve prior artifacts. The assignment
document declares its requirement for the schema-4 producer writer.
Legacy inventory readers reject this new inventory rather than dropping styles.

After saving the inventory to a new file, prepare a captured definition with:

```sh
python3 -m tools.external_beta.voicebank_production prepare-draft --inventory INVENTORY_JSON --project-id seam-pilot-01 --operator-id producer --output NEW_DEFINITION_JSON
```

Use the returned SHA256 with the canonical C++ writer:

```sh
build/release/seam_voicebank_cli init-production NEW_WORKSPACE NEW_DEFINITION_JSON DEFINITION_SHA256 producer UTC_TIMESTAMP
python3 -m tools.external_beta.voicebank_production validate-workspace --draft --workspace NEW_WORKSPACE --inventory INVENTORY_JSON
```

The uppercase values are paths/hash/UTC timestamp to supply, not literal inputs.
This creates missing, unreviewed assignments; it does not register source rights
or grant permission to generate from an unapproved source. Definitions and
workspace creation use new destinations so existing evidence is preserved.

Next implementation work completes legacy migration and the generation planner,
then measures articulation and the actual voice before generating a full bank.
