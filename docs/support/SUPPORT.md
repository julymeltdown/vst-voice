# Project SEAM External Beta Support

Document version: `external-beta-support-1.0`

## Intake

Support accepts a short reproduction, exact application/build identity, exact
OS/architecture/host/format tuple, stable diagnostic codes, and the previewed
export-safe bundle hash. A user-selected attachment is optional and must be
listed separately. Never send a project, lyrics, audio, bank bytes, raw logs,
full paths, dumps, or environment values by default.

## Severity

- **Blocker:** installation, launch, data loss, signature/trust, or host crash
  prevents the primary Beta journey.
- **Critical:** reproducible corruption, security boundary failure, stuck
  notes, or unsafe privacy/export behavior.
- **Major:** a named supported journey is unusable but a safe workaround exists.
- **Minor:** limited presentation, documentation, or non-core behavior issue.

Severity is triaged against the exact retained evidence tuple. No commercial
SLA is promised. Response, workaround, waiver, and expiry are recorded against
an opaque issue identifier.

## Recovery and deletion

Keep the original project and autosave until support asks you to verify a copy.
Use safe mode after repeated startup crashes. Delete local reports through the
application; do not delete the project or bank root to clean up a report.
Withdrawing an intake deletes restricted contact and attachment payloads while
leaving only a non-identifying technical disposition where required.

## Update or revocation incident

If a package, bank, or delegated key is revoked, pause distribution, preserve
the signed candidate identity and raw verifier output, and publish the exact
affected tuple through the invite-only support channel. The recovery path is a
new signed installer or explicitly authorized repair package. Never advise a
tester to disable signature checks.
