# Project SEAM Beta Readiness: Discovered Issues

## September 20 correction: combined-model validation overlap

The five-song acoustic validation regression cohort is **not jointly held out**
from the acoustic model and vocoder. Songs 00003, 00005 and 00024 occur in the
candidate vocoder's completed epoch-one training coverage. The source-WAV hashes
also match its original training corpus. Songs 00402/00420 are absent from that
original corpus; this alone does not prove independent source/recipe ancestry.

Prior acoustic/vocoder comparison numbers remain diagnostics, not generalization
or Beta qualification. The campaign now exposes acoustic-only validation scope,
keeps combined-model holdout unverified, and optionally audits checkpoint/export/
bundle-bound vocoder training overlap. Retain all five regression songs; qualify
on a separate frozen cohort checked against both models' entire training ancestry.
Details and exact evidence: `docs/implementation/SINGER_VALIDATION_CAMPAIGN_2026-09-20.md`.

- **Snapshot date:** 2026-08-30
- **Branch:** `codex/external-beta-completion`
- **Source commit:** `970d159d06a2daa11932a9dbc22a337ecf9dbe25`
- **Current decision:** `HOLD / NO-GO` for an open/public beta
- **Estimated practical readiness:** approximately 42% (engineering estimate, +/-5 percentage points)
- **Formal promotion readiness:** 0% because the canonical Usable Alpha matrix is 0/20 PASS and no External Beta candidate evidence has been accepted

This file is the root-level issue register requested after the repository-wide beta-readiness review. It is intentionally shorter and more operational than the full analysis in [docs/reviews/PROJECT_SEAM_OPEN_BETA_READINESS_2026-08-30.md](docs/reviews/PROJECT_SEAM_OPEN_BETA_READINESS_2026-08-30.md).

The register distinguishes implementation from release proof. A source contract, validator, unit test, or schema can be complete while the corresponding installed-product gate remains open.

## Severity and status

- **P0:** Blocks any honest external beta claim.
- **P1:** Must be closed before a controlled external cohort is supportable.
- **P2:** Does not independently block a small private alpha, but materially raises beta regression and operating cost.
- Every item below is **OPEN** unless a later evidence-bound update explicitly changes its status.

## P0: Release blockers

### SEAM-BETA-P0-08: Vocoder reconstruction remains pitch-inaccurate after training

**Discovered:** 2026-09-19, during the first full-song render through the shipped native worker.

**Initial evidence (three-update checkpoint)**

> **Status note added 2026-09-20.** The measurements below are historical. They still describe the
> three-update 32-channel smoke checkpoint they were taken from, but the headline signature — peak at
> 21000 Hz with energy share 0.48 above 16 kHz — **does not reproduce on any retained artifact read
> today**. Direct measurement of the retained outputs gives a spectral peak at 187.5 Hz with 0.85% of
> energy above 16 kHz for `recon-tracked4/item-000001.wav`, and a peak at 562.6 Hz with about 1.2% above
> 16 kHz for the four items under `recon-e6-native-pitch/`. The current defect is a harmonic comb at
> exactly 48000/256 = 187.5 Hz — the mel frame rate, which is closure condition 4's "hop-rate artifact" —
> plus a level error: `renderedRms` 0.001872 against `sourceRms` 0.025662, about 13.8 dB low. The
> frame numbers below (908 measurable voiced frames, median error 1276.6 cents, mean 1525.4 cents) *do*
> still reproduce. Also note the subject: the 168336-byte export this evidence describes is the
> 32-channel `mini-nsf-32-smoke-v1` fixture, not the 512-channel `mini-nsf-512-mrf-v1` architecture
> that the current r3 run is training, so none of these numbers characterize the capacity SEAM is
> actually building. See SEAM_COMPLETION_REVIEW_2026-09-20.md sections 3.3 and 3.4.

- The logistic entry in [docs/implementation/INTEGRATED_SINGER_EXECUTION.md](docs/implementation/INTEGRATED_SINGER_EXECUTION.md)
  states "the vocoder is correct; the acoustic model is not". That claim is withdrawn there, with the
  measurement that contradicts it.
- Feeding the vocoder the ground-truth mel of a held-out corpus song plus its measured f0 gives an
  output whose energy share above 16 kHz is **0.48** and whose spectral peak is **21000 Hz**, against a
  source that peaks at **294 Hz** with 0.05 above 16 kHz.
- Rescaling the mel by `1/ln(10)` and by `ln(10)` to test the log-base convention leaves the peak at
  21000 Hz in both cases, so this is not a normalization mismatch.
- A constant mel with constant f0 at 110, 220 and 440 Hz produces the same noise instead of the
  harmonic series the f0 conditioning should place.
- The checkpoint the shipped export was built from is
  `/Users/lhs/seam-corpus-2026-09-19/six-vocoder-run-4/epoch-000001`, which records `updates: 3`,
  `completedEpochs: 1` and `validSamples: 180000`.
- The project's own retained receipt already reported `meanSpectralDistance` 51.17,
  `f0MedianErrorCents` 832.9, `pitchStatus` FAIL and `allReconstructionsSatisfied: false`.

**Consequence.** Every acoustic model measurement taken through this vocoder, including the
`pitch-adherence FAIL` verdicts in the qualification dossiers, was taken through a stage that cannot
pass them, so those numbers understate what the acoustic stage learned. Two broken links exist in the
signal path, not one; the vocoder is downstream of the acoustic model.

**Status:** OPEN. The first run retained six complete epochs and is no longer running. Epoch 6 was
exported with passing Torch/ONNX parity. Its old synthetic `pitchFollowsRequestedNote` flag only
required two different measured pitches, not accuracy. Revision 2 separates pitch response from
accurate following: epoch 6 fails the latter because requested 440 Hz measured 461.405 Hz
(82.24 cents, exceeding the stated 50-cent diagnostic budget). Independent native-pitch evaluation
on all 12 configured held-out songs reports
mean spectral distance **1.250432**, mean absolute pitch error **1506.563 cents**, **12162 measurable
voiced pairs**, and **12/12 FAIL**, with zero unresolved items. For song 00008, source pitch agrees
exactly with its conditioning on the measured comparison frames while rendered pitch remains near
187.5 Hz. Thus synthetic conditioning response does not establish realistic reconstruction.

The continuation `vocoder-xl-pitch-r1` stopped with ENOSPC during epoch-seven checkpoint publication.
Its completed evaluation still fails: mean spectral distance 1.239364 and mean pitch error
1506.023 cents. No epoch-seven completion receipt exists; epoch six remains the verified resume point.
The failed run's partial optimizer binary was removed to recover working space; model bytes and
evaluation evidence remain. New disk-headroom checks run before allocation, around updates and before
checkpoint serialization. Rolling retention cannot free an old checkpoint before a verified successor
exists and does not fix pitch quality. See the execution log for artifact hashes and exact limitations.

**The closure condition originally written here was unreachable and has been corrected.** It required a
reconstruction receipt reporting `allReconstructionsSatisfied: true`, which is `spec_ok and pitch_ok and
energy_ok`; `pitch_ok` needs framewise pitch tracks that no producer supplied, so the term could never be
satisfied and every receipt reported it UNRESOLVED. The producer now exists
(`build_pitch_tracks` plus a `--pitch-executable` flag on the training CLI), so those receipts now carry a
real verdict.

Closure now requires all four of the following, and the second is deliberately not `pitch_ok`, because
measured on one real song only 908 frames were measurable. The comparator permits padded edge windows,
but any interior low-confidence voiced frame prevents a matching verdict even for identical audio:

1. A trained checkpoint whose reconstruction receipt reports `spec_ok` and a measured pitch error inside a
   stated budget on the measurable frames.
2. An owner decision on whether the pitch term is a conjunct of `reconstruction_satisfied` or a separate
   gate with its own threshold and coverage requirements, calibrated against identical-source controls.
3. A re-export whose graph is verified to follow the requested note, using the
   revision-2 `pitchFollowsRequestedNote` measurement recorded in the vocoder export receipt, requiring
   all four notes within 50 cents and at least 80% voiced coverage each. Epoch 6 does not satisfy this
   accuracy diagnostic. Even a pass would not substitute for item 1 or item 4; its estimator is
   target-windowed, whereas held-out evaluation uses the independent native pitch extractor.
4. A repeated full-song render whose spectrum is consistent with the source rather than with broadband
   noise or the hop-rate artifact.

### SEAM-BETA-P0-08 update: the 512-channel epoch was evaluated, and the pitch failure largely clears

**Measured 2026-09-20**, appended rather than rewriting the entry above.

The r3 run completed its epoch (2,804/2,804 updates, `epoch-000001`, receipt
`cec64e7c5adb3ec81cceaa7c81046ca8d62447e0126f49df2bba639fe536dd94`) and was exported and evaluated. It is
the 512-channel `mini-nsf-512-mrf-v1` architecture, 13,936,386 generator parameters.

| Measure | 32-channel smoke fixture | 512-channel epoch 1 |
|---|---|---|
| held-out mean absolute pitch error | 1506.563 cents | **46.676 cents** |
| frames within 50 cents | 0 of 908 | **11,376 of 12,186 (93.35%)** |
| per-item median absolute error | -- | **0.46-0.69 cents** |
| export `pitchFollowsRequestedNote` | false (440 Hz off by 82.24 cents) | **true, all four notes within 8.53 cents** |
| spectral peak | 187.5 Hz, the hop rate | **293.8-494.0 Hz, per-song fundamentals** |
| energy above 16 kHz | 0.0085-0.012 | **0.0002-0.0018** |

So the pitch inaccuracy this issue is named for is largely resolved once the correct architecture is
trained, and **closure condition 4 is met for held-out reconstruction** — the spectrum is now consistent
with the source and the hop-rate artifact is effectively absent (187.5 Hz band share about 1e-5).

