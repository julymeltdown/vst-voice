# Project SEAM Public Privacy Notice

**Status: `DRAFT / NOT APPROVED FOR DISTRIBUTION`**

**Required approval: privacy/security review and the named release authority.**

Document ID: `project-seam.public.privacy`
Version: `public-privacy-1.1.0`
Channel: `public-direct-download`

## Acceptance and scope

The product records the accepted document ID, version, SHA-256, and UTC time.
A changed version or digest requires explicit reacceptance. External Beta
acceptance is a different identity and cannot satisfy this public notice.

## Local-first data

Projects, lyrics, imported media, voicebanks, autosaves, exports, and ordinary
diagnostics remain on the user's system unless the user takes an explicit
export or submission action. Project SEAM does not treat local files as consent
for network upload.

## Network actions

The application may contact the signed public channel for update, repair,
pause, supersession, or revocation metadata. A support or security submission
must show the destination and the files to be sent, allow per-file exclusion,
and require confirmation. Crash and diagnostic exports are repeatable,
collision-safe local files; export does not submit them.

## Support records

A submitted report may contain product identity, package and installed-tree
hashes, voicebank identity, host/device details, logs selected by the user, and
the user's contact reply channel. The public support process records intake,
acknowledgement, triage, reproduction, resolution or escalation, user
communication, and retention or deletion. Restricted attachments are shared
only with people assigned to the report.

The acknowledgement identifies the exact exported bundle SHA-256, candidate
lineage, destination ID, and acknowledgement ID. Every later lifecycle record
uses that same bundle hash. A filename, ticket title, email subject, or newly
generated export cannot substitute for the acknowledged bytes.

## Retention, deletion, and withdrawal

Retention policy `project-seam.public.support-retention-1` permits public
technical fields for at most 180 days and restricted attachments or contact
data for at most 30 days. Earlier deletion is allowed when the report no longer
needs the material. A user may withdraw optional attachments or request
deletion by supplying the acknowledgement ID or exact bundle SHA-256.

Withdrawal and deletion records must repeat that bundle SHA-256 and identify
the responsible owner. After deletion, the service may retain only a minimal
audit record containing the opaque acknowledgement ID, candidate lineage,
bundle SHA-256, lifecycle times, and disposition. It must not retain contact
data, attachment bytes, projects, lyrics, audio, bank bytes, or raw logs.

Law or an active security investigation may require narrower preservation.
The assigned privacy or security owner records its basis, scope, and expiry;
the exception does not convert restricted material into public technical data.

## Release status

This source document does not claim that a public endpoint is operating. The
signed candidate record must bind the operational destination and evidence;
the canonical public gate remains fail-closed until that drill passes.
