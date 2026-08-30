# Project SEAM Public Support Contract

**Status: `DRAFT / NOT APPROVED FOR DISTRIBUTION`**

**Required approval: the operated support owner and the named release authority.**

Document ID: `project-seam.public.support`
Version: `public-support-1.0.0`
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

## Diagnostic handling

Users preview and consent to each attached file. Exported bundles remain local
until submission. Support must accept repeated exports without overwrite,
retain their SHA-256 identities, and distinguish public technical data from
restricted support data and private local data.

## Current availability

The checked-in contract is not evidence of a staffed endpoint or response
time. Public support is considered available only for a candidate whose
support-intake and incident-drill requirements pass and whose release state is
`PUBLIC_ACTIVE`.
