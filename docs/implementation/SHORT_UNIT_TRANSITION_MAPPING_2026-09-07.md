# Short-unit transition mapping

Status: classical and Raw short-transition integration implemented. The unchanged diagnostic corpus workflow passes in both modes; acoustic qualification remains open.

## Raw integration follow-up

RawRenderParameters now carries an optional validated source map. The renderer checks exact source/output endpoints and vowel origin, derives transition/release extents from that map and rejects a map leaving no sustain interval. Only the transient sections follow the map's source positions. Sustain retains the existing fixed pitch-ratio loop or compiled pitch stepping, including loop-print behavior and gain; no classical renderer fallback is substituted. Release/transient resampling can change their local pitch and the diagnostic states that limitation explicitly.

The concatenative short-unit path now passes the same map to Raw and labels its actual renderer truthfully. Raw renderer revision increases 8→9 for cache provenance. The fixture regression covers all four renderers without fallback. A dedicated Raw test compares actual sustain sample first differences with the expected loop phase increments and -6 dB gain at root pitch and one octave up; it also rejects conflicting extent/vowel origin. First differences remove the existing DC correction from the comparison without relaxing the phase/gain assertion.

The previously failing `seam_singing_quality_workflow` now passes unchanged (20.74 s), including both projects in bank and Raw modes. Retained Raw unequal-rests output at `/tmp/seam-broad-quality.rP8rbv/short-raw-fixed` contains 438000 frames (9.125 s), peak 0.4933145 and zero clipped samples. Both short-placement diagnostics identify Raw with `used_fallback=false`. This is diagnostic-corpus success, not listening approval or a production singer release.

Final focused verification: 538 Release core cases pass (11.86 s); all 18 timing cases pass in Release/Debug (0.99/0.40 s). Strict builds and diff checks pass.

## Classical integration follow-up

TimingSolver has an explicit opt-in for short-transition placement. Default/legacy callers still reject; opt-in requires a simple CV/sustain unit, one nucleus and at least three voiced target frames. The production pipeline opts in and tags these placements instead of extending notes. Legacy RawPhraseRenderer explicitly rejects a tagged placement, preventing silent truncation by an unaware consumer.

The concatenative renderer compiles the marker map from decoded source extent and rate, supplies it to Classic PSOLA, Spectral Classic or Stretch with the captured compiled performance, and disables Raw fallback for this path. Vowel alignment is exact; diagnostics disclose marker compression and pending acoustic qualification. Existing explicit-interior/alignment restrictions remain. Timing solver revision increases 10→11, changing render cache provenance.

The new regression verifies legacy rejection, opt-in placement and finite/nonzero mapped PCM in all three classical renderers, without fallback, with exact vowel onset. Its first draft lacked the required compiled-performance input; adding the production-equivalent input made the mapped-renderer assertions valid rather than relaxing renderer validation.

The unchanged retained unequal-rests corpus project now renders in bank mode at `/tmp/seam-broad-quality.rP8rbv/short-bank-fixed`: 438000 frames, 9.125 s, peak 0.4924655, no clipped samples. Diagnostics identify Classic PSOLA and no fallback for compressed placements. The corresponding Raw run still exits 4 explicitly: `Short-transition Raw mapping is not implemented`. No all-mode corpus PASS is claimed.

Current verification: Release core passes 538 cases (11.88 s), and all 17 focused timing cases pass in Release/Debug (0.48/0.41 s). Strict builds and diff checks pass.

## Measured cause

The retained unequal-rests case starts with a 120-tick (62.5 ms at 120 BPM) note. Its selected `demo.ja.g4.k-o.01` unit has vowel onset 2200 and stable start 5292 at 44100 Hz: about 70.11 ms of post-vowel transition before the stable region. The timing solver rejects the target rather than truncating the transition. Changing the corpus or removing that guard would not implement short-note support.

## New primitive

`compileShortUnitMarkerMap` constructs a bounded, strictly monotonic source/target map from verified-in-bounds unit markers. It is restricted to simple two-phone CV or one-phone sustain units; callers must establish a single target nucleus. It is not a substitute for multi-phone alignment evidence.

The map preserves audio start, target vowel onset and exact output end. The post-vowel transition and release each receive at most one third of the voiced target span (and no more than their rate-scaled source duration), retaining at least one sustain frame between them. Source stable/release markers must be ordered and distinct; absent/degenerate release landmarks reject. Source/output rates, decoded source extent and target arithmetic are bounded before constructing or evaluating knots. No voiced/unvoiced annotations are invented.

The map can be consumed by existing classical source-map renderers. Applying the generic linear source-map evaluator is a deterministic resampling baseline, not proof of pitch preservation or intelligibility. Production integration still needs renderer-specific admission, truthful diagnostics/provenance and audio-quality tests. The timing guard remains unchanged until those consumers exist.

## Verification

Focused tests exercise 44100/48000/96000 Hz, exact vowel-anchor sampling, ordered transition/sustain/release landmarks, exact output lengths and deterministic replay. They reject fewer than three voiced frames, contradictory zero-preutterance mapping, invalid rates, out-of-source markers and unsupported VCV units. Strict Debug/Release builds and all 16 timing cases pass (0.47/0.64 s); diff checks pass.

## Next work

Rerun the broad rebuilt regression inventory, add stronger pitch/transition/acoustic measurements and lifecycle regressions, and qualify the compression perceptually. Keep explicit interior-phone edits and multi-nucleus units dependent on real alignment. Acoustic qualification remains required beyond finite PCM or green tests.
