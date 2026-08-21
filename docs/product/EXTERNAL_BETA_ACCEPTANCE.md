# Project SEAM External Beta Acceptance Contract

This is the machine-enforced completion contract for the closed External Beta.
It is deliberately separate from `G3`, `G4`, and `G5`. A source-ready build,
another platform's validator result, or an invitation does not satisfy it.

## Promotion states

The only terminal states are `EXTERNAL_BETA_READY` and
`EXTERNAL_BETA_CLOSED`. The operational states `CohortActive`,
`DistributionPaused`, and `Revoked` are explicit and never inferred from a
missing row. A paused or revoked candidate cannot be distributed or closed.

## Ready contract

`EXTERNAL_BETA_READY` requires every `EB-001` through `EB-008` requirement in
the JSON mirror to be `PASS`. Each PASS row references an immutable evidence
record whose candidate root, stage node, authorized parent edge, release
identity, platform/architecture/surface, final and installed hashes, tool,
workload, machine profile, privacy class, producer/reviewer roles, trusted
time, and raw archive bytes are present and internally consistent.

The candidate root must be sealed, its stage graph must use named authorized
transformations, and its archive must be externally anchored and immutable.
Missing signing, installation, bank, host, final-soak, or raw provenance
evidence remains `NOT_RUN` or `BLOCKED`; it cannot be replaced by a G3/G4/G5
result. Blocker and Critical issues remain blocking until explicitly resolved.

## Closure contract

`EXTERNAL_BETA_CLOSED` additionally requires an ended evaluation window, at
least one independent external A1 session on each target platform (Apple
Silicon macOS and Windows x64) completing F1, F2, and F5, an external
completed session for every claimed host tuple, a terminal disposition and
reason for every pseudonymous assignment, resolved checkpoints and incidents,
and approval by A3 plus A4 or A6. The gate remains blocked for a missing
checkpoint, no-show without replacement, withdrawal without disposition, or
unresolved Blocker/Critical incident.

## Evidence and identity

Evidence is stage-addressed and append-only. Signing, bundling, notarization,
stapling, and installation create distinct descendant nodes. Rebuilding or
mutating bytes creates a new candidate root. Product version, build ID, source
commit, and build epoch come from the generated `seam/build/version.hpp`
authority; project, bank, state, update, support, and evidence schema versions
remain independent.

Workload and reference-machine IDs are hash-bound in
`external-beta-performance-workloads.json`. Trust purposes are separated in
`external-beta-trust-policy.schema.json`; release tooling accepts signer
handles and fingerprints, never raw private-key paths or serialized secrets.

The checked-in contract is intentionally blocked until target machines,
signing/notarization credentials, rights evidence, host licenses, and raw
archive records exist. No synthetic PASS record is included here.
