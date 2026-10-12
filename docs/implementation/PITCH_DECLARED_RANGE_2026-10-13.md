# Pitch estimates respect the declared frequency interval

Date: 2026-10-13, Asia/Seoul. Baseline HEAD: `73b309f0083b2d481282dede1c683bf9e5c1f884`. Fetch and fast-forward pull preceded this unit; existing stash and source evidence were preserved. The [evidence index](PITCH_DECLARED_RANGE_2026-10-13.evidence.json) binds the retained baseline, full build, tests, exact binaries, source snapshots and fresh original-recording measurements. This first candidate is superseded by the [boundary-neighborhood follow-up](PITCH_UPPER_NEIGHBORHOOD_2026-10-13.md) after a newly reproduced octave regression; independent review remains PENDING. Its build/measurements below are retained historical execution, not the current analyzer.

## Defect and repair

The [source supplement review](FEMALE_ORIGINAL_SOURCE_SUPPLEMENT_2026-10-13.md) found 106 voiced frames outside the native extractor's declared 60–1,200 Hz interval across ten unchanged female f1 recordings. At 44.1 kHz, rounding the shortest lag down admitted lag 36 and therefore 1,225 Hz. Parabolic lag refinement could also cross either configured frequency bound. High correlation alone did not establish an in-range estimate.

`libs/seam-voicebank/src/pitch.cpp:57` now rounds the shortest search lag up. The longest search lag remains unchanged. After interpolation, `pitch.cpp:182` requires a finite frequency inside both inclusive configured bounds before setting `voiced`. Rejected proposals produce `voiced=false, f0Hz=0` and retain their measured correlation confidence. The implementation does not clamp a rejected pitch to an invented boundary value. It does not establish ground-truth pitch accuracy, eliminate octave errors or qualify source material.

Two new native tests failed against the old implementation before this repair: the upper-bound assertion and a high-confidence interpolated estimate below the configured minimum. The six existing cases passed. The retained baseline CTest exit is 8; its executable and source snapshots are preserved. Both regression cases now pass with Direct and FFT correlation. The multi-rate test covers 8, 22.05, 44.1, 48 and 96 kHz, checks both boundaries, and requires an in-range integer-period control to remain voiced. The interpolation test requires high-confidence rejected proposals to be unvoiced with zero F0.

## Analysis identity and consumer compatibility

Stored analysis identity must change when analyser answers change. `libs/seam-voicebank/include/seam/voicebank/acoustic_analysis.hpp` advances the acoustic algorithm revision from `2` to `3`; existing current-analysis validation refuses older derivatives. The regression explicitly covers a previously current revision `2` and an unknown revision `0`. Historical bytes are preserved; they are not relabelled as newly measured data.

The CLI's native feature algorithm advances from `fft-autocorrelation-v1` to `fft-autocorrelation-v2`, using `kPitchAnalysisAlgorithm` in `pitch.hpp`. Python's `native_features.py` defines the matching current identity. `features.py` and `pitch_comparison.py` reject old identity, out-of-range voiced F0, nonzero unvoiced F0 and voiced confidence below the extractor threshold. They allow a high-confidence unvoiced zero, because a proposal can fail the frequency interval despite strong correlation. Such frames stay unmeasurable in comparisons; matching two rejected tracks cannot manufacture a pitch match. Applying features continues to clear the label's prior review revision.

Only current unit fixtures use the new identity. Captured recordings, prior outputs, manifests, cuts, worksheet decisions and the frozen 95-file owner kit remain unchanged. Older bank analyses need the existing explicit reinspection/current-review procedure; this unit grants no approval or automatic migration.

## Executed validation

The full dependent Release rebuild (`cmake --build build/release -j6`) finished with exit 0 before CTest. Its full log and tested binaries are retained. Build elapsed time was not recorded; no duration is inferred.

| Check | Result |
| --- | --- |
| Focused CTest | 7/7 entries pass, 24.50 seconds |
| Native cases inside six entries | 178 pass: synthesis 57, sample-review CLI 26, pitch 8, take inspection 6, production review 22, production 59 |
| Native training-pitch CLI entry | Pass; separately counted as an integration entry, not another native case count |
| Four changed Python modules | 23 cases reported, 22 pass and one optional native control skipped, 12.053 seconds |
| Previously skipped actual native comparison control, explicitly enabled separately | 1 pass, 0.234 seconds; same/reversed/octave/delayed/unvoiced waveform controls |
| Exact ten acquired original WAVs | 10 reader + 10 extractor invocations, all exits 0; original SHA/frame identities verified |
| Evidence reproduction | 90 new-unit artifacts, 2 preserved bindings and original supplement's 124 + 8 hashes pass |

The native CLI integration and separately enabled waveform control use the rebuilt executable. The remaining Python fixture tests are unit evidence. Neither group is human listening, phonetic or production acceptance.

## Fresh measurements and remaining source limitation

The new CLI SHA is `f7ad9d3a37bd13b8fa26bb0f9b03ceb233f04769ea548d233736bec71e4b87fb`. The original supplement's CLI SHA `ce8810dd…` and `fft-autocorrelation-v1` results remain retained under their original identity.

| Original group | Old out-of-range voiced frames | New out-of-range voiced frames |
| --- | ---: | ---: |
| Five straight scales | 39 | 0 |
| Five straight arpeggios | 67 | 0 |
| Total | 106 | 0 |

The changed search can alter other estimates as well: 742 F0 frame values differ across the ten tracks. All new voiced values are finite and inside the declared interval; unvoiced F0 is zero. Every output remains `trainingAdmitted=false, releaseEligible=false`.

The descriptive pre-screen was recomputed from the new results. Its longest note-66 run is still 10 consecutive hop observations, **58.0498866 ms**, below 150 ms. The 2,048-sample windows overlap; this is an observation-run duration, not an independently established usable sustain. No source layer, Japanese vowel mapping or new cut is qualified.

The source documentation follow-up received technical APPROVE at `73b309f0`. Its optional wording finding is corrected: none of the 106 old off-range frames passes the **combined** confidence >=0.9 and 35-cent test. Nine pass the cents test only, eight pass confidence only, and 89 fail both. The original acquisition and pre-screen bytes remain unchanged.

## Acceptance and next development

This closes the reproduced numerical defect locally, pending independent implementation review. Source admission still needs genuine actor identities, rights/performer and public-license disposition, qualified per-material phonetic/pitch/listening judgments, and the owner's failed-QC policy. The original 15 sustain cuts, null worksheet and frozen kit remain unchanged.

Canonical Usable Alpha remains **0/20 recorded PASS**, all NOT_RUN with empty evidence. External Beta remains BLOCKED with zero evidence for nine required categories. This is not an overall implementation percentage. AE1's original procedural voice, full learned-resource requirements, three languages, real installed/plugin/platform journeys, human qualification, release/signing evidence and U45 reconciliation retain their original scope. `.github` is untouched, README Windows TODO and the pre-existing stash are preserved; the full development goal remains ACTIVE.
