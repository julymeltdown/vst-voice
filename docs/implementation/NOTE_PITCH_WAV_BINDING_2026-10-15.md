# Native pitch reports bind their geometry to the held WAV

Date: 2026-10-15, Asia/Seoul. Base: `06803bb56de422626b731b921113d9183ba4b3e6`.
Local verification passes; independent review of this new unit is **PENDING**.

The peer's exact **APPROVE c111cbac5ba183dee822441d637a516836a52a2f** covers the
previous scorer's engineering scope, with three P3 findings and no required changes.
Its full verdict is retained: **18,179 bytes**, SHA-256
`22c5398186dd3385742a86e7536bdc71a8ab1389a6844d478a009d7a6c137fea`.
The peer independently recomputed placement/membership/report statistics and replayed
the direct estimator from held PCM; it ran no build, tests, native binary or retained
script. That approval does not extend to this follow-up or production qualification.

## Code change

`tools/singing_quality/score_rendered_notes.py` previously hashed the WAV but trusted
the native JSON's rate, channels and extent. A matching hash with internally consistent
wrong metadata could therefore misplace note windows. `feature_metadata` now reads the
WAV once and uses those same held bytes for both its hash and `wav_geometry`.

The geometry reader walks the entire little-endian RIFF/WAVE container, bounds chunk
headers/bodies/padding, requires unique format/data chunks and consistent RIFF size,
byte rate, channel count, block alignment and complete nonempty frames. It supports
the native reader's PCM8/16/24/32 and Float32 geometry, including the standard extensible
PCM/Float GUIDs. It checks geometry only; it does not decode PCM or authenticate the
analysis's F0 values. Source rate, channels and frames must equal native analysis
metadata. The mono training protocol specifically requires one channel and an equal
`frameCount`; the stereo/general analyze protocol checks its explicit `channels/frames`.

Negative `hostStartOffsetTick` is also refused, matching `Project::validate`, even
when later notes would still project to positive times. The existing timing rules
remain: add host offset in seconds after document tempo projection, but add render
shift to region ticks before projection. No DSP, native source or criterion changes.

## Controls that distinguish the defects

Ten scorer methods are new, bringing the focused suite from 29 to **39**. They cover
five metadata/protocol mismatches, a negative host offset, valid PCM/Float/extensible
geometry, padded unknown chunks and ten malformed-container subcases. Two existing
offset methods now use bounded pitch spans, so the two placement models produce
different medians under a 120-to-60 BPM change. Mock WAVs have valid silent geometry;
their mocked pitch frames remain independent and are not native measurements.

The unchanged parent scorer with these tests produces **16 failed assertions across
seven methods**: six single assertions and ten malformed-container subcases. After
repair, all **39** methods pass. Two controlled current-source mutations then change
only host placement to tick addition, or only render shift to seconds addition.
Each strengthened case fails; changing only its pitch span back to the former broad
span makes it pass. These are adapted weak controls with valid WAV fixtures, not an
execution of the entire historical c111 test file. Source/tests are restored
byte-for-byte after each control group; restoration changes their mtimes.

The first broader CTest run has **199 methods / one failure / two errors / 19 skips**.
All three affected bank methods call the shared `feature_metadata` with old non-WAV
mock identity bytes; the algorithm-error assertion consequently sees the earlier WAV
refusal. Only `run_mocked_features` in `test_bank_root_pitch.py` is migrated to a valid
8,192-frame mono silent PCM16 WAV. Pitch assertions, bank assets, classifications and
tolerances are unchanged. The repaired entry passes **199 methods**, with **19 skips**
and **180 executed methods**, in **2.593 s**. The scorer's 39 are included, not additional.
This fixture dependency is distinct from the old c111 demo-bank classification failure
and does not independently approve the separate d986 diagnostic unit.

## Real retained-song executions

The stereo diagnostic consumes copied original saved-project/WAV/native-analysis bytes.
The WAV is 48 kHz Float32, two channels, **2,004,000 frames / 16,032,044 bytes**.
Its old Direct-v3 analysis is reused; no new stereo native `analyze` run is claimed.
The new score's note rows and summary equal the original confirmed report exactly;
only copied-input path fields differ.

Both original channels are byte-identical. A new mono Float32 file retains the exact
left-channel words, without resampling: **2,004,000 frames / 8,016,044 bytes**,
SHA-256 `c6f8f1a5cc36e6f389ba36f14f333dcb542b6159d266b47da9e957f972044672`.
Actual unchanged `seam_voicebank_cli extract-pitch` produces **7,829 FFT-v3 full-hop
frames** in **0.962 s**; raw stdout is retained as a separate feature input. Actual stem
scoring invokes the CLI again and passes in **1.067 s**. The stereo score takes **0.086 s**.
Both reports select **2,530 frames**, measure/pass all **48 notes**, and have worst
absolute note medians approximately **0.680453 cents**, maximum frame error
**2.620477 cents**, and all selected frames within 50 cents. They are two diagnostic
executions of the same procedural recording, not independent singer/listening evidence.

Six fresh saved-song inputs are refused with exit 2: source-hash mismatch, rate/channel/
extent mismatches, FollowHost and negative host offset. The wrong render-offset run exits
1, measuring 47 notes with only six passing. All changed inputs and receipts are retained
separately; every protected input's before/after digest stays unchanged.

## Retention and acceptance boundary

The old c111 `mismatched-analysis.json` is actually the receipt that overwrote its own
modified analysis input. That historical input is **lost**. The old refusal stream and
unit test remain, but the old dossier's blanket “negative inputs retained” wording was
incorrect. Fresh input/receipt separation here is later evidence, not reconstruction of
the lost input. Wrappers put inputs under `inputs/` and receipts under `runs/`, constrain
labels and create new receipts exclusively, preventing that pathname collision.

The [evidence index](NOTE_PITCH_WAV_BINDING_2026-10-15.evidence.json) pins **108 artifacts /
12 preserved inputs** under `out/evidence/installed-clap-song-pitch-review-2026-10-15`.
The **19,364-byte** manifest SHA-256 is
`4fb0fd8224c676a5c8f8cf92fe1eab72ef054bac66cefb164838cb2f984c81eb`.
The read-only audit checks exact parent/final Git source, controlled mutation shapes,
twenty receipts, source/native-binary/input bindings, report recomputation for both
feature protocols, exact mono derivation, the full verdict and canonical state. It
also rehashes all **71 old pitch artifacts / 49 bank-diagnostic artifacts** and binds
historical source through immutable Git rather than today's changed bank fixture.
It imports/executes no retained implementation or native code. Old roots stay frozen.
Replay supports an explicit `--source-commit` for this unit's exact source revision.

This Python-only unit has **no native rebuild**; the existing CLI binary is retained
and stays unchanged throughout. Bindings show local byte consistency, not authenticated
or hermetic build/execution provenance. Full-suite, physical F0/phase, phonetic steady-
span corpus, listening/Gate A/B, real DAW, VoiceOver, Windows, released resources,
original/classical/neural/R9 scope and reviewed integration still remain.

No internet recording is downloaded/imported here. The 297 internet WAVs still require
rights/public-license disposition, genuine producer/distinct reviewer, coverage,
failed-QC policy and listening before bank admission. Alpha remains **0/20 recorded
passes**, U45 held, Beta **BLOCKED**, full goal ACTIVE. Canonical files, original
recordings, `.github`, README Windows TODO, prior evidence and the stash are preserved.
