# U5 acceptance audit — 2026-09-06

Verdict: **U5 local implementation acceptance PASS.** This audit covers the approved U5 ordered-phoneme-timing unit, not release-quality singing or the entire Beta GO goal.

Authority: `docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md`, U5. The complete R1–R20/V01–V18 requirements and U6–U48 remain mandatory. U6 owns complete F0/expression compilation; U15/U16 own measured processing and strengthened pitch/duration rendering. Raw diagnostic alignment is not substituted for those requirements.

## Requirement-to-evidence review

| U5 requirement | Current implementation and proving evidence |
| --- | --- |
| Ordered target spans from score, tempo and explicit offsets | `phoneme_timing_plan.cpp` compiles bounded note-local groups, real nucleus identities, separate explicit starts/ends and absolute output frames. `test_phoneme_timing.cpp` covers tempo-resolved endpoints, rounding at multiple rates, onset/nucleus separation, trailing coda ownership, overlapping notes and malformed input. |
| Sequential `かき` nuclei in one note | `test_synthesis.cpp`: `timing solver places multiple syllables sequentially within one note` checks distinct nuclei and downstream placement. Shared lane tests in `test_ui.cpp` verify the same default/edited boundaries and visibly marked estimates/conflicts. |
| A 30 ms edit moves intended acoustic placement | WAV-backed command tests in `test_synthesis.cpp` exercise raw/dispatched onset and vowel edits. `test_performance_snapshot.cpp` verifies exact waveform landmarks at both nuclei in an aligned long unit, including a second-note-relative edit across adjacent notes. |
| Real pipeline undo restores the result | The snapshot regression applies/reverts/reapplies `UpsertPhonemeOverrideCommand`, creates normal snapshots and renders production-region PCM. Original/edited identity and audio restore exactly; disk-cache replay after memory eviction matches. Frozen rendering survives removal of the test-created sidecar. |
| Source markers remain separate from target timing | Source alignment has its own bounded JSON schema, exact WAV digest and ordered phone landmarks. Timing placements retain target records; mapping validates matching coverage and rejects contradictory knots instead of inventing source positions. Bank/package hashes bind sidecar bytes; pack/install tests verify preservation and tamper detection. |
| Short-note and impossible timing conflicts are bounded/actionable | Compiler/solver tests cover too few frames, crossed nuclei, explicit ends crossing protected targets, invalid rates and insufficient post-vowel transition space. Edits are not silently clamped or target spans extended. |
| Preutterance, release and coarticulation policy | Snapshot tests check negative preutterance clipping at frame zero at 44.1/48 kHz without moving surviving audio, and explicit release past the note but inside its region. Compiler rejects region overruns. `seam overlap window bounds crossfading without shifting unit placement` checks exact overlap behavior; `PHONEME_TIMING_DISPLAY.md` states the policy. |
| Missing alignment cannot masquerade as complete placement | Normal snapshot selection chooses compatible smaller units or rejects forced unaligned multi-nucleus units. The frozen pipeline independently rejects supplied legacy plans lacking evidence. These are compatibility outcomes, not replacements for the verified aligned long-unit path. |
| Coupled edits use final dependencies | The audit reproduced a false conflict when both nuclei moved later than the original equal-time boundary. Timing revision 9 defers automatic-end validation until the edited next nucleus resolves. Dedicated tests retain rejection of explicit reversed ends and crossed nuclei; the snapshot command test checks actual samples for 300/400 ms anchors and restoration. |

## Verification scope and limits

Post-fix strict affected Debug/Release builds passed. Four CTest suites passed in each configuration: `seam_tests` (474 cases), synthesis (33), dedicated timing (14), and snapshots (17). Debug elapsed 88.96 seconds, including 84.47 seconds for core/native; Release elapsed 11.98 seconds, including 9.97 seconds for core/native. The earlier pre-fix Debug core run and intentionally failing new regression are not substituted for this final verification. `git diff --check` passed.

The rebuilt `seam_tests` target includes core, UI/native interaction, authoring, installer and standalone workflow regressions. It is not a fresh installed-app visual review, signed platform/DAW matrix, or perceptual singing qualification. Focused synthesis, dedicated timing and snapshot suites supplement it. Case counts across suites overlap and must not be added as a unique-test total.

All source remains local/uncommitted. Publication/source-index closure is not claimed. Structural validity and signed byte identity do not establish source rights, phonetic accuracy, reviewer admission or musical quality.

Current aligned raw support is root-pitch, same-pitch across notes, without pitch automation or nondefault raw overrides. Unsupported renderer/pitch requests reject explicitly. Complete melody, pitch-preserving rendering, expressive modulation and qualified singer resources remain downstream implementation work.

## Next implementation action

Continue U6: compile bounded absolute-time F0/voicing, dynamics, articulation and modulation phase from complete score/performance intent, independently of selected sample units. Preserve canonical ownership/base/offset/vibrato semantics. Then connect renderer consumers under the planned downstream capability/quality gates; do not redefine complete singing as the current raw diagnostic path.
