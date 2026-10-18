# ADR 0005: Master-Only Branch Policy

- Status: Accepted
- Date: 2026-08-16

> **Status note, 2026-10-18.** Practice has moved away from this decision, and its enforcement is inactive. [AGENTS.md](../../AGENTS.md) sends isolated work to `codex/*` worktree branches that the integrating developer merges into `master`; on 2026-10-18 the main checkout had 45 local branches besides `master` (44 `codex/*`, one `optimize/*`). The hooks in `.githooks/` are not installed (`core.hooksPath` is unset). `scripts/verify_master_branch.py` and the `seam_branch_policy` target fail on those branches, and so does a default `seam_license_audit` run, whose only error is this branch check; with `--allow-non-master` the license audit passes. Reconciling the policy is part of owner decision D7 in the [completion plan](../plans/2026-10-18-project-completion-plan.md).

## Decision

Use only the `master` branch. Do not create feature, release, or temporary branches.

## Enforcement

- repository initialized with `master`;
- local branch verification script;
- pre-commit and pre-push hooks;
- CI runs only for master and rejects additional local refs in artifact verification;
- release report records branch state.