**Closure condition 2 now has the control it asked for, and the answer is that the conjunct is
unsatisfiable.** Comparing a held-out render against itself returns
`UNRESOLVED | measurable 958 | within 958 | outside 0 | voicingMismatch 0 | unmeasurable 45`. Bit-identical
audio cannot satisfy `comparisonSatisfied`, because `tools/voice_model_training/pitch_comparison.py:155` requires
`MATCH_ON_MEASURABLE_FRAMES` and any unmeasurable interior span holds the status at `UNRESOLVED`. The
canonical contract's own `pitch-within-50` criterion asks for "minimum 90 percent" and this epoch measures
93.35%.

**Status stays OPEN, and the conjunct now reduces to a single term.** `allReconstructionsSatisfied` is
`spec_ok and pitch_ok and energy_ok` (`vocoder_reconstruction.py:302-305`). `spec_ok` passes at 0.97
against a 3.5 budget. `energy_ok` also passes: it is a peak-floor plus no-clipping check and never compares
RMS to the source, so the 5.7-7.6 dB RMS shortfall visible in the receipts is a quality observation, not a
gate failure. That leaves `pitch_ok` alone, which the control above shows no audio can satisfy. The live
blocker is therefore the owner decision in closure item 2, not the model.

Also still true: this is one epoch against a 60,000-update budget; the labels are
`com.project-seam.training-generated-teacher` so R9 still requires a rights-cleared corpus; and no listener
has heard the output.



### SEAM-BETA-P0-08 application-path follow-up: improvement is not closure

**Measured September 20, 2026.** The statement above that the blocker is "not the
model" is too broad. Held-out vocoder reconstruction uses source mel/F0; the actual
application also depends on the acoustic model and score conditioning. Likewise,
one identical-input control with unmeasurable frames does not prove that *no audio*
can satisfy the strict comparator. It demonstrates a coverage limitation for that
control. Its 93.35% within-tolerance figure has measurable pairs as denominator,
not all score frames, and does not by itself establish the product pitch criterion.

The same saved pause song was exported with the existing epoch-9 acoustic model
and the new 512-channel epoch-1 vocoder. Production-worker execution, committed
master/stems/project, and saved-project reopen pass. The new master hash is
`f5002b034a4e18d26d5c6fbf92a47038c7718d15adf7cb9cf4f391ce852a9197`.

Both old and new masters were compared by the same new reproducible command,
`tools.voice_model_training.compare_application_export`, with source
`prepared/song-023/source.wav`, exact 150,000-frame alignment and arithmetic stereo
downmix; no gain correction or time adjustment:

| Application master | Spectral distance | Measurable voiced pairs | Within 50 cents | Mean absolute cents | Strict pitch |
|---|---:|---:|---:|---:|---|
| Old 32-channel epoch 6 | 2.76567 | 128 | 0 | 1668.23 | MISMATCH |
| New 512-channel epoch 1 | 2.03145 | 364 | 227 | 675.69 | MISMATCH |

The new result improves substantially but only 62.36% of measurable voiced pairs
are within 50 cents. It is still a musical-quality failure. The earlier assertion
that closure condition 4 was met by held-out reconstruction did not establish a
successful application song render. No acceptance threshold was changed here.

Reports under `/Users/lhs/seam-corpus-pauses-2026-09-19-r1/`:
`application-e9-v512-e1/comparison.json` (SHA-256
`a41b7942262abd564132f8c8da6c312531115bdc694a25ece3650eef7be8bfeb`) and
`application-e9-v6-score-silence/comparison-v2.json` (SHA-256
`a512c8cba13f0ff4a0d4ec27fcf999a2d835645bbd112fd3bac05c4682745e97`).
The old historical measurement is preserved; these are fresh comparisons using
captured float32 pitch inputs on both sides. The candidate remains unqualified.
Verification: five new comparison tests cover signed PCM16/24/32 decoding,
channel ordering/cancellation, identical-signal distance, truncation and unequal
length refusal. Full training-tool discovery ran 237 tests with one skip and no
failures; source closure and phase11 source checks passed. Real native extraction
was executed for both reports above, separately from mocked unit-test pitch calls.

Further stage isolation on September 20: replacing only the pause-only acoustic
graph with the existing combined-corpus epoch-9 export raises this application's
within-50-cent count to **421/447 measurable voiced pairs**, with mean error
**102.93 cents** and spectral distance **1.62676**. The source-driven vocoder control
measures **474/504**, **40.75 cents**, and **0.92164**, but produces voiced rest
noise without the application's score envelope. All strict comparisons still
report `MISMATCH`. See [stage comparison](docs/implementation/SINGER_STAGE_COMPARISON_2026-09-20.md)
for fixed inputs, graph/audio hashes, G5 diagnosis, test-set limitations and next
steps. These results improve the engineering candidate; P0-08 remains open.

**Where most of that error is, measured 2026-10-04.** Reading the retained application
comparison as a distribution rather than a mean: the median frame is within 3.54 cents, but 23 of
its 586 frames carry 93.4% of the total absolute error, and 20 of those 23 are exact negative
multiples of 1200 cents. All 23 are marked as confident comparisons, not as low-confidence frames,
and every one is voiced on *both* sides -- these are octave disagreements about notes both signals
agree are sung. Removing those 23 frames would drop the mean from 102.93 to 7.17 cents, which says
the aggregate failure is concentrated rather than diffuse, and does not by itself say the rest passes.

An earlier version of this entry attributed the errors to the earliest-qualifying-peak rule in
`libs/seam-voicebank/src/pitch.cpp` having no continuity term, and reported seven reproduced octave
jumps. **SUPERSEDED — see the third correction below; the premise of this paragraph is wrong.** The
attribution was withdrawn on the same day on the belief that the reproduction had been run against
`master.wav`, which is not the audio the comparison measured (the stored candidate hashes to
`b452273c...`, `master.wav` to `015f386a...`), and the 16-bit downmix clipped samples. Measured on
the retained candidate track itself there are two post-gap octave jumps, and re-extracting
`master.wav` as float32 gives none. The rule still has no continuity term and remains a plausible
contributor, but these 23 errors are not attributed to it. The full correction, including that a
repair attempt was measured against the wrong file and is therefore untested rather than refuted,
**A third correction, 2026-10-04: the premise of the paragraph above is itself wrong.** The claim that
`master.wav` is not the audio the comparison measured does not hold. `comparison.json` records
`candidateSha256` as **`015f386a59f826904e58b0f09b065185b50428cc5effc9aca0ff5c635c0e05a3`**, which is
the digest of `master.wav` in the same directory. There is no `b452273c...` file there. The corpus is
24-bit stereo, so any reproduction must downmix before the bounded extractor will accept it — that, not
a file mismatch, is why naive re-runs failed.
Re-measured from that audio, **the errors are not octaves.** Against the stored reference the ten
misreported frames are **−2478 to −3692 cents**, i.e. **−2.05 to −3.08 octaves**, at lags 512 and 768.
Against each frame's true period those lags are **2.8 to 12.7 times** — not harmonic multiples. So the
"earliest qualifying peak picks the double period" mechanism does not describe this audio.
**What they are:** ten frames of **541** voiced (1.8 per cent) locked to the bottom of the 60–1200 Hz
range, with a clean histogram gap between 100 and 125 Hz. **Four repairs were measured and rejected:**
preferring the strongest peak made it worse (10 → 29, reverted); adjacency to a voicing gap applies to
1 of 10 bad frames against 7 of 531 good ones, so the post-gap theory is unsupported; an RMS floor that
catches all ten drops **87.6 per cent** of good frames; and confidence does not separate them (bad
0.41–0.88 against good 0.33–1.00). No property inside the frame distinguishes them.
**The stakes are lower than the earlier text implied, and the question has changed.** At 1.8 per cent of
voiced frames, outside the sung range, this is bounded. The listening packet's question is no longer "is
this an octave?" but **"is the audio at those ten frames a note at all, or a breath or silence?"** — if it
is not singing, the right answer is to report those frames unvoiced, which is a judgement about the
audio rather than the code. Nothing in the product changed; the one attempted code change was reverted.
**Read from the two stored pitch tracks, the anomaly is on the candidate side.** Both tracks have
the same median F0 (335.7 Hz against 333.3 Hz), so they are singing the same note. The candidate
has 231 distinct voiced pitch values to the reference 121, and **14 frames sitting at exactly
187.5 Hz -- a lag of 256, which is this analysis hop size, so those frames report the frame period
as its own pitch.** Five of them sit where the reference reports *unvoiced*, and the candidate
confidence rises from 0.61 to 0.92 across that stretch. Three probes through the shipped extractor
rule out a general failure: a plain 300 Hz tone reads 300.0 Hz on all 375 frames, a tone followed by
digital silence gives 0 voiced frames in the silence, and a tone followed by low-level noise gives
0 voiced frames in the noise. Confirming the hop-locked frames needs the candidate audio itself,
which the comparison records only by SHA-256.

**A first U16 corpus render is now measured, and it disagrees with its own score.** The U16 quality
tooling was run end to end on the checked-in corpus for the first time. All four dry vocals carry real
signal with no clipping: the 41-second melody at peak 0.5026 and RMS 0.0691, the 9.12-second case at
peak 0.4925. The project writes MIDI 62 to 72, which is 293.7 to 523.3 Hz, but the shipped
extractor reads only 3.5 per cent of voiced frames inside that range for the bank render and 4.5 per
cent for the forced raw render. In the bank render 59 per cent of voiced frames are above the highest
written note. Both renderers show it, so it is not the bank selection. Nobody has listened to these
renders, and the corpus notice states the bank gives eight phoneme labels the same spoken recording
by design, so this is a measurement discrepancy to investigate, not a musical finding. Details and
two claims of mine that checking disproved are in the diagnosis.

**The U16 pitch discrepancy is the fixture, and both suspects are now cleared.** The extractor reads
four synthetic tones at MIDI 60/64/72 to within 0.001 Hz at confidence 0.9998, so the earliest-peak
rule does not misreport clean audio. The bank is the cause: `production-bank/manifest.json` names
itself a *Public-domain Human Production Pipeline Fixture*, and all **8 units** reference the single
file `audio/human-vowel-demo.wav` at **rootMidi 67** — every phrase's `resources` list carries the same
`audio_sha256 caf8ceb0...`. That recording measures at a **median 990.07 Hz (MIDI 83)** against a
score asking MIDI 62-72, so a correct renderer cannot produce an in-range fundamental and the
extractor reports the harmonics that really exist. An independent re-run matches the packet:
5745 voiced frames, 196 in range (3.4 per cent). **The U16 corpus is not a pitch-accuracy test and
never was** — its own notice says it exercises timing and fallback. This does not reopen the separate
extractor defect measured on the retained real-sung-render comparison, which remains open pending a
listening judgement.

