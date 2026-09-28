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

- Do not modify `.github`; the user deferred GitHub CI work.
- Keep Windows support marked as TODO in `README.md`; the user is developing on macOS.
- Never claim human listening, DAW, screen-reader, signing, Windows, or external reviewer acceptance without the corresponding evidence.
- Treat the full-scope implementation ledger as root-owned: update it when integrating a completed unit, not from parallel implementation branches.
- Preserve unrelated changes and existing stashes. Do not use destructive Git commands to force a clean tree.
