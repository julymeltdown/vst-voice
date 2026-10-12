# Female VocalSet cohort preparation: code and execution evidence

Date: 2026-10-13, Asia/Seoul. Integrating base: `d4626fb3f41b84de2ee6e0f43c115b35af5534df`. This is F2 source preparation for the owner-selected internet route. It produces a concrete unassigned cohort; it does not complete producer import, source rights, Japanese mapping, a production singer or Beta.

## Concrete result

Five original f1 straight sustained-vowel takes from [VocalSet 1.1](https://zenodo.org/records/1203819), already acquired and license-bound in [the intake report](FEMALE_VOCAL_SOURCE_INTAKE_2026-10-13.md), yield **15 unassigned cuts**, **12.474921 seconds** total. Observed nearest MIDI notes are 60, 72 and 77; these are measured proposal labels, not approved pitch layers. Source techniques are not qualified product styles. Karoryfer growls, forte and pp takes remain outside this run.

The original CC BY 4.0 legal-code bytes, dataset/creator attribution and change notice accompany the cuts. Grants cover only licensor-held rights. Singer performer/personality rights are NOT_ESTABLISHED; the repository acquisition agreement and public-license/EULA admission question remain open. No permission flags or frozen kit files changed.

Local output: `out/evidence/female-vocal-sources-2026-10-13/cohort-preparation/final-cuts/`. Every row is NOT_ASSIGNED with pitch, phonetic and listening review NOT_REVIEWED. The directory has no INCOMPLETE marker. The [evidence index](FEMALE_VOCAL_COHORT_PREPARATION_2026-10-13.evidence.json) binds the exact local manifest, plan, source/analysis hashes, retained analyzer binary, raw commands, deterministic repeats and reader results. Media and executables remain ignored local evidence, not a release archive.

| Source vowel | Observed MIDI | Original frame interval, end exclusive | Measured median Hz | Cut seconds |
|---|---:|---|---:|---:|
| a | 60 | [52224, 93952) | 261.041 | 0.946213 |
| a | 72 | [250880, 262400) | 519.194 | 0.261224 |
| a | 77 | [395264, 413440) | 703.640 | 0.412154 |
| e | 60 | [42496, 109056) | 261.404 | 1.509297 |
| e | 72 | [178432, 199936) | 523.349 | 0.487619 |
| e | 77 | [315904, 369664) | 700.416 | 1.219048 |
| i | 60 | [31232, 53248) | 259.935 | 0.499229 |
| i | 72 | [165888, 195840) | 525.123 | 0.679184 |
| i | 77 | [307712, 371200) | 702.971 | 1.439637 |
| o | 60 | [64768, 113152) | 263.351 | 1.097143 |
| o | 72 | [180736, 239360) | 524.807 | 1.329342 |
| o | 77 | [368640, 385536) | 700.333 | 0.383129 |
| u | 60 | [21504, 96256) | 262.032 | 1.695057 |
| u | 72 | [161792, 173568) | 530.355 | 0.267029 |
| u | 77 | [302080, 313088) | 700.708 | 0.249615 |

## Code path and refusal boundaries

- [Preparation implementation](../../tools/voicebank_source_intake/prepare_sustain_cuts.py) separates `bound_file`, `proposal_rows` and `prepare`. Bound inputs are local relative paths, without traversal/symlinks; SHA-256 and byte caps bind the actual read snapshot. All source records validate before an exclusive output directory is created. Existing paths are refused; I/O failure leaves an explicit partial marker and no foreign path is removed.
- `proposal_rows` reads actual mono PCM16 WAV data, checks analysis source identity/format/extent and the complete ordered 2048/256 hop grid. The native producer is [analyzeCommand](../../apps/seam-voicebank-cli/main.cpp:174) using [PitchConfig defaults](../../libs/seam-voicebank/include/seam/voicebank/pitch.hpp:17). Finite voiced frames need confidence >=0.90 and <=40 cents from one observed note. Any rejected frame breaks the run.
- A run supports the union from its first window origin through its last origin +2048. The proposed PCM interval drops 2048 samples from both union edges. It must last >=0.20 seconds and contain a complete interior analysis window; the extra window bound matters at low sample rates. Only the longest qualifying run per source/observed note is retained. Medians and minimum confidence use complete windows inside the cut, not whole-file medians or the exercise score.
- WAV publication copies exact PCM into new standard RIFF containers. It drops ancillary chunks and performs no resampling, normalization, pitch shift or phoneme assignment. Source, analysis, derived-file and PCM hashes, frame bounds, sample rate, measured pitch/confidence and source labels are retained in `cuts.json`.
- [Tests](../../tests/test_voicebank_source_intake.py) verify deterministic PCM-preserving output, note separation and confidence gaps, low-rate short-run handling, stale/malformed analysis and hash refusal, traversal/symlink/duplicate-source refusal, and occupied-output preservation. [Tool instructions](../../tools/voicebank_source_intake/README.md) define the plan and commands.

## Executed verification

1. Retained the exact native analyzer executable (9,675,088 bytes) before fresh analyses of all five original takes; all commands exit 0. Its SHA and relevant producer source hashes are retained. It comes from the preceding full Release rebuild before publication of d4626fb3; no signed candidate/build identity is implied. Historical intake analyses lacked a retained binary; these new executions have one.
2. Prepared the actual cohort three times: all 15 cuts and metadata are byte-identical. Final-source run is explicitly retained. Compared each extracted interval to original PCM, verified all five originals unchanged, and observed existing-output refusal exit 2 with output hashes unchanged.
3. Actual retained native `inspect-wav` on all final 15 cuts: exit 0, mono 44.1 kHz, finite decoded samples, non-silent and zero clipped samples. This proves reader/PCM compatibility, not vocal quality, phonetic identity or seam/loop markers.
4. Seven Python unit cases PASS. CMake configuration exit 0 and selected Python CTests (new intake plus existing draft inventory) **2/2 PASS**. No C++ library changed in F2; these selected tests are Python-only and do not use stale C++ test binaries. No full CTest, sanitizer, native Studio, listening, DAW or Beta acceptance claim.

## Exact next boundary

The Japanese sustain-only starter preset remains only a potential target ([draft_inventory.cpp](../../libs/seam-voicebank-production/src/draft_inventory.cpp:455)). A distinct real phonetic reviewer must decide whether each source vowel maps to the requested Japanese phoneme, especially source u. Until then there is no Japanese assignment or schema-2 draft inventory. These short cuts omit onsets/transitions, consonants, CV/VC/VV, low pitch layers and full style/language coverage. Marker/loop quality, acoustic usefulness and pitch suitability still need actual QC/listening.

A registered accountable producer and owner rights-policy disposition precede workspace source admission. Use the existing source-registration, assessment and native selected-row import paths described in the intake report; do not fabricate identities, a singer contract or rights PASS. These proposals contain no producer registration, source permission, candidate generation, signature or release approval. Supplied recordings still do not complete AE1. The original full U/R/V, owner observation and all Beta gates remain unchanged.

Independent technical review of landed `6ddca089` is **APPROVE** for source/preparation only, with no P1/P2 issues. The reviewer independently reproduced all 15 intervals/statistics, verified 105 artifact hashes and evaluated the native pitch algorithm on 287 sampled frames against the original PCM. Lead native CLI/build/test runs were not rerun. Four nonblocking P3 follow-ups remain: remove local paths from shareable metadata, reject duplicate source bytes/JSON keys, give malformed inputs a controlled refusal, and test partial-write/format failures. No rights, phonetic, pitch, listening or bank acceptance was granted. The separately published UI unit d4626fb3 received APPROVE; its index/ledger now record that technical verdict, without human or Beta promotion.
