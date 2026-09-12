# Voicebank coverage correctness before Style/Coverage UI

Reviewing the data source for U25's Style/Coverage sheet exposed false-positive structural coverage. The analyzer previously marked the union of every matching unit span. For tokens `k a t` and units `k a` plus `a t`, that union covers all three tokens even though no non-overlapping unit sequence covers the phrase. The unit selector advances by each candidate's token count, so overlap-only coverage must not be presented as a complete sequence.

## Changes

`VoicebankCoverageAnalyzer::analyzeRegion` now computes maximum non-overlapping covered-phoneme count with dynamic programming. Gaps remain available for partial-coverage diagnostics. Equal scores prefer a longer unit at the earliest position; the result is deterministic for a fixed manifest/input. Adding a `k` unit to the example finds the complete alternative `k` + `a t` instead of greedily choosing `k a` and leaving a gap.

- `CoverageIssueKind::SequenceConflict` and `sequenceConflictCount` distinguish an overlapping-only candidate from an absent recording.
- Every note touched by a candidate span must exist and satisfy the caller's pitch-distance limit. Checking only the first note could incorrectly cover later high-pitch or orphan phonemes.
- Disabled-unit and unsupported-style diagnostics propagate across the matching span, including interior phones. They are no longer mislabeled as missing just because no unit starts at that interior token.
- Note pitches are indexed once. Auxiliary coverage/decision/witness storage is linear in token count; all candidate combinations are not retained.
- Each issue can retain one relevant witness unit ID. This is diagnostic evidence, not an exhaustive list of alternatives or every possible cause.

Inventory presence flags, such as `hasBreath`, retain their existing meaning and may include disabled assets. They must not be interpreted as enabled renderer capabilities by the future sheet.

## Boundaries

This is structural coverage under the requested style and pitch-distance policy. It does not validate source files, licensing/trust, forced unit overrides, phoneme-alignment requirements, renderer compatibility, timing or acoustic quality. `complete()` is not Beta GO approval, and an empty phoneme query is not evidence that a bank can sing. The Style/Coverage sheet itself remains to be implemented; this checkpoint repairs its underlying evidence rather than inventing capability claims.

## Verification

Two new regression cases cover overlap-only false completeness, a non-greedy complete alternative, all-note pitch checking and orphan-span rejection. The existing missing/disabled/style/pitch test now asserts exact per-phoneme counts for all four categories rather than lower bounds.

Release native app/core/U3 and Debug U3 builds pass. All 632 Release core cases pass (14.38 s); 16 U3 cases pass Release/Debug (0.51/0.61 s). `git diff --check` passes. This does not add or reaccept a roadmap unit. Changes are local/uncommitted; full Beta GO remains incomplete.