is in [the diagnosis](docs/implementation/PITCH_TRACKER_OCTAVE_ERROR_2026-10-04.md). Nothing is
fixed, no threshold is proposed, and this does not qualify a singer or close this blocker.

**A never-run tool was silently discarding 24 bits per sample.** `seam_clap_state_tool` was referenced
by zero files and had never been executed, so its path carried no evidence at all. Run end to end it
reported success while writing every extracted render as PCM16: a float32 WAV round-tripped through
`pack`/`extract` came back with every mantissa bit truncated, and a 24-bit render came back at half
depth. The state format itself was never lossy -- it stores normalized float PCM -- so the loss was
entirely at the write boundary, which is why frame counts, RMS and byte-identical framing all looked
correct while the payload was not. `PluginSession` now records the source encoding
(`ClapSampleFormat`), the codec carries it in the SEAMCLP1 header slot that every writer previously
left at zero, and `extract` writes the format it was packed from and reports it. Existing state files
stay readable: the zero slot decodes as `pcm16`, which is exactly what `extract` always emitted, and
an unknown nonzero id is rejected rather than guessed at. Measured float32 and 24-bit round trips are
now bit-identical. This closes the defect, not the larger gap: the other never-run tools
(`seam_singer_pilot`, `seam_neural_production_render`, `seam_editor_native`, `seam_neural_worker`,
`seam_installer_verifier`, `seam_voicebank_studio_native`, and the CLAP hosts) have not been given
the same treatment yet.

**Correction to that list: `seam_singer_pilot` is not among the unverified tools.**
`tests/test_singer_pilot_cli.py` drives every mode it supports -- `articulation`, `boundaries`,
`nasals`, `stops`, `affricates`, `glides`, `events`, and authored `phrase` -- runs each twice to
assert the rendered bytes are identical, and asserts that malformed phrase input is rejected. The
`seam_singer_pilot_cli` test passes. Two real observations remain: the binary has **no argument
parser**, so `--help` is taken as an output directory and the tool writes a complete pilot packet
into a directory named `--help`, and the remaining tools on that list have not been re-probed yet. A
CLI that creates output for an unrecognized flag is a defect in its own right.

The subsequent fixed five-song validation campaign measures **3090/3649 (84.68%)**
within 50 cents on measurable voiced pairs, with every strict comparison still
`MISMATCH`. It also exposed and repaired a separate application blocker: standalone
Japanese `ん` notes lacked resolved start timing. The compiler now supports a sole
untimed voiced moraic `N` without inventing a vowel, and the previously rejected
song exports and reopens. See the [validation campaign](docs/implementation/SINGER_VALIDATION_CAMPAIGN_2026-09-20.md)
for all results, the timing-policy revision and evidence boundaries. The earlier
94.18% on one test song must not be generalized to the validation set.

The planned combined acoustic continuation has since completed through epoch
thirteen. With the same vocoder and fixed five validation songs, application
within-50-cent accuracy improves to **3781/4033 measurable voiced pairs (93.75%)**,
and mean absolute pitch error falls to **77.46 cents**. All five songs export and
reopen, with exact zero audio in their explicit score rests. All strict pitch
statuses still report `MISMATCH`. Per-song results and hashes are appended to the
validation campaign report; this is measurable progress, not closure of P0-08.

**Update 2026-10-04: the residual pitch artefact is located, and it is absent from current output.**
The remaining pitch blocker above was the octave defect in the retained application render. It has now
been measured to its cause. Full evidence is in
[PITCH_TRACKER_OCTAVE_ERROR_2026-10-04.md](docs/implementation/PITCH_TRACKER_OCTAVE_ERROR_2026-10-04.md);
three claims in that report were wrong and are corrected there in place.

The defect is **46 of 541 voiced frames (8.5 per cent), not the ten previously recorded**. Every one of
them reports a period that is an exact multiple of the 256-sample analysis hop; the reference track has
**zero** such frames, and the separation is perfect at a tolerance of 0.001 hops. The errors **are**
octave errors, not the non-octave readings previously claimed. The cause is an impulse train spaced one
hop apart inside the rendered audio, confirmed by three independent checks: it survives every change to
the analysis hop from 128 to 1024, so it is not an analysis artefact; a spectral estimator validated at
97.1 per cent on correctly tracked frames finds the correct note at 83.3 per cent of the disputed frames,
so the singer is singing and the tracker reads an octave low; and six candidate repairs were measured,
none shippable, because the hop-parity rule that removes the defect also silences a genuine 187.5 Hz
note.

**It does not reproduce at HEAD.** An end-to-end render of an installed procedural singer through the
real authoring and export stack gives 7692 frames, 7456 voiced and **0** lag-exact frames, against the
retained build's 46. The render is populated, not silent: 41.0 s, peak 0.0918, RMS 0.0152, medians
293.6-392.1 Hz. The best-evidenced cause is `1c6d57c6`, which halves the spectral analysis hop across a
voiced-to-unvoiced-to-voiced transition and postdates the build that produced the retained audio. It is
not proven: the retained project cannot be re-rendered, because its track resolves through neural
resource `seam.pause-experiment` v3 with content hash `d4dd7737...`, which is not on disk anywhere.

**P0-08 therefore remains OPEN.** No cent-level pitch-accuracy verdict has been produced on a current
application render, and no qualification follows from a defect being absent. Release CTest 230/230 passed
at `17301814`.

**Update 2026-10-04: the cent-level verdict the previous entry said was missing now exists.** It was
produced by scoring the HEAD render's measured pitch against the **written score**, so the target for
every frame is the note the composer asked for rather than a second analyser reading.

Method: an installed procedural singer is rendered end to end through the real authoring and export
stack, the 48 authored notes are read from the same C++ literal the test renders from (41.0 s at PPQ 960,
120 BPM, MIDI 60-69), and each frame is scored against its own note. The rule is the project's own, from
`tools/singing_quality/acoustic_metrics.py`: only the steady span of each note is scored (from 25 per cent
in, so the consonant transition is excluded), frames under the 0.60 confidence floor are counted rather
than scored as correct, and the 1200 Hz analyzer ceiling is excluded rather than counted as an error.

| Render | Scored frames | Median \|cents\| | Mean \|cents\| | Within 50 c | Within 200 c | >=200 c | Octave (>=600 c) | `withinLimits` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :---: |
| `baseline-master.wav` | 7344 | **0.180** | 4.396 | **98.05 %** | 99.77 % | 17 (0.231 %) | 3 (0.041 %) | **false** |
| `tuned-master.wav` | 7334 | **0.208** | 4.908 | **98.32 %** | 99.66 % | 25 (0.341 %) | 4 (0.055 %) | **false** |

**Sub-centre median accuracy on a real sung render.** 0.18 cents is roughly a thousandth of a semitone.
All 48 notes produced scored frames; none was empty. Steady-span medians are 0.166 and 0.194 cents.

**And `withinLimits` is still false**, which is the honest result and the reason P0-08 stays open. The
project's rule requires zero octave errors, and there are 3 and 4. Every one is inside a single note, and
the baseline case is fully characterised: at t = 7.471 s and 7.477 s, inside note `を` (MIDI 64, target
329.63 Hz), two adjacent frames report 164.41 Hz and 109.01 Hz at confidence 0.724 and 0.685, bracketed
by correct neighbours at 329.50 and 329.88 Hz. **An independent spectral estimator reads 328.12 Hz at both
frames, within 7.9 cents of target** — so as with the retained-render defect, the singer is producing the
right note and the tracker misreads it at the note boundary. The chosen lags are 292 and 440 samples,
neither on the 256-sample grid, so this is a different failure from the impulse train already characterised,
and much smaller: 3 frames in 7344.

**What this changes.** P0-08's substance is now measured rather than assumed. A current application render
tracks its written score to a 0.18-centre median with 98 per cent of frames inside 50 cents, which is the
figure the blocker has been waiting on. What remains is a **0.04 per cent boundary artefact**, correctly
characterised, with an independent oracle proving the audio is right and the analyser wrong.

**What this does not change, and is not claimed.** No qualification follows. `withinLimits` is false on the
project's own rule, nobody has listened to this render, one song on one singer is not a corpus, and the
five-song validation campaign's own figures are the corpus-level number. Closing P0-08 needs either the
boundary artefact repaired and a multi-song corpus scored under this same rule, or an explicit, recorded
decision that a 0.04 per cent note-boundary artefact is below the bar, made by someone who has listened.

**Update 2026-10-04, corpus level: the five-song campaign's poor pitch figures are the impulse artefact,
not the singer.** The multi-song corpus requirement was met by scoring all five songs of the retained
campaign `campaign-e37-v512-e2-r4` against their own written scores, read from each song's `input.seam`
under the same rule. Those renders are from the **older build `741ae2f2`**, not HEAD.

Scored as-is, the five songs look poor: median 2.1 to 4.0 cents, 73 to 81 per cent within 50 cents, and
734 octave frames out of 3551 scored, which is 20.7 per cent. **That number is an artefact of the
analyser, and the data says so unambiguously.**

| Song | Scored | Hop-locked | Median, all frames | Median excluding artefact | Within 50 c excluding artefact |
| --- | ---: | ---: | ---: | ---: | ---: |
| 00003 | 1011 | 187 | 3.596 | **2.482** | 94.2 % |
| 00005 | 880 | 183 | 3.077 | **1.889** | 94.6 % |
| 00024 | 789 | 165 | 3.944 | **2.306** | 92.6 % |
| 00402 | 463 | 68 | 2.106 | **1.540** | 96.2 % |
| 00420 | 408 | 58 | 3.966 | **3.137** | 91.4 % |
| **pooled** | **3551** | **661 (18.6 %)** | 3.596 | **2.221** | **93.36 %** |

