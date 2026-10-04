# Agent Instructions

## Commit and push cadence

- This cadence is the default for every implementation turn, including each “continue” handoff. Do not wait for a large milestone or the end of a session before committing and pushing verified work.
- Keep changes in small, reviewable units. Commit each completed and locally verified unit as soon as its focused checks pass; do not let multiple unrelated units accumulate in one working tree.
- Push completed work promptly. For work on `master`, push each verified commit to `origin/master`. For isolated `codex/*` worktrees, commit on that branch and notify the integrating developer immediately; the integrating developer should merge, verify, and push each coherent unit to `origin/master`.
- If a unit is still in progress after roughly 30 minutes, commit a safe checkpoint when the current state is internally consistent. Mark incomplete work clearly in the commit message or handoff; do not present an unverified checkpoint as complete.
- After every push, verify that local `HEAD`, `origin/master`, and `git ls-remote origin refs/heads/master` agree. Report the commit hash and the checks actually run.
- Before committing, inspect `git status` and the staged diff. Stage only files belonging to the unit; preserve unrelated user changes, worktree edits, and generated artifacts.
- Use a concise commit subject and include the verification scope in the commit body. For repository commits in this project, end the body with: `Local only; .github untouched.`

## Mandatory implementation handoff checkpoint

- Do not move on to another substantial unit, end a “continue” turn, or report a completed milestone while a verified unit is still only in the working tree. Commit it and verify the push first.
- A parallel agent's “done” message is not an integration checkpoint. The lead agent must inspect the resulting diff, run the agreed focused checks in the integrating checkout, commit the coherent unit, push it, and verify the remote hash before describing it as landed.
- If a push cannot be completed, state the exact blocker and keep the unit explicitly marked unpushed; never imply that a local commit is on `origin/master`.
- At each handoff, report the latest pushed commit hash, focused checks run, and any remaining uncommitted or unpushed work.

## Project constraints

## Verification hygiene

- Before trusting a CTest result, confirm every dependent target is rebuilt. A focused
  `cmake --build ... --target a b` leaves other binaries linked against the old library, and a stale
  binary fails in a way that points straight at the newest change. When a `libs/` header or source
  changes, prefer a full `cmake --build build/release -j6` before the CTest run.
- Confirm a failure is real by checking artifact timestamps before changing code. This happened on
  2026-10-04: `seam_clap_plugin_host_smoke` failed against a state file written minutes earlier, and
  the cause was a stale `seam_clap_host`, not the codec under test.
- Do not assume a tool is unverified because a handoff says so. Re-probe it: on 2026-10-04 the
  claimed-unrun `seam_singer_pilot` turned out to be covered mode-by-mode by
  `tests/test_singer_pilot_cli.py`, including determinism repeats.
- Probe unfamiliar CLIs carefully. `seam_singer_pilot` has no argument parser, so `--help` was read
  as an output directory and it wrote a pilot packet into a directory named `--help`.

- Do not modify `.github`; the user deferred GitHub CI work.
- Keep Windows support marked as TODO in `README.md`; the user is developing on macOS.
- Never claim human listening, DAW, screen-reader, signing, Windows, or external reviewer acceptance without the corresponding evidence.
- Treat the full-scope implementation ledger as root-owned: update it when integrating a completed unit, not from parallel implementation branches.
- Preserve unrelated changes and existing stashes. Do not use destructive Git commands to force a clean tree.
