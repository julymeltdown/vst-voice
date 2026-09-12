# Procedural in-note onset policy

Update: [policy revision 2](VOICED_ONSET_TIMING_2026-09-09.md) also allocates
single voiced onsets. The original revision-1 evidence below is historical;
the duration and manual-ownership rules remain unchanged.

Status: implemented engineering default, not pronunciation/listening qualification.

## Behavior

The shared timing compiler now accepts an explicit `PhonemeTimingPolicy`. Existing callers default to `SourceDependent`, preserving the previous interpretation. Production procedural snapshots select `ProceduralInNote`; this choice does not apply a new policy to sample-bank rendering.

For a wholly untimed syllable with exactly one unvoiced onset before a voiced nucleus, the policy starts the onset at the existing equal-elapsed-time syllable boundary and delays the nucleus by `max(1 frame, min(60 ms, floor(syllableFrames / 4)))`. A syllable must have at least two frames. This keeps a bounded in-note onset interval and normally leaves at least three quarters of the syllable for its vowel. The source recipe still must supply a supported frication binding; compiler allocation does not make plosives or arbitrary phone names renderable.

Any authored start or end in the syllable disables inference for that whole syllable. Explicit timings are not replaced or clamped. Partially edited syllables may therefore still need a complete valid onset/nucleus edit. Clusters, codas, voiced onsets and nucleus-free groups do not receive this default; unsupported articulation still rejects at backend preparation.

Automatic ends before an inferred onset stop at that onset's start, not the delayed next nucleus. Conflicting explicit ends reject. This prevents preceding vowels from overlapping the next frication gesture. Policy preparation adds linear per-note passes and bounded per-syllable state, not a token-by-syllable nested scan.

## Provenance and persistence

`PhonemeTimingAnchor::inferredStartFrame` distinguishes a generated start from `explicitStartFrame`. Resolved pronunciation tokens and saved `phonemeOverrides` remain unchanged. The articulated planner/renderer accepts either start representation and continues checking recipe bindings, score-note bounds and shared nucleus relationships.

The performance compiler revision is now 9; the procedural timing policy has revision 1, explicitly included in procedural render identity. This invalidates obsolete render content without rewriting recipe identities or saved edits. Candidate v2 records resulting planned source spans and compiler revision; those spans are not acoustic measurements or evidence of human approval.

## Evidence and remaining work

### Editor display and first boundary edit

The phoneme lane now selects the procedural timing policy when the region's owning track has a saved procedural-recipe reference. It performs no recipe-file IO during painting: this is a timing-policy projection, not proof that the resource is available or supports the phone. Existing sample-track timing and heuristic geometry remain unchanged.

Inferred onsets use their compiler start and nucleus end instead of the fixed 28-pixel placeholder. Their vowel starts use the same delayed nucleus. A `^` label prefix distinguishes compiler-inferred timing from `~` source-dependent geometry estimates and `!` conflicts. Both painting and boundary hit testing use this shared lane model. Valid procedural spans retain exact model widths rather than forcing a two-pixel minimum that would move a short span's boundary. Text remains governed by the existing constrained label policy.

The first boundary edit on a note containing inferred onsets materializes that note's dependent starts/ends, then applies the requested change in one composite command. This preserves neighboring vowel ends when a syllable stops using inference. Both initial and subsequent explicit edits now preflight the shared timing and affected note's gesture geometry: unresolved/empty spans, note/nucleus crossings and gesture overlaps reject before publication. Repeating an unchanged explicit boundary returns without a revision or render notification. Undo restores the prior state; redo restores the complete edit, with one render notification per operation. Materialization uses the editor's existing 48-kHz display clock and microsecond timing storage. This is not a complete new timing editor or an acoustic/source-capability check.

Tests cover two zoom levels, onset/nucleus geometry and hit testing, inferred versus authored flags, sample-policy restoration, initial conflict rejection, one-step undo/redo and rendering the resulting edited score through the actual procedural pipeline. Physical mouse/keyboard interaction and visual accessibility review remain unverified for this addition.

Timing tests cover multi-syllable allocation, onset/nucleus binding, shortened syllables, old-policy behavior and complete/partial authored timing preservation. Snapshot tests exercise untimed Japanese `さ` at a nonzero song origin without mutating pronunciation or overrides. Export tests bake and strictly load the untimed candidate through the real Final path. See the execution ledger for build/test results.

The 60 ms cap and quarter-syllable fraction are explicit engineering choices, not empirically qualified universal phonetic durations. Per-source duration controls, extended/pre-note timing, clusters/closures/bursts, full timing-editor/native qualification, and reviewed CV/VC/unseen-phrase listening evidence remain required. This does not complete U20 or Beta GO.
