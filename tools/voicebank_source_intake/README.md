# Source-bound sustain cut proposals

`prepare_sustain_cuts.py` prepares **unassigned, unapproved** straight sustained-vowel material from original mono PCM16 WAVs and matching `seam_voicebank_cli analyze` output. It copies PCM intervals into new WAV containers without resampling, normalization or pitch changes. Ancillary RIFF chunks are removed and that change is recorded. It does not register a producer, import a workspace, assign Japanese phonemes, qualify pitch layers or approve any source rights.

```sh
python3 tools/voicebank_source_intake/prepare_sustain_cuts.py \
  --plan /absolute/path/to/cohort-input.json \
  --output /absolute/path/to/new-output-directory
```

The output parent must exist. An existing output is refused. All bound input files must be below the plan directory, without traversal or symlinks. The plan has schema version 1, a `license` record with relative `path`, lowercase `sha256`, non-empty `sourceUrl` and `attribution`, and 1–32 `sources`. Each source has a unique safe `id`, `sourceVowel` (a/e/i/o/u), `sourceTechnique: straight`, and `audio`/`analysis` records containing relative `path` and exact SHA-256. Repeated original audio hashes and duplicate JSON keys are refused. These vowels are source labels, not a language mapping.

`analysisContract` must be exactly `{"command":"seam_voicebank_cli analyze","windowFrames":2048,"hopFrames":256,"coverage":"CompleteWindows"}`. The default producer is `apps/seam-voicebank-cli/main.cpp` and `libs/seam-voicebank/include/seam/voicebank/pitch.hpp`; retain the executing binary, command receipts and original/analysis hashes outside this tool. An analysis's source path must still resolve to the bound original WAV. A relocated corpus needs fresh matching analyses, not silently rewritten analysis identity.

The tool validates byte hashes, mono PCM16 extent, matching format/source identity, a complete ordered hop grid, finite measurements and frame bounds. A proposal needs contiguous voiced frames at confidence ≥0.90, within 40 cents of one observed MIDI note. Rejected frames break the run. One full analysis window is removed from each edge of its measured support; the remaining cut must be at least 0.20 seconds. For each source and observed MIDI, only its longest qualifying interval is proposed. Short cuts and these thresholds are engineering choices, not bank QC or listening acceptance.

Output consists of original-PCM cut WAVs, `cuts.json`, the exact `intake-plan.json` and hash-bound `license-source.txt`. `cuts.json` retains source/analysis/derived/PCM hashes, frame intervals, sample rate, measured pitch/confidence, source labels, attribution and change notices; it contains no absolute plan-directory path. Resolve relative input records against the separately retained plan directory for local reproduction. Fresh analyses after relocation may change their hashes. Every assignment/review/admission remains unset or unapproved. No draft inventory is generated. Malformed inputs print a refusal to stderr and exit 2. An I/O failure can leave an exclusively owned partial directory with `INCOMPLETE`; preserve and investigate it rather than importing it.

Run `python3 -m unittest tests.test_voicebank_source_intake -v`, or configured CTest `seam_vocal_source_intake_tests`. The tests verify original PCM preservation, repeat byte determinism, separation of notes/confidence gaps, specific stale/hash/format refusals, repeated-source/JSON-key refusal, controlled large-number/deep-JSON exits, partial-write markers and occupied-output preservation. The actual female VocalSet run and its language/rights holds are recorded in [the preparation report](../../docs/implementation/FEMALE_VOCAL_COHORT_PREPARATION_2026-10-13.md).
