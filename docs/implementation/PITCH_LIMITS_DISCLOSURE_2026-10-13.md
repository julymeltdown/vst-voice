# Pitch limits and complete remeasurement disclosure

Date: 2026-10-13. Base: `e0fcfd41ad3eb4748609b2633ba9a241bcdcb4c0`. This follow-up responds to the numerical review of `5906c9b8`: improve the contract's wording and controls without changing the estimator or its identity. [Evidence index](PITCH_LIMITS_DISCLOSURE_2026-10-13.evidence.json).

The previous report disclosed 1,088 changed F0 values but omitted the material loss of previously voiced estimates. Recomputing every paired source frame across the ten retained original VocalSet tracks confirms:

| Old-to-new category | Frames |
| --- | ---: |
| Off-range voiced to unvoiced | 106 |
| Off-range voiced to another voiced value | 0 |
| In-range voiced to unvoiced | 976 |
| In-range voiced to changed in-range voiced value | 6 |
| In-range voiced value unchanged | 16,092 |
| Previously unvoiced F0 value changed | 0 |

The 976 lost in-range frames occur in 176 runs, with a maximum run of 23 frames; 946 had old estimates between 1,100 and 1,200 Hz. Confidence stays exactly identical in 99 of the 106 off-range rejections. The counts compare estimator outputs and do not establish the true fundamental or true voicing of the recordings. The reviewer traced short-lag ripple selection in examples; we do not generalize that diagnostic into complete musical qualification. Changed frames also change training labels and inspector medians, which is why the separately landed [inspector-3/history unit](TAKE_INSPECTOR_VERSION_HISTORY_2026-10-13.md) is required.

## Boundary contract and controls

The [pitch code comment](../../libs/seam-voicebank/src/pitch.cpp) now says exactly what the final guard proves: an interpolated value outside the configured interval is rejected while its correlation confidence is retained. An in-range value may still be an alias, and a boundary peak lacking an outer neighbor can land on the limit. No algorithm, threshold, feature version or inspector version changed in this unit.

The [new native control](../../tests/test_pitch_analysis_bounds.cpp) uses the default 60–1,200 Hz interval and complete frames of 1201/1225 Hz pure tones at 44.1 kHz, with both Direct and FFT correlation. Every tested frame is unvoiced with zero F0 despite correlation confidence above 0.9. Existing 1199 Hz positive controls remain. A full dependent Release rebuild preceded the focused pitch run: **10 cases, one CTest entry, all pass**, 0.59 seconds. This is a focused run, not a fresh full-suite claim.

Ten separate native CLI probes use 0.2-second PCM24 synthetic tones, FFT correlation and the CLI's full hop grid, including its tail frames. Selected observations are:

| Rate | True tone | Voiced / total frames | Returned voiced frequency |
| --- | ---: | ---: | --- |
| 44.1 kHz | 59 Hz | 28 / 35 | Exactly 60 Hz |
| 48 kHz | 59 Hz | 31 / 38 | Exactly 60 Hz |
| 44.1 kHz | 1201 Hz | 0 / 35 | None |
| 44.1 kHz | 1225 Hz | 0 / 35 | None |
| 48 kHz | 1201 Hz | 0 / 38 | None |
| 48 kHz | 1225 Hz | 37 / 38 | About 612.502–612.509 Hz |
| 48 kHz | 1200 Hz | 23 / 38 | About 1199.976–1199.999 Hz |

The full-hop CLI results are not the same frame configuration as the complete-frame native controls. Tail/padding and exact-limit interpolation can affect rejection. The 48 kHz 1225 Hz alias and lower-bound behavior remain known estimator limits, not passing ground-truth measurements. General octave-error repair, exact-limit stability and weak-fundamental/strong-harmonic coverage remain separate work requiring a broader reference corpus. The range guarantee alone is insufficient to admit a pitch layer, training track or production sample.

All original audio and prior numerical outputs remain byte-identical; the new manifest binds both old/new tracks, probes, sources, binary and run receipts. The parent numerical scope has independent technical APPROVE; review of this disclosure/control unit is pending. No source, performer, real actor, listening or release acceptance was added. Canonical Alpha remains 0/20 and External Beta remains BLOCKED; the full goal stays active.