The split is not a judgement call. **661 of the 3551 scored frames report a period that is an exact
multiple of the 256-sample hop**, which is the impulse-train signature already characterised from the
retained comparison in this register. Those frames have a median error of **1976 cents and are 93.2 per
cent octave errors, with 0.00 per cent inside 50 cents**: they are the artefact, wholesale and
unambiguously. Excluding exactly those frames leaves 2890 frames at a **2.22-centre median with 93.4 per
cent inside 50 cents**, consistent across all five songs.

**An independent oracle confirms the audio is right.** An FFT estimator was run over the same frames and
finds the written note within 100 cents at **89 per cent** of them, on audio this analysis did not
produce. The campaign's own `combinedModelHoldoutVerified` is `false`, which is a separate and still-open
question about those models and is untouched by this measurement.

**What this changes.** The campaign's headline pitch figures are not a measurement of the singer. They are
the known analyser blind spot applied to five songs' worth of audio, and once it is separated the residual
accuracy is **2.2 cents median, 93.4 per cent within 50 cents** on the old build, against **0.18 cents
and 98.05 per cent** on HEAD. Both are far inside the 50-cent tolerance; the old build is simply worse by an
order of magnitude on the median.

**What this does not change, and is not claimed.** This is a re-analysis of retained evidence, not a new
render: the five songs were produced by `741ae2f2` and **have not been re-rendered at HEAD**, so this says
nothing about how the current build performs on a five-song corpus. It is still one singer family. It does
not close P0-08, which remains **OPEN** for the reasons already recorded: `withinLimits` is false, nobody
has listened, and `combinedModelHoldoutVerified` is false.

**Update 2026-10-04: the five songs re-rendered at HEAD. The impulse artefact is gone; a different
2 per cent octave residual remains and is not yet explained.** The previous entry re-analysed retained
audio. The five songs have now been **re-rendered with current HEAD code** and scored under the same rule.
Each project's own `proceduralRecipe` was used through `ExportService::exportSetWithSources` with
`TrackRecipeFileSource`, which is the production export path, so the recipe is read and identity-checked
exactly as the editor reads it.

| Song | Scored | Median, cents | Within 50 c | Octave frames | Hop-locked |
| --- | ---: | ---: | ---: | ---: | ---: |
| 00003 | 1197 | **0.176** | 96.07 % | 20 | 0 |
| 00005 | 1075 | **0.217** | 96.28 % | 19 | 0 |
| 00024 | 931 | **0.172** | 94.31 % | 28 | 0 |
| 00402 | 524 | **0.258** | 94.85 % | 7 | 0 |
| 00420 | 505 | **0.162** | 93.66 % | 11 | 0 |
| **pooled** | **4232** | **0.176** | — | **85 (2.01 %)** | **0** |

**The impulse-train artefact is eliminated, not merely reduced: 661 hop-locked frames across these five
songs at the old build, 0 at HEAD.** Median accuracy is 0.176 cents, an order of magnitude better than the
2.221 cents measured on the old build's audio, and all five songs exceed 90 per cent within 50 cents.

**But 85 octave frames remain, and they are not the same defect.** Applying the independent FFT oracle
that resolved every earlier finding splits them cleanly, and the split is not favourable:

| Population | Frames | Oracle reads the written note | Position in note |
| --- | ---: | ---: | --- |
| Analyser wrong | 34 (40 %) | yes, median oracle error **-4 cents** | median 42 frames into a 94-frame note |
| Renderer suspect | **51 (60 %)** | **no, median oracle error -839 cents** | median 69 frames into a 70-frame note |

For 34 frames the audio is right and the estimator is wrong, exactly as before. For **51 frames both
estimators miss the written note**, so the audio at those instants does not contain it. Those frames
cluster at the very end of a note: 46 of them sit within 4 analysis frames of the note's end ("93 into 94",
"46 into 47"), where the score's target changes or the voice releases.

**One hypothesis was tested and refuted, and is recorded so it is not retried.** The obvious reading is a
release tail, since a decaying voice has no note left to measure. Measuring window energy relative to each
note's own loudest window does not support it: frames within 4 hops of a note edge have a median relative
energy of **0.623** against **0.645** for frames in the note's middle quarter, and the octave-error frames
specifically sit at a median of **0.751**, i.e. **louder than average, not quieter**. Whatever these 51
frames are, they are not a quiet decay.

**What this changes.** P0-08 now has a genuine HEAD five-song result, and it is strong on the metric that
mattered most: the previously dominant artefact is completely absent and cent-level accuracy is 0.176
cents median across 4232 frames and five songs.

**What this does not change, and is not claimed.** `withinLimits` remains **false** on 85 octave frames, and
51 of them are now a genuine unexplained renderer-or-score question rather than an analyser artefact.
**This is a newly identified open defect, not a closure.** Nothing here has been listened to, it is still
one singer family, and `combinedModelHoldoutVerified` is still false. The next step is to determine why 51
frames near note ends contain neither the written note nor anything the estimators recognise; that is a
rendering or score-alignment question and has not been investigated.

**Update 2026-10-04: those 51 frames are the scoring window, not a defect. The renderer is correct.**
Comparing each unexplained frame against **both neighbouring written notes** rather than only its own
settles it outright:

| Test | Result |
| --- | ---: |
| Frames whose audio contains the **next** written note | **50 of 51** |
| Frames whose audio contains the **previous** written note | 0 |
| Frames matching neither | 1 |
| Frames still inside the note's written span | **51 of 51** |

**The next note is already sounding while the previous note's written window is still open.** The renderer
places a note's onset before its predecessor's written end, so the final frames of each affected note carry
the following note's pitch. The scoring window is monophonic and non-overlapping, so those frames are
scored against a note that has already stopped being sung. Measured onset overlap:

| Song | Note pair | Frames affected | ms before written end | Share of the note |
| --- | --- | ---: | ---: | ---: |
| 00420 | 74 into 72 | 2 | **35.7** | **14.3 %** |
| 00005 | 69 into 60 | 5 | 25.3 | 10.1 % |
| 00402 | 79 into 72 | 5 | 25.0 | 10.0 % |
| 00420 | 79 into 67 | 4 | 20.7 | 8.3 % |
| 00024 | 64 into 79 | 2 | 19.7 | 7.9 % |
| 00024 | 72 into 62 | 4 | 19.0 | 7.6 % |
| 00003 | 79 into 69 | 3 | 14.0 | 3.7 % |
| 00005 | 60 into 71 | 4 | 18.7 | 3.7 % |
| 00003 | 74 into 62 | 4 | 17.3 | 3.5 % |

Fifteen note pairs across all five songs, largest 35.7 ms or 14.3 per cent of a note, typical 1 to 5
analysis frames. This is consistent with a deliberate, small pre-onset or legato transition rather than a
timing fault: it is **early by a few tens of milliseconds, never late**, and it is bounded.

**The conclusion that matters for the blocker.** Of 85 octave frames, **34 are the analyser reading a note
the audio contains correctly, and 50 are the scoring window measuring a note the audio has already left.
That is 84 of 85 accounted for, and not one of them is the renderer singing a wrong pitch.** The single
remaining frame matches neither neighbouring note and is not characterised.

**What this changes.** The 2 per cent residual is not a renderer defect and not the impulse artefact. It is
the cost of scoring a legato render with a non-overlapping window: the same boundary effect already seen at
0.041 per cent on the single HEAD song, visible here at 2 per cent only because these five songs have more
short notes and faster legato. **The correct fix is to the measurement, not the renderer**: a scorer must
resolve the same overlap the renderer does, which the project's own placement records already carry as
`vowel_onset_frame` and `destination_end_frame`, and which this note-span scorer does not use.

**What this does not change, and is not claimed.** P0-08 stays **OPEN**. This is an analysis of one family
of one-singer renders, nothing has been listened to, and `combinedModelHoldoutVerified` is still false. The
84-of-85 accounting is an argument for correcting the scorer, and correcting the scorer is not the same as
reaching the project's own acceptance bar with a defensible measurement.

**Update 2026-10-04: the early onset is real and universal; the cue spans do not describe it, and this
entry corrects the previous one's proposed remedy.** The entry above concluded the fix belongs to the
scorer, on the grounds that the renderer's own placement records would resolve the overlap. **That
remedy was tested and it does not work**, so it is corrected here rather than left standing.

Re-scoring all five songs against the renderer's own vowel spans, taken from
`ProductionProjectRenderer::renderWithSources`, gave **83 octave frames in 3753 scored, against 85 in 4232
from the written-note scorer**. The count did not fall. Comparing the two window sets explains why:

| Cue span versus its written note | Result across all 60 spans |
| --- | ---: |
| Ends after its written end | **0 of 60** |
| Begins before its written start | **0 of 60** |
| Begins after its written start | up to **2880 samples (60.0 ms)** |

**The cue spans end exactly where the written note ends and start up to 60 ms after it begins.** They
describe when the vowel body is placed, not when the audio starts moving to that note, so scoring inside
them still includes the frames where the next note is already sounding. The remedy was wrong and the
original finding stands.

**Measuring the true onset from the waveform settles what it is.** Walking back from each previous note's
written end to the first frame already carrying the next note's pitch, judged by an independent spectral
reading of the audio:

| Measure | Value |
| --- | ---: |
| Notes measured | 33 |
| Notes whose audio begins **early** | **33 of 33 (100 %)** |
| Notes whose audio begins late | **0** |
| Median advance | **928 samples (19.3 ms)** |
| Range | 416 to 4000 samples (8.7 to 83.3 ms) |

**This is a systematic renderer timing property, not a scoring artifact.** Every note in the corpus starts
sounding before its written start, by a median of 19 ms and never late. The 50 octave frames are the tail
of that advance, landing in the last frames of the preceding note's written window.

**Two readings of that are possible and this entry does not choose between them.** Either the renderer
deliberately leads each note for legato, which is a musical choice and would mean the written score
understates the real phrasing; or the timing solver places onsets early by a fixed amount, which is a
timing defect measured against what the score asked for. **Deciding which requires listening**, because a
19 ms lead is inaudible on an isolated note and very audible as an audible legato portamento across a run
of notes. No one has listened to any of these renders.

