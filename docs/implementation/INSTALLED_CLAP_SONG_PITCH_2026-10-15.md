# Installed CLAP song: written-note pitch diagnostic

Observed on parent `c27d1d6f5b9a883b48ea8c11c170998b68ac7a04`. This unit repairs the existing
Python scorer, without changing voice DSP, native targets or acceptance criteria.
The [evidence index](INSTALLED_CLAP_SONG_PITCH_2026-10-15.evidence.json) records
local retained evidence, not authenticated execution or product qualification.

## Findings and implementation

The original scorer assumes first-region local ticks at the first tempo and an
assumed 48-kHz rate. It slices by list index rather than `sourceFrame`, skips later
regions, tolerates missing stems and cannot extract the retained stereo master
through its mono-only `extract-pitch` command. Five regression assertions fail in
the unchanged parent scorer: region placement, later regions, tempo changes,
source-frame positions and explicit sample-rate mismatch. All 12 assertions pass
after repair; the final focused suite has 29 passing methods.

`tools/singing_quality/score_rendered_notes.py` now integrates the ordered tempo
map for every region/note and adds FixedAudio host placement in seconds. Explicit
`--render-offset-ticks` shifts regions before tempo projection; the loaded host
probe adds 960 ticks to each region, so this retained song requires that value.
Native feature metadata supplies the rate; a caller's rate is checked for agreement.
Frame centres select each note's middle third. Voiced frames require confidence
at least 0.5 and finite F0 in the declared range, excluding the upper ceiling.
Each report retains source hashes, note placement, measurements and exclusions.
Reports are created exclusively, preserving existing files and source aliases.

Both existing mono-stem extraction and native stereo `analyze` output are supported.
Stem counts must match exactly; their lexicographic-to-track mapping is explicit.
A master needs one nonoverlapping vocal track and no backing audio. Missing frame
grid/extent, malformed numeric features, truncated audio and source-hash mismatch
are refused. FollowHost is refused because document tempo cannot replace its
captured host timeline. The helper's positionless fallback supports its original
pure unit tests; actual CLI protocols require source positions and window metadata.

## Actual execution

The native `seam_voicebank_cli analyze` command consumed the unchanged retained
[full-song host output](INSTALLED_CLAP_SONG_2026-10-15.md): 2,004,000 frames, two
channels, 48 kHz, 41.75 seconds. It finished in 10.569 seconds and produced 7,821
Direct autocorrelation v3 frames (2,048-frame window, 256-frame hop, CompleteWindows,
60–1,200 Hz). It uses the CLI's mono mix of that stereo audio. The current native
binary predates this invocation and is retained with unchanged analyser sources;
no native rebuild is required by this Python-only edit. Its embedded source ID is
`fe340737`, while observed checkout HEAD is `c27d1d6f`; these observations do not
authenticate historical compilation or replay the full host journey.

All 48 saved notes are measured and pass the default per-note median limit of
50 cents. Worst absolute note median is **0.680453 cents**. Across the selected
**2,530 frames**, median absolute error is **0.309123 cents**, maximum is
**2.620477 cents**, and all are within 50 cents. None of the candidate middle-third
frames are excluded as unvoiced, low-confidence or out of range. Omitting the
known 960-tick probe shift leaves only 6 of 48 notes passing and one unmeasured;
this intentional negative run fails. Modified source-hash analysis and a modified
FollowHost project are refused with exit 2. Original inputs remain untouched.

The 29 focused tests use mocked feature protocols and source bytes for placement,
refusal and report-preservation logic; those bytes are expressly not native WAV
fixtures. The separate song measurement uses actual retained native analysis.

The broader `seam_singing_quality_contract_tests` entry **fails**: 181 methods,
one failing method and 19 skips. The unchanged `test_bank_root_pitch.py` also fails
alone against the same native CLI (four methods, one failure). Its whole-file demo
bank estimate is -38.579896 cents, contradicting the old asserted >500-cent
mismatch classification. This module is identical to the parent and does not import
the scorer. Its classification remains unresolved, with no threshold widening,
asset replacement or inferred bank suitability in this unit. A missing dossier
path during evidence setup and the following missing-verifier invocation are
recorded in `setup-errors.txt`; inventory corrected the path before freezing.

## Retention and remaining work

`out/evidence/installed-clap-song-pitch-2026-10-15/manifest.json` binds **71 artifacts / eight preserved inputs**,
12722 bytes, SHA-256 `b37eea0671dfa79922af47aed095efc281fe863859bedbc990827ba407f25aee`. Parent/final source, native
binary/header/source observations, original/final tests, native analysis, reports,
negative inputs and all command receipts/stdout/stderr are retained. The read-only
verifier recomputes this fixed song's note/frame error statistics and checks input,
source, receipt and canonical status hashes; it does not rerun native code or provide
independent F0 ground truth. Replay needs the prior retained song/project paths.

```sh
python3 -B out/evidence/installed-clap-song-pitch-2026-10-15/verify.py --source-commit HEAD
```

The source refresh verifier again checks 297 distinct unchanged internet WAVs:
25 clean VocalSet recordings (265.179 seconds) and 272 female growls (338.473
seconds), plus the retained license/provenance bindings. No new audio is downloaded
or imported here. Source/performer/identity/public-license disposition, genuine
producer and distinct reviewer, phonetic coverage, failed-QC policy and listening
remain unapproved. Source references: [VocalSet](https://zenodo.org/records/1203819)
and [Karoryfer publisher](https://shop.karoryfer.com/pages/free-samples).

This is a middle-third engineering measurement of the legacy procedural fixture,
not the Beta phonetic steady-span metric, the required corpus or human Gate A/B.
Pitch accuracy alone does not establish intelligibility, identity or musical
quality; v3 boundary/alias limits still apply. The preceding ccbb39d6 peer review
remains live; subsequent song/publication/scorer units have no independent verdict.
Resolve the demo-bank classification next, then retain any peer findings before
claiming reviewed integration. Full classical/neural/resource/target/DAW/release
requirements and unresolved tolerance/semantic integration remain. Alpha is 0/20
recorded passes, Beta BLOCKED, U45 held and the full goal ACTIVE.
