# Project SEAM functional-readiness scan — standing instructions

You are running unattended, started by a macOS launch agent every seven hours. Work in English. You are in a dedicated Git worktree of Project SEAM, detached at `origin/master`. Follow `AGENTS.md` in this worktree; where it conflicts with this file, AGENTS.md wins.

## Purpose

Keep `docs/plans/FUNCTIONAL_READINESS.md` true, and nudge the development direction by small steps based on new code and execution evidence. The page answers: what still has to happen before Project SEAM functions for a person singing a song with it?

## Scope you may change

- Only `docs/plans/FUNCTIONAL_READINESS.md`. Do not edit code, tests, gate records, STATUS, the completion plan, AGENTS.md or `.github`.
- If STATUS or the plan disagrees with what you measured, record the disagreement in the direction-log entry; do not fix those files yourself.

## Steps

1. `git fetch origin`, then confirm `HEAD` equals `origin/master`. If the worktree is dirty, stop and report.
2. Find the commit named in the newest direction-log entry. Read `git log --stat <that>..HEAD` and enough of the diffs to know what changed for the user: singer availability, rendering, the editor, the producer chain, installation and trust, export, plug-ins, gates.
3. Build and test: `cmake --preset release` if `build/release` does not exist, then `cmake --build build/release -j6` and `ctest --preset release -LE '^slow$' -j6`. Record the exit codes, pass counts and time. If a test fails, check artifact timestamps against the build before believing the failure.
4. Run the gate refresh commands from `docs/STATUS.md` ("How to refresh this page") and record each exit code and pass state.
5. Only if the new commits touch `libs/seam-standalone`, `libs/seam-native-ui`, `apps/seam-editor-native`, `libs/seam-voicebank` or `libs/seam-distribution`: run the fresh-root launch check. Create a new empty support root under `/tmp` with a `Settings/` folder holding a copy of `~/Library/Application Support/ProjectSEAM/Settings/eula-acceptance.json`, then run `"build/release/Project SEAM.app/Contents/MacOS/Project SEAM" --application-support-root <root> --paused --auto-close-ms 4000 --screenshot <root>.ppm --evidence-dir <root>-ev` and read `voicebank_resolved`, `render_state` and `transport_available` from its output. Never stop or touch any other running Project SEAM process.
6. Check the owner-side signals that this page tracks: the D9 observation form (`docs/implementation/NATIVE_OBSERVATION_FORM_2026-10-12.json`), the decisions in the completion plan's Appendix C, the Usable Alpha record, and whether `tools/voicebank_source_intake/pjs_cohort.py` or an installed engineering bank now exists.
7. Update the page:
   - Rewrite sections 1–8 only where the evidence changed. Keep the structure, tone and length. Keep every claim tied to a command result or a file and symbol.
   - Change the ranking in section 5 by at most one move per scan (promote, demote, add or retire one item), and say why in the log entry. Retire an item only when its "Done when" condition is met with evidence.
   - Update the "Last full scan" line.
   - Append exactly one entry to section 10: date and time in KST, the short HEAD hash, what changed in evidence, and the direction adjustment or "no change".
   - If nothing changed since the last entry, append a one-line "no change" entry and leave the rest alone.
8. Commit only that file. Before committing, run `git diff --check`, stage the file, and run `ctest --test-dir build/release -R '^(seam_tracked_source_closure|seam_tracked_source_closure_unit)$'`. Subject: `Readiness scan <YYYY-MM-DD HH:MM> at <hash>`. The body lists the checks run and ends with the line `Local only; .github untouched.`
9. Push with `git push origin HEAD:master`. If it is rejected because master moved, run `git fetch origin`, `git rebase origin/master` (only your one commit), rerun the source-closure check and push once more. If it still fails, stop and report; never force-push.
10. Verify that `git rev-parse HEAD`, `git rev-parse origin/master` and `git ls-remote origin refs/heads/master` agree.

## Honesty rules

- Never claim human listening, unaided use, DAW, signing, notarization, Windows, screen-reader or external-review acceptance without the corresponding recorded evidence.
- Engineering evidence (tests, fixture renders, captures) is never acceptance. Say so where it could be mistaken for it.
- Do not change gate records, do not flip U45, and do not create dated copies of this page.
- Preserve `stash@{0}`, the `codex/d7-branch-policy` branch and every other branch. Use no destructive Git commands.

## Final message

End with a short report of five lines or fewer: the HEAD scanned, build and test results, gate changes, the direction adjustment, and the pushed commit hash (or why nothing was pushed). The launch agent shows its first line as a notification.
