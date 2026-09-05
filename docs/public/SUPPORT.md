# Project SEAM Public Support Contract

**Status: `DRAFT / NOT APPROVED FOR DISTRIBUTION`**

**Required approval: the operated support owner and the named release authority.**

Document ID: `project-seam.public.support`
Version: `public-support-1.1.0`
Destination ID: `project-seam.public.support-intake`

## Published destination

Every active candidate must name an operational intake URI in its signed public
release record and prove that the URI belongs to the destination ID above. The
URI is execution data and is not replaced by a developer address, source-tree
path, checklist, or placeholder in this document.

## Required report lifecycle

The operated support path records these states for the exact candidate:

1. `INTAKE`
2. `ACKNOWLEDGED`
3. `TRIAGED`
4. `REPRODUCED`
5. `RESOLVED_OR_ESCALATED`
6. `USER_COMMUNICATED`
7. `RETAINED_OR_DELETED`

The report identifies a named owner at every state. A resolution includes the
affected release identity, disposition, user communication, and retention or
deletion result. An unresolved safety or integrity report may trigger a signed
pause, repair, rollback, supersession, or revocation operation.

## Hash-bound acknowledgement

Intake accepts only a bundle conforming to
`../product/public-support-bundle.schema.json`. Its acknowledgement returns an
opaque acknowledgement ID and repeats the exact bundle SHA-256, candidate
lineage, destination ID, and assigned owner. Every lifecycle transition,
withdrawal, and deletion record repeats the same bundle SHA-256. Re-exporting a
report creates a different report identity and cannot silently replace the
acknowledged bytes.

The operated lifecycle record conforms to
`../product/public-support-report-lifecycle.schema.json`. Candidate evidence
must include all seven ordered stages, a non-empty owner ID for each stage, the
acknowledgement identity, and the minimal retained audit-record hash.

## Diagnostic handling

Users preview and consent to each attached file. Exported bundles remain local
until submission. Support must accept repeated exports without overwrite,
retain their SHA-256 identities, and distinguish public technical data from
restricted support data and private local data.

## Retention and withdrawal

Policy `project-seam.public.support-retention-1` caps public technical fields at
180 days and restricted attachments or contact data at 30 days. A valid
withdrawal or deletion request names the acknowledgement ID or bundle SHA-256.
The service deletes restricted payloads and contact data, records the same
bundle hash and responsible owner, and keeps only the minimal audit record
defined by the public privacy notice. A security preservation exception must
be scoped, owned, and time-bounded.

## Current availability

The checked-in contract is not evidence of a staffed endpoint or response
time. Public support is considered available only for a candidate whose
support-intake and incident-drill requirements pass and whose release state is
`PUBLIC_ACTIVE`.
