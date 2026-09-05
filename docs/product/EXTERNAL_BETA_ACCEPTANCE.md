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

`EXTERNAL_BETA_READY` requires every `EB-001` through `EB-009` requirement in
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

`EB-009-full-product` adopts every mandatory R1–R20 and V01–V18 outcome from
the user-settled full-scope report. The exact 83-case registry, required
language/resource/backend/platform/host dimensions, workloads, independent
review roles and fixed/empirical criteria are in
[`full-product-beta-contract.json`](full-product-beta-contract.json).
The acceptance JSON embeds its actual byte digest; the evaluator rehashes
the referenced file and the candidate root transitively binds that digest.

The [authority amendment](FULL_SCOPE_AUTHORITY_AMENDMENT.md) supersedes the
old scope veto and deferrals while preserving historical NOT_RUN studies.
Japanese, English and Korean, procedural voice design, real-input production,
classical and qualified neural singing, all expressions and nine host tuples
are mandatory. The explicit unresolved empirical criteria and resource matrix
block final acceptance. The 500 ms classical small-edit target does not relax
the separate existing 400 ms preview workload.

The closed evidence envelope explicitly declares a hash-bound
`fullProductReport` reference for EB-009. Its
[typed schema](full-product-beta-evidence.schema.json) cannot certify musical
or product completion. Until U45 implements semantic artifact validation,
READY and CLOSED always report the explicit EB-009 unavailable-validator
failure, including for fabricated PASS summaries.

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

The engineering evidence surfaces are intentionally separate from release
truth: `tools/external_beta/voicebank_gate.py` validates the non-official Beta
bank dossier, `tools/voicebank-script-generator/main.py` generates the pinned
recording inventory and operator CSV, `tools/external_beta/voicebank_production.py`
validates immutable takes, QA, retakes, candidate exports, and bank locks,
`tools/external_beta/install_evidence.py` validates clean-snapshot lifecycle
records and exact installed-byte inventories; `scripts/run_external_beta_install_evidence.py`
provides the fail-closed command surface,
`scripts/run_external_beta_standalone_journey.py` validates the twenty-row
standalone matrix, and `scripts/run_external_beta_product_soak.py` validates
the 30-minute/120-minute resource and fault records. `tools/external_beta/host_evidence.py`
and `scripts/run_external_beta_host_evidence.py` validate the nine required
REAPER, Bitwig Studio, and Logic Pro installed-byte tuples. The checked-in templates
remain BLOCKED until real target-machine, rights, signing, and raw evidence
exists.

`tools/external_beta/evidence_archive.py` and
`tools/external_beta/evidence_audit.py` rehash a restored governed archive and
verify candidate-root lineage before any READY decision. The operational order
is documented in `docs/product/EXTERNAL_BETA_RUNBOOK.md`.