**What this changes.** The 85 octave frames are now fully attributed: **34 are the analyser misreading a
note the audio contains correctly**, **50 are frames where the next note has already begun inside the
previous note's written window**, and **1 is uncharacterised**. None is the renderer singing a wrong pitch,
and the second population is a **timing property worth deciding on deliberately**, not noise.

**What this does not change, and is not claimed.** P0-08 stays **OPEN**. The advance has not been judged,
the 1 remaining frame is not characterised, nothing has been listened to, this is one singer family, and
`combinedModelHoldoutVerified` is still false. If the advance is judged a defect, the fix is in the timing
solver and the 50 frames become a true pass; if it is judged intentional, the written score is what needs
correcting. **That decision is a listener's and it has not been made.**

**Update 2026-10-04: the advance is in the timing model by design, but its magnitude does not match the
recipe.** The mechanism was located in source rather than inferred from audio.

`libs/seam-synthesis/src/timing_solver.cpp:108` sets `destinationStart = noteOn - vowelOffset`, where
`vowelOffset` is derived at line 96 from the unit's own markers, `unit->markers.vowelOnset -
unit->markers.audioOffset`, resampled to the output rate. **A unit is therefore placed so that its vowel
lands on the note's written start, which necessarily puts its preutterance before that start.** Leading the
note by the unit's own preutterance is the intended behaviour of this model, not an accident of the render.

**The measured advances are discrete and per-unit, which is what that model predicts.** Across the 33
measured onsets there are only ten distinct values, each recurring:

| Advance | ms | Notes |
| ---: | ---: | ---: |
| 544 | 11.33 | 6 |
| 1184 | 24.67 | 5 |
| 672 | 14.00 | 4 |
| 928 | 19.33 | 4 |
| 1056 | 22.00 | 4 |
| 4000 | 83.33 | 4 |
| 800 | 16.67 | 3 |
| 416 | 8.67 | 1 |
| 1312 | 27.33 | 1 |
| 1696 | 35.33 | 1 |

A single constant would have meant one shared offset; ten recurring values are consistent with different
units carrying different declared preutterances, which is exactly what `vowelOnset - audioOffset` is per
unit. **This supports the intended-placement reading over a timing fault.**

**And one thing does not line up, stated rather than smoothed over.** The recipe's per-phone bursts are
10 ms, with `burstMilliseconds: 10` on every plosive in the project recipe, while the measured advances
run 8.7 to 83.3 ms. The small advances near 8.7 ms are consistent with a 10 ms burst; the large ones are
not. Four onsets show the maximum 83.3 ms, which is also the ceiling of the 4000-sample search window and
therefore **may be a search artefact rather than a real onset**. This entry does not claim those four are
real advances.

**What this changes.** The 50 frames are explained by a documented, intentional placement rule with a
measured per-unit distribution, which is materially different from an unexplained early onset that could
have been a defect. The remaining uncertainty is narrower: whether the largest advances are real, and
whether preutterance of this length is musically intended.

**What this does not change, and is not claimed.** P0-08 stays **OPEN**. The 83.3 ms cases are not
explained and may be a measurement ceiling. Nobody has listened, so whether a per-unit lead of 8 to 35 ms is
desirable legato or an audible timing defect remains undecided. The 1 uncharacterised frame from the
previous accounting is still uncharacterised, and `combinedModelHoldoutVerified` is still false.

**Update 2026-10-04: the onset-advance figures above are withdrawn. The method failed a control.** The three
entries above report a 19.3 ms median advance and a per-unit distribution, and treat them as the explanation
for 50 of the 85 octave frames. **Those numbers came from a detector that does not recover known onsets,
and they are withdrawn.**

The suspicion that four onsets at exactly 83.3 ms were a search-window artefact was correct. Widening the
search from 4000 to 20000 samples moved **7 of 30 onsets**, four of them running to exactly 20000 samples,
or 417 ms, which is longer than most notes in these songs.

**The control that should have been run first.** On a synthetic signal where every onset is exact by
construction, the detector reported:

| Case | True advance | Reported |
| --- | ---: | ---: |
| distinct pitches, note 64 | 0 | **6160** |
| distinct pitches, note 60 | 0 | **not found** |
| shared pitch, first 64 | 0 | **6160** |
| shared pitch, second 64 | 0 | **26000** |
| ascending run, three notes | 0 | **not found, three times** |

A second attempt, with a shorter window and a sustained-match requirement, failed the same control on five
of seven known onsets.

**The cause is structural.** The analysis window is 2048 samples, so at a boundary its spectral peak
reports the louder of the two notes across 43 ms. Scanning backwards for the next note's pitch therefore
finds whichever note is loudest behind the boundary, not where the pitch changed.

**What is withdrawn and what stands.** Withdrawn: the 19.3 ms median, the ten-value distribution, "33 of 33
notes start early", and the claim that `timing_solver.cpp:108` explains those figures. **Still standing:**
85 octave frames; **34 are the analyser misreading a note the audio contains correctly**; and **50 frames
carry the next written note within 2 to 8 cents**. Both of those rest on comparing reported pitch with an
independent spectral reading of the same window, which does not depend on onset detection. **Now
unmeasured:** by how much the audio leads. The 50 frames prove the next note sounds inside the previous
note's written window; they do not quantify it, and three attempts to quantify it have now failed.

P0-08 remains **OPEN**. Nothing here changes the listenable state of the blocker, and the same discipline
applies going forward: an onset or boundary number needs a synthetic control whose answer is known before
its real-corpus figure is believed.

**Update 2026-10-04: the onset lead measured properly is under 17 ms, and it accounts for the 50 frames.**
The retraction above withdrew the onset figures for want of a control. Supplying one produces an answer.

Rendering a two-note project with a **long written gap** so the audio is silent across the boundary, and no
analysis window can contain two pitches, removes the loudness bias that made the spectral scan unsound.
Six cases spanning descending and ascending motion, a leap and a step, and two gap lengths:

| Case | Lead |
| --- | ---: |
| descend 67 to 60, 2 s gap | **16.67 ms** |
| descend 67 to 60, 0.5 s gap | **16.67 ms** |
| leap up 62 to 74, 1 s gap | **12.67 ms** |
| ascend 60 to 67, 2 s gap | **0.67 ms** |
| ascend 60 to 67, 0.5 s gap | **0.67 ms** |
| step up 64 to 67, 1 s gap | **0.67 ms** |

**Six of six start early, by 32 to 800 samples, median 320 or 6.67 ms.** The lead is real, small, and tracks
the **direction of motion**: descending and leaping intervals lead by 12.7 to 16.7 ms, ascending steps and
intervals by 0.67 ms.

**This agrees with the recipe and contradicts the withdrawn figures.** The recipe declares
`burstMilliseconds: 10` on every plosive, and a 10 ms preutterance budget lands squarely in the measured
band. The withdrawn scan reported a 19.3 ms median with outliers to 83 ms, which neither the recipe nor
this probe supports.

**And it accounts for the 50 octave frames.** A lead of up to 16.7 ms is about one analysis frame at 256
samples, so where consecutive notes meet within a frame or two, the closing frame of the first already
carries the second note's pitch. That is exactly the measured pattern: those frames sit at the extreme end
of a note and carry the **next** written note within 2 to 8 cents. **The preutterance is a documented,
bounded, intended placement and the 50 frames are its footprint.**

**What is settled and what is not.** The onset question is closed on measurement: the lead exists, is under
17 ms, depends on interval direction, agrees with the declared preutterance, and fully explains the 50
frames. Whether a lead of up to 16.7 ms is *musically* desirable is a listening question and is not
answered. The 1 uncharacterised frame remains uncharacterised, nothing has been listened to, this is one
singer family, and `combinedModelHoldoutVerified` is still false, so **P0-08 remains OPEN**.

### SEAM-BETA-P0-01: No rights-cleared, usable Beta Voicebank
**Status: OPEN.** No bank exists that this project may transform and redistribute; the dossier is a blocked contract template.

**Evidence**

- [docs/voicebank/beta-voicebank-01-dossier.json](docs/voicebank/beta-voicebank-01-dossier.json) is `BLOCKED`.
- Package hashes, delegated signing identity, trust epoch, source/derived assets, rights review, clean-install receipt, and reference-song receipt are empty or `NOT_RUN`.
- All four mandatory permissions are false: source use, transformation, redistribution, and end-user rendered audio.
- The dossier's `requiredUnits` array is empty.
- [docs/voicebank/BETA_JAPANESE_CVVC_INVENTORY.json](docs/voicebank/BETA_JAPANESE_CVVC_INVENTORY.json) is only an inventory artifact. Its checked-in snapshot contains 72 coverage keys and 144 takes but only the `k`, `s`, and `t` consonant families; it is not evidence of a complete, recorded, redistributable Japanese singing bank.
- [docs/voicebank/BETA_VOICEBANK_ACCEPTANCE.md](docs/voicebank/BETA_VOICEBANK_ACCEPTANCE.md) explicitly says the real `.seambank` and private rights records are external release inputs and that the checked-in dossier is only a blocked contract template.

**Impact**

External musicians cannot evaluate the product's core singing journey using a bank Project SEAM is demonstrably allowed to transform and redistribute.

**Closure criteria**

- One exact non-official `(voicebankId, version, contentSha256)` is frozen.
- Source and derived assets are hash-bound.
- Rights evidence explicitly covers source use, transformation, redistribution as a local singing voicebank, and commercial/non-commercial end-user renders in the intended territories.
- Required Japanese unit and pitch-layer coverage is complete.
- Marker, pitch-mark, loop, clipping, DC-offset, and retake QA pass.
- The bank passes four-renderer listening QA, hostile-package validation, signed package verification, clean installation, and a canonical reference-song render.

### SEAM-BETA-P0-02: Release candidate identity is stale and not immutable
**Status: OPEN.** Requires signed/archived candidate bytes; not closable by a source change.

**Evidence**

- Repository HEAD is `970d159d...`.
- The inspected `build-release-current` CMake cache and built `Info.plist` identify source commit `776d43e2...`, build ID `0.13.1-local`, and trust epoch `0`.
- The build system permits an incremental local build to retain an older configured identity.

**Impact**

Screenshots, test output, binaries, plug-ins, installers, SBOMs, and evidence records cannot be proven to describe one exact source candidate.

**Closure criteria**

- A clean candidate configure injects the exact source commit, non-local release ID, and valid trust epoch.
- Configuration fails closed when cached identity differs from the candidate source.
- App, CLAP, VST3, AUv2, installer, SBOM, and all evidence records report the same immutable candidate identity.

### SEAM-BETA-P0-03: No distributable, trusted installer candidate
**Status: OPEN.** Requires Developer ID signing and notarization credentials held outside this repository.

**Evidence**

- The current local macOS app is ad-hoc signed and fails strict distribution verification.
- A local Developer ID staging artifact remains unnotarized and Gatekeeper-rejected.
- No single authoritative notarized/stapled macOS installer is present.
- No signed Windows x64 installer and clean-install evidence are present.

**Impact**

The repository has build outputs, but not a release artifact a beta tester can safely install and identify.

**Closure criteria**

- macOS app, plug-ins, and installer are Developer ID signed with Hardened Runtime, notarized, stapled, and verified after download on a clean target account/machine.
- If Windows remains in beta scope, the x64 installer and binaries are Authenticode-signed, timestamped, clean-installed, and uninstall-tested.
- Installed bytes resolve to the exact candidate root from P0-02.

### SEAM-BETA-P0-04: Canonical standalone musician journey is 0/20 PASS
**Status: OPEN.** All 20 canonical rows are NOT_RUN with no evidence; needs installed candidate bytes and a person.

**Evidence**

- [docs/product/usable-alpha-acceptance.json](docs/product/usable-alpha-acceptance.json) has no completed physical run for the 20 canonical rows.
- The rows cover Finder launch, first-run flow, project creation, notes and lyrics, technical edits, production audio, transport, save/reopen, recovery, bank relink, master/stem export, external playback, and a 30-minute session.

**Impact**

Unit/controller success does not prove native dialogs, device negotiation, permissions, crash recovery, file associations, or exported audio work as one installed journey.

**Closure criteria**

- All 20 rows pass against the same signed-installed candidate.
- Evidence is timestamped, reviewer-attributed, hash-bound, and retained under the canonical evidence root.

### SEAM-BETA-P0-05: Target OS and DAW compatibility matrix is incomplete
**Status: OPEN.** Four of the nine required tuples are Windows and cannot be run on this macOS machine.

**Evidence**

- No completed checked-in records cover the required nine host tuples.
- The required set includes REAPER and Bitwig CLAP/VST3 on macOS and Windows, plus Logic Pro AUv2 on macOS.
- Internal hosts and generic plug-in validators do not demonstrate real DAW scanning, editor lifecycle, state recall, isolation, or offline bounce.

**Impact**

Host-specific crashes, state corruption, scan rejection, GUI lifecycle defects, and bounce differences can reach testers undetected.

**Closure criteria**

- Every in-scope OS/format/DAW tuple passes scan, instantiate, edit, save/reload, playback, offline bounce, close/reopen, and uninstall/rescan scenarios using installed candidate bytes.

### SEAM-BETA-P0-06: Physical audio, accessibility, soak, and human acceptance are unproven
**Status: OPEN.** Requires physical hardware, assistive technology, long sessions and real people.

**Evidence**

- VoiceOver, Accessibility Inspector, Narrator, UIA Verify, and Inspect runs are `NOT_RUN`.
- There is no current signed-installed evidence for physical listening, external-player comparison, audio/MIDI device loss and reconnect, sleep/wake, buffer-size or sample-rate changes, or actual MIDI hardware.
- There is no accepted 30-minute or 120-minute physical soak result.
- There is no completed external-musician cohort evidence.

**Impact**

The product can pass deterministic and visual checks while failing on real audio hardware, assistive technology, long sessions, or musician workflows.

**Closure criteria**

- Physical audio/MIDI scenarios and soak thresholds pass.
- VoiceOver and Narrator target runs pass on installed builds.
- Multiple external musicians complete the reference journey with Blocker/Critical count at zero.

### SEAM-BETA-P0-07: No governed release authorization or immutable archive
**Status: OPEN.** Requires independent release roles and an externally anchored archive.

**Evidence**

- The External Beta aggregate remains `BLOCKED` with no accepted evidence set.
- There is no signed release-role authorization, complete candidate-root provenance tree, governed raw evidence archive, or external immutable anchor.

**Impact**

Even if individual checks pass, the team cannot prove what was authorized, shipped, or later revoked.

**Closure criteria**

- Independent release roles approve the exact candidate.
- Named build/install/evidence transformations are hash-bound.
- The raw archive is immutable and externally anchored.
- Pause, revoke, rollback, and revalidation decisions are auditable.

## P1: Product and operating defects

### SEAM-BETA-P1-08: An admitted neural singer cannot render the same audio twice

**Evidence (2026-09-19, branch `codex/production-readiness-completion`)**

The first trained neural bundle this repository has produced was qualified on held-out material and
failed one automatic criterion. The dossier is
`/Users/lhs/seam-corpus-2026-09-19/dossier-1000.json` (verdict `FAILED`, failed `determinism`);
runs at `timesteps=1000` and at `timesteps=8` both fail identically, so this is not an artifact of a
small training configuration.

The exported acoustic graph generates its own diffusion noise rather than accepting it:
`onnx.load(.../bundle-1000/acoustic).graph.node` contains a `RandomNormalLike` node with inputs
`['diffusion//ConstantOfShape_output_0']`, and the graph's declared inputs are exactly
`['tokens','durations','f0','steps']` — there is no noise or seed input to bind. Four separate
`seam_neural_worker` processes given byte-identical SNW1 requests returned four different audio
digests (`d88d8bc8…`, `76c9a4ea…`, `3b9f03c0…`, `d6383f9d…`).

The generator is seeded per session rather than per request: within one ONNX Runtime session two
consecutive calls differ, while two freshly created sessions return the same first result. Because
`seam_neural_worker` completes exactly one request per process, per-request determinism is absent in
the shipped path even though a single-call session is reproducible.

**Impact**

The same project renders differently on every export for the neural backend, which contradicts the
product's own `reproducibility-tolerances/*/neural/pcm-error` contract. A creator cannot reproduce a
take, a reviewer cannot compare candidates on identical bytes, and no release evidence can be
re-derived from a frozen project.

**Not a defect in the candidate's sound**

Bundle admission, response binding, vocabulary coverage, finite non-silent audio and runtime budget
all pass. Pitch adherence is reported `UNRESOLVED` because the run failed before pitch was measured;
it is no longer reported as `PASS`, which it was until this register entry (`qualification.py`
returned early on determinism and left the criterion at its initial value).

**Closure criteria**

- The acoustic deployment graph exposes its initial diffusion noise as an explicit input, or takes a
  seed input, so an identical request is bit-identical across processes and repeated calls.
- A qualified held-out run reports `determinism PASS` on at least two independent requests per item.
- The reproducibility tolerance for the neural backend is measured against the frozen project and
  recorded, rather than asserted from the contract's presence.

**Closure (2026-09-19)**

Fixed at the graph-assembly step. ONNX stores a random op's seed in a float32 field, and ONNX Runtime
seeds the generator per session, so a fresh session repeats even though repeated calls within one
session do not. Because `seam_neural_worker` completes exactly one request per process, pinning the
seed on every random sampling node while the graph is merged makes an identical request bit-identical
in the shipped path, without changing the declared inputs the worker admits.

`tools/voice_model_training/onnx_acoustic.py::pin_sampling_seed` seeds each unseeded sampling node,
leaves a matching seed alone, refuses a conflicting one rather than rewriting it, and the export
fails when a graph has no sampling node to pin. The shipped constant is below `2**24` so its float32
round trip is exact; the first constant chosen was silently rounded by two, which the round-trip test
now pins.

Verified at the worker boundary: four separate processes given byte-identical requests returned
identical audio digests (`016c3808e8f90383` for `held-out-0`/`corpus-song-f` and `6107901c5cf8cfa6`
for `held-out-1`/`corpus-song-c`), and the re-qualified dossier
(`/Users/lhs/seam-corpus-2026-09-19/dossier-seeded.json`) reports `determinism PASS`.

**Status:** CLOSED. The same run now fails `pitch-adherence` instead: the rendered audio has zero
voiced coverage, so it does not sing the requested note at all. That is the honest result of three
training updates on 1303 frames and is tracked as remaining work, not as a determinism defect.

### SEAM-BETA-P1-01: Repeated support export can collide

`NativeEditorApp` always targets `Support/latest-diagnostic.zip`, while `SupportBundleService` rejects an existing destination. A second export can therefore fail unless the previous archive is removed.

**Required change:** use a timestamp/candidate-bound filename or an explicit preview plus atomic replace/save-as flow.

**Status: CLOSED — the description above is stale.** The fixed name is gone.
`SupportBundleService::exportPrepared` builds
`project-seam-support-<candidate>-<createdAt>-<sha256[:12]>.zip` (`libs/seam-authoring-runtime/src/support_bundle.cpp:476`) and,
on `ErrorCode::Conflict`, advances a bounded sequence suffix and retries up to 1000 times rather
than failing (`libs/seam-authoring-runtime/src/support_bundle.cpp:508`). Verified by running three consecutive exports of the
*same* prepared bundle against one directory: all three succeeded with distinct names
(`...aa68952ebcc7.zip`, `...aa68952ebcc7-2.zip`, `...aa68952ebcc7-3.zip`). This is the
"timestamp/candidate-bound filename" repair the entry asked for.

### SEAM-BETA-P1-02: User attachments are assigned an unsafe privacy class

Generated diagnostics are allowlisted and filtered, but consented attachments are copied after only basic regular-file, name, and size checks. The enclosing manifest can still label the entire bundle `ExportSafe` even when a project, lyric, raw log, secret, or audio file was attached.

**Required change:** separate generated-diagnostic and user-attachment privacy classes; preview every attachment and require explicit per-file consent without claiming the attachment itself is export-safe.

**Status: CLOSED — the description above is stale.** Generated diagnostics and user attachments now
carry different privacy classes: a consented attachment is `RestrictedSupportAttachment`, never
`ExportSafe`, and the enclosing manifest is stamped `RestrictedSupportData` whenever any
restricted attachment is present (`libs/seam-authoring-runtime/src/support_bundle.cpp:401`). Per-file consent is honoured twice
over: an unconsented attachment is previewed (hashed and listed) but not copied into the archive,
and the archived bytes are the ones bound at preview time, so a file changed after preview cannot
enter the bundle. The existing case "support bundle binds per-file consent and prepared attachment
bytes" asserts all of this, including that `"privacyClass":"ExportSafe"` appears nowhere in a
bundle carrying an attachment.

### SEAM-BETA-P1-03: Operational approvals are not cryptographically authoritative

The release operations path changes state from actor role strings, booleans, and approval lists. It checks candidate consistency but does not require a signed, hash-chained audit/approval record.

**Required change:** bind every approval to candidate root, previous decision digest, signer identity, signature, and append-only authority.

**Status: CLOSED — the description above is stale.** `tools/external_beta/operations.py` verifies
each quorum approval as an Ed25519 signature over the approval's own canonical payload, against a
role-bound trusted key whose `signerId` must equal the role's signer, and rejects duplicate signers
so one identity cannot fill two seats of a quorum (`tools/external_beta/operations.py:76`). PAUSE and REVOKE are
additionally refused unless signed by a role-bound trusted key (`tools/external_beta/operations.py:182` and
`tools/external_beta/operations.py:200`). The entry's premise — that state moved on role strings and booleans alone —
no longer holds.

### SEAM-BETA-P1-04: Pause and revoke are not proven to reach installed clients
**Status: OPEN.** The operations model and its signature checks exist, but no installed client is shown enforcing a propagated pause or revoke.

The operations model can represent `DISTRIBUTION_PAUSED` and `REVOKED`, but there is no end-to-end evidence that an installed updater/client consumes and enforces that authority.

**Required change:** implement and rehearse signed pause/revoke propagation, client enforcement, cached/offline behavior, and recovery.

### SEAM-BETA-P1-05: An invalid soak profile silently becomes a five-second smoke run

The Phase 12C soak runner selects 7,200 seconds only for the exact `full` profile; arbitrary or misspelled profile values fall through to a five-second run.

**Required change:** parse a closed enum, reject unknown profiles, bind the selected duration into the receipt, and require heartbeat/watchdog evidence for the full run.

**Status: the silent five-second fallthrough is CLOSED; the heartbeat half is not.**
`tools/external_beta/product_soak.py` no longer selects a duration by string match. It validates
`durationSeconds` against the closed set `{1800, 7200}`, binds each to exactly one phase
(`usable-alpha-30m` and `external-beta-120m` respectively), and rejects anything else
(`tools/external_beta/product_soak.py:207`–`212`). The sample series is separately required to be strictly increasing
and to reach the declared duration (`tools/external_beta/product_soak.py:261`), so a run cannot claim a two-hour soak
from a series that stops early. **Still open:** there is no independent heartbeat or watchdog
source in the record, so "the process was alive for the declared duration" is still inferred from
the collector's own samples rather than attested by something outside the measured process.

### SEAM-BETA-P1-06: Validators are ahead of evidence collectors

Several release tools validate supplied JSON records but do not drive the installed product, collect metric series, capture the environment, or preserve raw evidence themselves.

**Required change:** add candidate-bound collectors for installation, DAW hosts, accessibility, physical soak, and cohort sessions; validators should consume collector-produced records.

**Status: PARTIALLY closed.** Four candidate-bound collectors now exist and drive the product
rather than merely validating supplied JSON: `install_collector.py` (measures the machine it is
given and refuses to invent what it cannot observe), `host_collector.py` (reads the host and its
DAW, then runs the existing validators), `standalone_collector.py`, and
`soak_collector.py` (`collect_soak_samples`). Each stamps a `collector` block naming its own
tool and version into the record it produces. **Still open:** there is no accessibility collector
and no cohort-session collector, so VoiceOver/Inspect runs and external-musician sessions are
still hand-authored records rather than collected ones. The entry's five families are four done,
two missing — accessibility and cohort.

### SEAM-BETA-P1-07: No verified field support loop
**Status: OPEN, and the code half is now done.** The application can record an honest submission to a configured destination (`de76483e`), but no intake endpoint exists and no tester-to-triage exercise has been run.

The application can create a local support ZIP, but there is no verified intake destination, ticket handoff, acknowledgement, triage owner, escalation path, or pause/revoke service-level rehearsal.

**Required change:** run a complete tester-to-triage support exercise with privacy review and measured response ownership.

## P2: Structural and maintenance risks

### SEAM-BETA-P2-01: Native UI complexity is concentrated in very large files
**Status: PARTIALLY closed — the accessibility boundary is extracted; three named boundaries remain.**
`libs/seam-native-ui/src/editor_accessibility.cpp` now holds the six methods that make up the
accessibility cluster: `rebuildAccessibilityTree`, `dispatchAccessibility`,
`accessibilityFocusMoved`, `listEntryRefusal`, `dispatchAccessibilityAction` and
`setAccessibilityValue`. The move was a pure deletion from `editor_controller.cpp` (841 lines
removed, none added) and the moved block was verified byte-identical against `HEAD` before it was
committed, so nothing about the behaviour changed with the location. The two file-local helpers the
block used (`exportCancellable`, `technicalLaneForId`) each had exactly one use inside it and
moved with it.

**Still open.** `editor_controller.cpp` is 6632 lines, down from 7458. The plan names four
boundaries — input mode, selection/edit commands, accessibility dispatch, and overlay/panel
coordination — and exactly one has been taken. `editor_semantics.cpp` (1484 lines) is the other
half of accessibility and is untouched; input mode, selection/edit commands and overlay
coordination are all still in the controller. A boundary that had to drag the whole controller
behind it would not be a boundary, so these are taken one at a time and each is proved by the
tests that already exercise it.

**Update: the overlay/panel boundary is now taken too.** `editor_overlays.cpp` holds the
twenty-five methods that decide which overlay is open, what it shows and when it refreshes: the
vibrato inspector, style coverage sheet, Japanese reading review, dynamics inspector, find and
diagnostic-find reviews, the clear-dynamics / note-cleanup / clear-vibrato reviews, and the lyric
replacement and distribution reviews. The move was again a pure deletion (368 lines) plus one
added `#include`, and the block was diffed byte-for-byte. Breaking one of them (making the style
coverage sheet refuse to open) fails 7 tests, so the moved behaviour is covered, not merely
relocated.

One helper had to be shared rather than copied. `externalTextTarget` — the sentinel lyric id a
free-text field commits to — was a function in the controller's anonymous namespace, and after the
split three translation units needed it. Copying it three times would have been three chances for
the sentinel to drift and quietly stop matching, so it now lives once in
`include/seam/native_ui/editor_text_target.hpp`.

**Update: selection/edit commands are now extracted too — three of four boundaries taken.**
`editor_edit_commands.cpp` holds the fifty-two methods that move the selection (track, adjacent
track, region), the structural edits (add / remove / rename / reorder a track or region, split,
duplicate, copy to track, delete, move, resize), the per-track and per-note parameter commands (mix,
route, voicebank, seam and unit overrides, quantize, slur, melisma, lyric distribution), and the
selection-to-host reconciliation that follows an edit.

This extraction needed two shared helpers rather than none, which is the honest difference from the
first two. `kHostSelectionTries` moved with its only caller. `makeNotice` and
`kSelectionSyncFailedCode` were needed by three translation units at once and now live in
`include/seam/native_ui/editor_notices.hpp`; because that made the builder shared it was renamed
`makeEditorNotice` to say what it is. The moved block is byte-identical to the original except for
that one rename, which was verified by diffing against `HEAD` and undoing the rename in the
comparison — so the only behavioural question is whether the shared builder behaves as the local one
did, and it is the same function body with `inline` linkage. Breaking `deleteSelectedRegion`
fails 5 of the 1466.

**Update: input mode is extracted — all four named boundaries are now taken.**
`editor_text_input.cpp` holds the seven methods that carry text into the project: beginning a
phone-hint or lyric edit, the composition updates and commits, moving between lyric notes, and
cancelling a composition back to whatever opened it. Pure deletion again (316 lines, none added)
and byte-identical to the original. Making `beginLyricEdit` refuse fails 12 of the 1466, the
heaviest coverage of the four extractions, which is what one expects of the path a creator types
through.

`keyDown` itself did **not** move. It is input too, but it is input to the *editor* rather than
input to a *field*, and it reaches into so much of the controller that moving it would have meant
moving the controller. Pointer gestures, scroll and zoom, and the audio-settings commands stayed for
the same reason. That is the honest edge of this boundary, and it is why the controller is 4943
lines rather than something much smaller.

**Still open.** `editor_controller.cpp` is 4943 lines, down from 7458 — a 34% reduction with all
four named boundaries extracted, each proved by the tests that already exercised it. What remains
is not a missing boundary but two oversized methods: `replacementReviewView` is 434 lines and builds
the replacement, distribution, style coverage, vibrato-clear, note-cleanup, clear-dynamics and
Japanese-reading views in one body, and `keyDown` is about 480 lines on its own. Splitting either
is a change rather than a move and is separate work.

**Update: `replacementReviewView` is split — it is now a 72-line dispatcher.**
`editor_review_views.cpp` holds the eight per-mode builders: the Japanese reading review, the style
coverage sheet, the dynamics draft inspector, the vibrato draft inspector, the find review, the
clear-dynamics review, the note-cleanup review and the clear-vibrato review. Each was already a
self-contained branch that ended in a return; each is now its own method taking the view the
dispatcher has already started. The replacement review itself and its detail page stayed in the
dispatcher, because both read the asynchronous job handle it owns.

**This one was a change rather than a move, and it showed.** The first attempt silently dropped two
lines that set the "Preparing replacement review..." status, and five existing cases caught it —
which is the argument for not treating a green build as evidence for a change. Every original
statement was then checked to still be present across the two files (433 of 433), and the tests went
from 1466 to 1467 with a new case pinning that each mode still reaches its own builder.

**One claim was withdrawn.** The dispatcher order looked load-bearing — the first matching mode flag
wins — and a comment said so. Reordering the dispatch fails **no** test, because each mode's
`open()` clears the others' flags, so the branches are exclusive in practice and the order is not
observable. The order is still preserved, because the flags are members rather than locals and the
exclusivity is a property of the callers rather than of this function; that is now what the comment
says, instead of a claim the mutation had just disproved. The new case pins what is genuinely
pinnable — that each mode reaches its own builder — and says plainly that it does not pin the order.

**Update: `keyDown` is split too — the controller is 4117 lines.**
`editor_keyboard.cpp` holds the two halves. `keyDownForOpenSurface` (340 lines) asks what surface
has focus — the replacement panel, the time map, the phoneme review, the sample microscope, the
recovery sheet, an active composition, the voicebank browser, the audio settings, the expression lane
— and each returns as soon as it handles a key, because an open surface owns the keyboard.
`keyDownGlobalShortcut` (144 lines) asks what the key means to the editor: undo, redo, play, save,
nudge, delete. `keyDown` is now a 12-line dispatcher.

The one design decision worth naming: the modal half returns **nothing** when no surface took the
key, rather than a failure. A failure would reach the host as a refused key press, so every key the
editor does not bind would read as an error instead of as unhandled. The first attempt used a
sentinel failure and was replaced before it was ever run.

Unlike the replacement-view split, **this one passed on the first run** — 466 of 466 original
statements verified present across the three files before building, and 1467 of 1467 after. The
mutation evidence is the stronger part: making the modal half claim every key fails 18 of the 1467,
and making undo/redo refuse fails 3, so both halves and the boundary between them are load-bearing.

**Still open.** Nothing structural remains in this file: it is 4117 lines with no method over 480.
The entry's own claim that the concentration was the risk is now addressed; what remains is that the
remaining 4117 lines are still one file, which is a smaller version of the same concern rather than
a resolved one.

The editor controller, native UI test file, editor scene, AppKit window implementation, and application controller each concentrate several unrelated state machines. This raises merge conflict, regression, and field-fix cost.

**Required change:** before broad beta iteration, extract only stable boundaries: input mode, selection/edit commands, accessibility dispatch, and overlay/panel coordination. Preserve behavior with state-machine tests and visual evidence.

### SEAM-BETA-P2-02: Cross-version migration is not verified as an installed product journey
**Status: PARTIALLY closed — the project family now has a real N-to-N+1 journey; the other six
families still use synthetic fixtures.**
`predecessor_release.py` requires seven state families (project, media, settings, autosave,
catalog, clap, host) and its own tests build all seven synthetically in a temporary directory, so
they prove the validator's shape and nothing about migration. The **project** family is now covered
by a genuine journey: a real schema-1 document (the oldest schema this build reads) is written to
disk, opened through `StandaloneApplicationController`'s real OpenProject command, checked for
current semantics rather than mere decoding — a schema-1 document predates the bounce-timing
choice and must read as `FixedAudio`, not inherit this build's default — then saved and reopened,
with the saved bytes asserted to be schema 20, to differ from the predecessor's digest, and to
retain the legacy Japanese lyric.

That journey is load-bearing: refusing legacy schemas in `project_json.cpp` fails it at the open
itself, independently of the three schema tests that also fail.

**Still open.** The remaining six families are covered only by synthetic fixtures, and the project
journey runs in-process rather than from installed candidate bytes. `editor_controller.cpp` is
also still 7458 lines (P2-01), which is the other reason broad beta iteration is risky. Nothing here
produces a signed predecessor record, because that needs a signed installed candidate.

Schema migrations and future-version rejection exist, but there is no predecessor-to-current fixture that jointly verifies projects, autosaves, media, voicebank catalog, CLAP state, plug-in rescan, and updater behavior.

**Required change:** create immutable N-to-N+1 installed fixtures and test update, rollback, reopen, rescan, and data preservation.

### SEAM-BETA-P2-03: Status documentation can overstate readiness

Several documents mix contract validity, source implementation, target-machine pass, and beta readiness. Strings such as `*_CONTRACT=PASS` can be mistaken for product acceptance, while older readiness percentages use a different denominator.

**Required change:** maintain one current release status page with four separate states: `CONTRACT_VALID`, `IMPLEMENTED`, `TARGET_PASS`, and `BETA_READY`.

**Status: the required page now exists; the register is the thing that was overstated.**
`tools/external_beta/release_status.py` reports the four states in order, weakest first, and
every row names the evidence for its own state plus what blocks it from rising. The page reads its
numbers from the documents it summarises rather than restating them, so it cannot drift: the
canonical journey row is computed from `docs/product/usable-alpha-acceptance.json`, which records
20 requirements, all `NOT_RUN`, none carrying evidence, gate `BLOCKED` — so the page says 0 of 20.
Current counts are 4 `CONTRACT_VALID`, 3 `IMPLEMENTED`, 0 `TARGET_PASS`, 0 `BETA_READY`, and
`betaReady` is `false`.

Two design points are what keep it honest rather than merely present. A row may not be constructed
without evidence, and one below its ceiling may not be constructed without naming its blocker, so
a claim cannot quietly read as further along than it is. And a citation that names a file is
checked to exist — the first draft of this page cited `tools/external_beta/host_matrix.py`, which
does not exist, and the citation-existence case caught it.

Mutation-checked, including a defect the mutation found. Raising any row to `BETA_READY`, and
removing the blocker requirement, both fail. A third mutation exposed a real weakness in the first
version of the journey case: it compared the page against a second implementation of the same
reading rule, so hardcoding `20 of 20` satisfied both sides. Rewritten to pin the matrix's own
recorded state, it then failed on a substring match, because `"20 of 20"` contains `"0 of 20"` —
the case passed a page claiming every row was complete while the matrix records none. It now
matches the phrase with a digit boundary on both sides.

**What this does not do.** It reports status; it does not change any gate, close any P0, or create
evidence. Nothing here is `TARGET_PASS` or `BETA_READY`, and no row may claim those states while
the page reports `betaReady: false`. The four P0 gates that need physical runs, installed
candidate bytes, real reviewers, or rights-cleared assets are exactly as blocked as before.

## Beta Voicebank sourcing decision

### Decision

It is technically feasible to create the non-official Beta Voicebank by generating and processing synthetic speech, but a generic “free for commercial use” label is not enough for Project SEAM's distribution model.

The recommended order is:

1. **Best legal/technical fit:** a project-owned procedural synthetic voicebank generated from DSP primitives, with no borrowed human voice identity.
2. **Best quality-to-cost fit:** record the maintainer's or a consenting performer's voice under an explicit release, then apply documented pitch/formant/timbre processing to create a fictional non-official beta identity.
3. **Conditional experiment:** use an offline TTS model only after the engine, model weights, voice, training-data provenance, generated-output, modification, sample-library redistribution, and end-user render terms all pass review.
4. **Do not use for the distributable bank without written clearance:** free-tier SaaS TTS output, platform/system voices, celebrity-like voices, cloned voices, or corpus clips whose copyright license does not separately settle voice/personality/privacy rights.

### Why the procedural option is especially suitable here

- Project SEAM already has a deterministic Japanese inventory generator and a signed data-only `.seambank` format.
- A procedural generator can produce the exact vowel, CV, VC, VV, breath, glottal, release, and special units at the required pitch layers instead of cutting arbitrary prose.
- Generator version, parameters, source code, unit WAV hashes, markers, and package identity can be reproduced and audited.
- The result can be intentionally robotic and fictional. Beta needs a reliable non-official evaluation instrument; it does not need to claim final commercial character quality.
- The remaining risk becomes audible quality and phonetic coverage, which can be measured, rather than unclear rights in a human likeness.

### Mandatory license checklist for any third-party TTS path

- [ ] TTS engine code license is compatible with how the generator is run and distributed.
- [ ] Model-weight license permits the intended commercial use.
- [ ] The specific voice/model card is reviewed; a repository-level license is not substituted for a per-voice license.
- [ ] Training-data provenance and performer consent are documented sufficiently for the release territory.
- [ ] Generated audio may be used commercially.
- [ ] Generated audio may be modified and transformed.
- [ ] Processed unit WAVs may be redistributed as a reusable sample/singing-voicebank product, not only embedded in a finished video or song.
- [ ] End users may commercially release songs rendered from the bank.
- [ ] Attribution, share-alike, notice, source-offer, and downstream-license duties are compatible with `.seambank` distribution.
- [ ] The voice is not confusingly identifiable as, or marketed as, an unconsenting real person.
- [ ] A reviewer signs the redacted rights approval required by the Beta Voicebank dossier.

### Proposed evidence-bound spike

The fastest responsible experiment is a **procedural Beta Voicebank spike**, not immediate adoption of a third-party TTS service:

1. Generate a deliberately small but musically usable Japanese subset at MIDI 60 and 72 from owned DSP code.
2. Produce source WAVs deterministically and retain generator version, parameters, and hashes.
3. Generate or hand-correct markers and pitch marks, then run existing production validators.
4. Package and sign a non-official, characterless `.seambank`.
5. Clean-install it and render the canonical reference phrase/song in all four production renderers.
6. Conduct blind listening for intelligibility, joins, sustain stability, pitch accuracy, and fatigue.
7. Expand to the full accepted inventory only if the spike clears those thresholds.

This spike can establish technical viability without weakening the existing rights gate. It must not change the dossier to `PASS` until the actual package, complete inventory, listening results, clean install, and rights approval all exist.

## Open-beta GO rule

Open beta remains `NO-GO` until every P0 item is closed. A narrowly labeled macOS private alpha may be considered earlier, but only with an exact rights-cleared bank, immutable signed candidate, clean install, completed core musician journey, privacy-safe support path, and explicit scope exclusions.
