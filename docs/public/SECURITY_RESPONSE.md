# Project SEAM Public Security Response Contract

**Status: `DRAFT / NOT APPROVED FOR DISTRIBUTION`**

**Required approval: the security-response owner and the named release authority.**

Document ID: `project-seam.public.security-response`
Version: `public-security-response-1.1.0`
Contact ID: `project-seam.public.security-contact`

## Published contact

Every active candidate must bind an operational security contact URI to the
contact ID above in its signed release record. A repository address or template
does not prove that the contact is monitored.

## Response flow

The response owner acknowledges, triages, reproduces, contains, remediates,
communicates, and records retention or deletion for the exact candidate. The
record identifies affected source, bank, package, installed-tree, update
metadata, and evidence-root hashes when applicable. Reporter data is limited to
what the reporter consents to share and is restricted to assigned responders.

The initial acknowledgement repeats the exact submitted bundle SHA-256 and
candidate lineage. Triage, containment, remediation, communication,
withdrawal, deletion, and any preservation exception remain bound to those
bytes. A replacement bundle begins a separate evidence identity.

## Distribution control

A credible compromise can publish a signed `DISTRIBUTION_PAUSED` operation.
Resume requires a new complete approval quorum signed after the pause. A signed
repair is narrowly scoped and does not silently replace the candidate. A
terminal `REVOKED` operation is irreversible; recovery requires a new candidate
root and a new evidence chain.

## Archive and disclosure

Decisions, trusted times, reviewer roles, user communication, and operation
envelopes remain reproducible from the governed archive. Approval envelopes
sign the terminal `EvidenceRoot` and remain outside its digest, preventing an
approval digest cycle. Disclosure timing is chosen to protect users while a
repair or revocation is distributed.

Restricted attachments and contact data follow
`project-seam.public.support-retention-1`: at most 30 days unless an assigned
security owner records a narrower investigation scope and expiry. Public
technical fields are capped at 180 days. Closure records the withdrawal or
deletion result and preserves only the minimal non-content audit record allowed
by the public privacy notice.

## Current availability

This checked-in contract does not claim a live response channel. The incident
drill, support intake, update path, rollback or revoke path, and independent
authorization must pass before `PUBLIC_ACTIVE`.
