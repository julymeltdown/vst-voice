# Source quality assessment implementation

The producer now supports `inspect-source-quality` and `record-source-quality`
without hand-editing current project JSON. This is coverage/listening recording,
not source-rights acquisition or actual singer qualification. The full R1–R20
scope remains unchanged and no additional unit is accepted.

Schema 3 adds immutable assessment history while preserving historical schema
1/2 snapshots and the default schema-2 Draft writer. Records bind policy,
current source-owned audio, evidence and an independent registered reviewer.
Canonical writer transitions, evidence integrity, stale material, duplicate
decisions, self-review and schema downgrade are checked. Reassessment preserves
ingress rights/history and invalidates affected unit approval. Candidate
provenance retains local evidence; release/disclosure qualification remains open.

The CLI journey no longer starts with coverage/listening PASS already seeded.
It imports unassessed material, inspects and records a synthetic reviewer
decision through the actual CLI, creates an editable draft, reviews units,
publishes/packages/installs, then reopens and exports a new score with producer,
original WAV, original quality evidence and original license paths unavailable.
This is a sine fixture with a test signing key—not a real qualified singer.
A separate CLI case checks reassessment after unit approval.

## Verification snapshots

- Full Release build passes, including a final rebuild after readiness hardening.
- Full CTest before the final readiness/query hardening: **119/120 PASS**,
  321.17 s. Only source closure failed, with **358 unindexed required inputs**
  at that execution. Later evidence files can change this count.
- After final shared-readiness, indexed material lookup and CLI-query updates:
  **5/5 focused suites PASS**, 25.76 s.
- Latest core: **826 passed, 0 failed**; producer: **42/42**; Draft: **9/9**;
  CLI source/review/install/export: **4/4**; Studio: **24/24**.
- Latest Python parity/admission: **19 tests PASS**, 1.124 s, including actual
  C++ CLI identity comparison and schema-3 verification.
- `git diff --check`: PASS. The earlier full run is not relabeled as another
  full execution of the final small hardening change.

Current CLI SHA-256:
`c35c68aa486f5dbda0c20c4bfee48a93a8f8cffaa1101fd810fc8fcb48d0e0d5`.
Raw full/focused execution is in `evidence/source-quality-2026-09-09/`.

No staging, commit, push, actual reviewer approval or release promotion was
performed. Source comparison against `session-preservation-8cD2iU` found no
missing captured source files; changes were intentional. This does not settle
whether earlier uncaptured session changes were lost.

## Remaining boundaries

Native assessment-entry UI, initial source/rights configuration, legacy take
attribution, language/style assignment migration, unit-kind QC, measured
large-bank assessment/readiness costs, private-evidence release disclosure, and
real music/source qualification remain open. These supplied manual decisions
do not replace the full-product gate's empirical and independent evidence.
See the exact format/identity contract in
`../formats/PRODUCTION_SOURCE_QUALITY_V1.md`.
