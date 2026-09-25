# SEAM Progress Report — 2026-09-25

Basis: the pre-commit snapshot of branch `codex/production-readiness-completion` at `a3ff816a`, plus its then-uncommitted worktree. Previous baseline: `SEAM_PROGRESS_AND_FINISHING_PLAN_2026-09-23_KO.md`. This report grants no unit or release acceptance. GitHub CI is deferred by user direction and was not evaluated. The version-control risk below describes that initial snapshot; subsequent commits and their remote state must be checked in Git.

## Summary

The engine and production pipeline are mostly built and tested locally. Beta GO is still far away because it depends on evidence that code cannot produce: human listening, a real singer corpus, frozen thresholds, independent reviewers, and external hosts and platforms.

| Measure | Value | Change since 09-23 |
|---|---:|---|
| Pipeline working end to end (record → design → generate → edit → package → install → export) | ~90% | unchanged, hardened |
| Planned implementation, weighted estimate | ~56% | +1 point (U15 started) |
| Units formally accepted | 6/48 = 12.5% | unchanged |
| Beta release-gate evidence (EB 9 + R 20 rows) | 0/29 = 0% | unchanged |
| Human listening results | 0/46 rows scored | unchanged |

The weighted estimate counts accepted units as 1.0, implemented but unaccepted units as 0.8, partial units as 0.45, and units that have not started as 0. It measures how much of the planned code exists, not whether the product is ready.

## Unit status (48 units)

| Status | Count | Units |
|---|---:|---|
| Accepted (local) | 6 | U1–U5, U29 (POSIX only) |
| Implemented, not accepted | 10 | U6, U7, U10, U11, U17, U18, U23, U24, U30, U31 |
| Partial | 29 | U8, U9, U12–U16, U19–U22, U25–U28, U32–U41, U44–U47 |
| Not started | 3 | U42 (final singer candidates), U43 (musical/creator qualification), U48 (final archive and decision) |

U15 moved from not started to partial: versioned acoustic analysis, pitch-mark staleness detection, and renderer use of measured voicing are in code. Acceptance still needs listening and fixed-corpus results.

## Work since 09-23

There are 29 new commits and about 136 ledger entries. Most of the work went to:

- New Project singer catalogue and diagnostics (U22, the largest share).
- Direct vibrato editing on the piano roll with pointer, keyboard, and accessibility controls (U25).
- Multi-track MIDI export and import, including an FL Studio round trip and an OpenUtau desktop round trip that reproduced the original file byte for byte (U31/U32).
- USTX 0.6–0.9 import and export, checked against a file saved from the OpenUtau desktop app (U30, still PARTIAL).
- The trained neural model now runs through the production worker and the normal render and export path. This path works end to end, but the model is trained on synthetic data and its audio has not been qualified (U35/U37).
- English (CMUdict) and Korean pronunciation fixes, character performance binding, and harmony tracks.

The latest full Debug run passed 1,129/1,129 tests. Focused USTX tests passed in Debug and Release today.

## What blocks Beta GO

| Blocker | Who can close it |
|---|---|
| Nobody has listened to the singer; 46 blind packets are ready and unscored | Human, a few hours |
| Training data is 41 minutes of the project's own procedural singer, with no human voice | Rights-cleared human corpus, a budget and legal decision |
| 11 quality thresholds and the evaluation profile are UNRESOLVED, so the gate cannot pass structurally | Human decision after measurements |
| Independent reviewers (musician, vocal producer, native speakers, 5 creators) | Humans |
| Nine DAW host tuples, macOS signing and notarization, and an immutable archive | External tools and licenses |
| Windows runtime | README TODO; the contract still requires it |
| GitHub CI | Deferred by user |

Agent-only work can still finish the partial units (about 1–2 weeks of focused work). After that, the remaining time depends on people, data, and external systems.

## Immediate risks

1. **Unsaved work at the initial audit.** The 29 commits after `origin/master` (`44be07e7`) existed only on this machine. On top of that, 185 files (+14.5k lines) and 20 new files were uncommitted. Their later publication status is a separate Git check.
2. **Disk space.** The disk is 92% full (77 GiB free). One earlier test run failed with `No space left on device`. Clean build outputs before long test or training runs.

## Recommended order

1. Commit and push the current worktree, then merge it into `master`.
2. Score the 46 listening packets. This single step tells us whether any quality work so far is moving in the right direction.
3. Decide on acquiring a human singing corpus. Without one, quality is capped at the procedural teacher voice.
4. Meanwhile, the agent keeps closing partial units, prioritizing U22, U25, and U30–U32 because they are closest to acceptance.
