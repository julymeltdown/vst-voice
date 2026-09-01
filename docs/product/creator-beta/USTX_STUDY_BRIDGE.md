# Project SEAM USTX Creator-Study Bridge

**State:** Implemented locally, validation passing, not yet hash-bound or independently approved.

**Authority:** This is the throwaway low-fidelity USTX path required by the creator-scope study. It is not the production USTX adapter, does not authorize schema 8, and must not be presented as Beta interchange support.

## Study job

The bridge lets a study operator:

1. convert one trusted USTX 0.9 fixture into an ordinary SEAM schema-7 project;
2. open, inspect, save, and meaningfully edit that project in the current native editor;
3. convert the edited schema-7 project back to USTX 0.9;
4. inspect the mandatory conversion reports;
5. open the exported file in the pinned OpenUtau reference.

Every conversion prepares and synchronizes the converted file and JSON report before sequential no-clobber publication. The child-process exit status is the commit signal: study tooling must not consume either pathname until the command exits successfully. A raced destination is preserved, and a second-publication failure removes only the first file created by that invocation when its identity is unchanged. This recovery contract assumes an operator-controlled output directory without a hostile concurrent writer; the production adapter requires handle-bound publication. The report contains source and target hashes, statistics, limitations, and each known loss. No singer, renderer, phonemizer, resampler, wavtool, package, script, or other executable reference is resolved.

## Run it

Import a trusted USTX fixture:

```bash
uv run scripts/creator_ustx_study_bridge.py import-ustx \
  INPUT.ustx OUTPUT.seam \
  --report OUTPUT.import-report.json \
  --voicebank-id STUDY_BANK_ID \
  --voicebank-version STUDY_BANK_VERSION \
  --voicebank-content-hash STUDY_BANK_SHA256 \
  --character-id STUDY_CHARACTER_ID \
  --character-version STUDY_CHARACTER_VERSION
```

Export an edited schema-7 project:

```bash
uv run scripts/creator_ustx_study_bridge.py export-ustx \
  INPUT.seam OUTPUT.ustx \
  --report OUTPUT.export-report.json
```

The command exits successfully only when both the converted file and report are newly written. A successful message still names the loss count; `PASS` means the study conversion completed, not that the conversion was lossless or production-ready.

## Supported study subset

- USTX version 0.9 and SEAM project schema 7 only.
- Fixed USTX 480 PPQ and arbitrary valid SEAM PPQ with deterministic tick rounding reports.
- Tempo events and time signatures.
- Vocal tracks, voice parts, regions, note timing, MIDI tone, and visible lyrics.
- Track name, mute, solo, volume, and pan.
- Note pitch points converted between OpenUtau note-relative milliseconds and SEAM region-relative ticks through the tempo map, with note tuning and evaluated note-boundary values materialized explicitly.
- OpenUtau `snap_first` materialized into explicit pitch values during import; exported pitch uses `snap_first: false` so OpenUtau does not silently rewrite SEAM-authored values.
- Touching-note pitch conflicts use later-note boundary ownership and produce an explicit collision loss.
- OpenUtau minimum voice-part duration normalization, including the next-beat tail required by `UVoicePart.AfterLoad`.

## Deliberate limitations

The report records, rather than silently hides, unsupported behavior:

- persisted note vibrato, because schema 7 has no note-vibrato field;
- OpenUtau part curves and expression descriptors;
- exact OpenUtau pitch-shape semantics when mapped to SEAM linear or smooth interpolation;
- phoneme expressions and detailed phoneme overrides outside the schema-7 subset;
- SEAM unit-selection and seam overrides on USTX export;
- singer, voicebank, character, renderer, style, and executable-tool identity;
- wave and SEAM audio tracks;
- USTX versions other than 0.9.

The parser accepts at most 4 MiB, rejects aliases, anchors, explicit tags, multiple YAML documents, non-UTF-8 input, non-finite values, excessive depth before construction, excessive node or collection counts, and oversized or invalid Unicode scalars. Expected parser resource failures become ordinary bridge diagnostics. Input reads hold one no-follow regular-file descriptor on supported POSIX platforms. This is a trusted-fixture boundary, not the production rapidyaml design. It does not satisfy the production 64 MiB input and 256 MiB retained-memory contract, cross-platform handle implementation, callback-failure behavior, fuzz gate, hostile-fixture corpus, or handle-bound output publication.

## Automated verification

Run the focused bridge behavior:

```bash
python3 -m unittest tests.production.test_creator_ustx_study_bridge -v
```

Run the creator-product contract suite:

```bash
python3 -m unittest discover -s tests/production -v
```

Static checks:

```bash
ruff format --check tools/creator_scope scripts/creator_ustx_study_bridge.py tests/production/test_creator_ustx_study_bridge.py
ruff check tools/creator_scope scripts/creator_ustx_study_bridge.py tests/production/test_creator_ustx_study_bridge.py
uvx --with PyYAML==6.0.3 --with types-PyYAML --with jsonschema==4.26.0 \
  basedpyright --level error tools/creator_scope \
  scripts/creator_ustx_study_bridge.py \
  scripts/verify_creator_scope_ratification.py \
  tests/production/test_creator_ustx_study_bridge.py \
  tests/production/test_creator_scope_ratification.py
```

The fixture `tests/fixtures/creator-study/minimal-openutau-0.9.ustx` covers two touching Japanese notes, a tempo change, pitch shapes, `snap_first`, and note vibrato loss.

## Required binding before a creator session

The canonical prerequisite remains `NOT_RUN`. Before it can become `PASS`:

1. bind the exact committed prototype files and fixture corpus by SHA-256;
2. bind the exact installed SEAM study candidate and rights-cleared bank;
3. repeat native SEAM loading against that candidate;
4. load the exported USTX through `OpenUtau.Core.Format.Ustx.Load` at commit `8c0dc4007e6e8c8181f3a12c10205671800eeb8b` in the isolated reference profile;
5. preserve repository-relative raw evidence and hashes;
6. obtain independent product-research, QA, and engineering approval;
7. update the canonical ratification record without changing `schema8Authorization` until the complete study passes.

Local implementation or green unit tests cannot satisfy those evidence requirements by themselves.
