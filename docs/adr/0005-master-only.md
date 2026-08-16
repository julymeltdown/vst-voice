# ADR 0005: Master-Only Branch Policy

- Status: Accepted
- Date: 2026-08-16

## Decision

Use only the `master` branch. Do not create feature, release, or temporary branches.

## Enforcement

- repository initialized with `master`;
- local branch verification script;
- pre-commit and pre-push hooks;
- CI runs only for master and rejects additional local refs in artifact verification;
- release report records branch state.
