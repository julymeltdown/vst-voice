# Release audit reproduction and report admission repair

Scope: audit findings F16–F18, Full-Scope U45/U46 foundations. This change does
not freeze a resource matrix, choose empirical thresholds, approve resources,
run an external cohort, or issue Beta GO. The canonical contract stays unchanged.

## Operations now reproduce the audit

`PROMOTE_READY`, `START_COHORT`, `RESUME`, and `CLOSE` require the operation
snapshot and decision to identify the same `candidateRootSha256`. The decision
must contain a `releaseAudit` object with hash-bound `candidate` and
`archiveManifest` file references plus the restored `archiveRoot` directory.
Relative paths are based on the decision file's directory for the CLI.

The operation reopens and rehashes those documents, compares their candidate
identity with the snapshot, and executes `release_audit.audit_release` against
the restored bytes. CLOSED executes the complete CLOSED audit, including cohort
closure. The trusted archive-anchor requirement remains in force. Caller
`auditPassed`, `freshGo`, and `cohortAuditPassed` fields do not authorize any
transition. The recorded receipt contains the reproduced state and exact input
digests; it is not accepted as authority on a later operation.

Pause and revoke remain available without audit artifacts. No injectable audit
function or synthetic-contract switch was added to the operation path. Existing
approval-role requirements remain; this change does not authenticate a person's
identity or replace release authorization/signing policy.

The release audit now passes its restored root through to EB-009. A referenced
report must stay within that archive root. Its subordinate references must stay
within the report's directory. Absolute paths cannot substitute evidence from
outside the restored tree.

## The report has a tested success path

The artifact's `kind` is validated as an artifact field and only its locator and
digest are passed to the file-reference reader. Two additional contradictions
exposed by a full positive fixture were repaired: internal exact-ID sequences
may be tuples, and observations use the schema's `resourceIds` rather than a
nonexistent `resource` scalar; declared languages are actual language values
rather than the contract's `each-declared-language` placeholder.

Fixed numeric criteria now enforce their existing units and floors/ceilings.
`methodSha256` is the canonical JSON digest of the applicable frozen criterion
definition, and `protocolSha256` is the canonical JSON digest of its versioned
check definition. Retained review and measurement JSON must contain the same
typed claims as the report. Continuity file references are also rehashed.
Empirical cells use the existing typed grid validator, match frozen qualification
bindings, and compare measurements to the frozen per-cell maximum/minimum or
exact machine profile. No canonical qualification number was supplied by this
change. Observation source/build identity is checked against the candidate's
release identity when one is provided.

The temporary synthetic test report covers all 20 requirement rows, 83 case
rows, required check/operation records, and 175 empirical cells using a copy of
the contract. It contains generated test-tone audio and explicitly synthetic
records. It validates through the ordinary schema and semantic code without
mocking that code. Tampered audio, rehashed out-of-tolerance measurements,
substituted method/protocol/reviewer claims, and failed empirical cells reject.
The same report cannot satisfy the unresolved canonical contract. Test values
are neither production qualification nor an authorized release artifact.

## Remaining U45/U46 work

- Full capability/language/backend/resource-matrix coverage must be finalized
  with its actual frozen resource schema and qualified resources.
- Artifact-specific project/audio/inference/creator evaluators must reconstruct
  the promised musical and workflow properties from retained artifacts. Typed
  JSON agreement and hash equality are not independent acoustic reanalysis or
  proof that a human performed a review. Grouped creator comparisons and all
  protocol constraints still require their own evaluators.
- Public predecessor and signed public operation paths now reproduce the same
  Beta audit from restored inputs, with a complete synthetic archive/signature/
  report/cohort fixture and relocation/tamper tests. The exact entrypoints and
  policy/reference fields are documented in `docs/product/PUBLIC_RELEASE_RUNBOOK.md`.
- A real complete READY/CLOSED promotion cannot be demonstrated while the
  canonical matrix and empirical profile remain unresolved. Current operation
  tests exercise actual audit rejection and irreversible/pause behavior. Public
  replay tests additionally prove successful ordinary validation under an
  explicit synthetic trusted policy copy; no evaluator is patched and no
  production qualification is inferred.

This is additional gate implementation, not U45/U46 completion. Tests operate
on temporary local fixtures and never mutate a real release or remote state.
