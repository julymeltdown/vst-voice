# Project SEAM Public Production Acceptance Contract

**Canonical machine mirror:**
[`public-release-acceptance.json`](public-release-acceptance.json)

**Current result:** `BLOCKED`

This gate extends the Usable Alpha and External Beta contracts without changing
their document identities or evidence. Source readiness, CI, an internal
validator, a checklist, or a synthetic fixture cannot authorize public
distribution.

## State contract

The exact states are `DRAFT`, `AUTHORIZED_FROZEN`, `SIGNED`,
`CLEAN_INSTALLED`, `BANK_READY`, `EVIDENCE_PASSED`, `EXTERNAL_BETA_READY`,
`EXTERNAL_BETA_CLOSED`, `PUBLIC_ACTIVE`, `DISTRIBUTION_PAUSED`, `SUPERSEDED`,
and terminal `REVOKED`.

`PUBLIC_ACTIVE` requires `EXTERNAL_BETA_CLOSED` for the same candidate lineage.
A pause stops new acquisition and normal updates. Resume requires a fresh
complete signed quorum whose trusted times follow the pause. `REVOKED` cannot
transition to any state. A revoked product can be replaced only by a new
candidate lineage.

## Immutable root chain

1. `FreezeRoot` binds source, bank source, public documents, SBOM, trust policy,
   toolchain, and every unsigned payload.
2. `ArtifactRoot` binds `FreezeRoot` plus signed macOS, Windows, and bank
   descendants.
3. `EvidenceRoot` binds `ArtifactRoot`, installed trees, the evidence index,
   and the restored immutable archive manifest.

Independent reviewer approval envelopes sign the terminal `EvidenceRoot`,
role, signer identity, decision, policy version, and trusted time. Every trusted
key binds `keyId`, role, and `signerId`; distinct signer strings alone are not
authority. They remain outside all three root digests. The release-manager
operation envelope is signed under a separate actor-bound trusted-key policy
and references the evidence root and approval-envelope hashes, so approval
renewal cannot create a digest cycle.

## Mandatory categories

| ID | Requirement |
|---|---|
| `PR-001-contract` | Public contract and exact release identity |
| `PR-002-root-chain` | Three-layer root coherence |
| `PR-003-external-beta-closed` | Same-lineage closed External Beta |
| `PR-004-public-documents` | Public document identity, digest, acceptance, and reacceptance |
| `PR-005-signed-artifacts` | Signed macOS, Windows, and bank descendants |
| `PR-006-clean-installed` | Independent clean-installed trees |
| `PR-007-bank-ready` | Rights, musical, package, and installed-bank identity |
| `PR-008-target-matrices` | Apple Silicon `UA-001..UA-020` and separate Windows `PW-001..PW-020` |
| `PR-009-update-channel` | Signed download, update, pause, repair, supersede, and revoke metadata |
| `PR-010-support-intake` | Bundle-hash-bound intake through acknowledgement, triage, reproduction, disposition, communication, withdrawal, and retention/deletion with a named owner per stage |
| `PR-011-incident-drill` | Candidate-bound pause, repair, rollback, revoke rehearsal, and communication |
| `PR-012-archive-restore` | Restored immutable archive and raw evidence hashes |
| `PR-013-approvals` | Role-bound Ed25519 reviewer quorum plus separately signed release-manager operation |
| `PR-014-rollback-revoke` | Verified rollback and irreversible separate-lineage revoke rehearsal |

Every category must be `PASS` and reference candidate-bound raw evidence inside
the restored archive. Open Blocker or Critical issues remain blocking.

## Public documents

The checked-in public EULA, privacy, support, and security-response documents
are `DRAFT / NOT APPROVED FOR DISTRIBUTION`; the EULA requires legal counsel
approval. They have public IDs and SHA-256 digests separate from External Beta.
The candidate stores the accepted public ID, version, and digest. An External
Beta acceptance or digest cannot satisfy a public row, and `PUBLIC_ACTIVE`
requires every document entry to carry `approvalStatus: APPROVED`.

## Current snapshot

The repository includes no public candidate record, target evidence, approval
envelope, or operated endpoint evidence. The acceptance mirror therefore
remains `BLOCKED` with an empty evidence array. Synthetic passing candidates
exist only in tests and never become canonical release evidence.
