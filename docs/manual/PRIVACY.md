# Project SEAM External Beta Privacy Notice

Document version: `external-beta-privacy-1.0`

Project SEAM is local-first. Projects, lyrics, audio, media, banks, full paths,
raw logs, error context, dumps, environment values, and host strings remain
local by default. Crash recovery writes a bounded local marker and may show a
local-private report; it does not upload automatically.

An export-safe support bundle contains only typed public build/artifact/bank
identity, coarse operating-system and host versions, stable diagnostic codes,
sanitized stack symbols, bounded counters, and a manifest of the exported
bytes. User-selected attachments are a separate per-file choice and are not
covered by the default export-safe guarantee unless you select them.

Before exporting, the application previews the final archive size, entry names,
and archive SHA-256. You may delete a local report or cancel export offline.
Support contact details and user attachments are kept in a separate restricted
intake; technical evidence uses an opaque reference and never needs your name.

Update checks are user initiated and independent from diagnostics. A failed,
offline, expired, or unverifiable check does not block authoring. The checker
does not collect account or device identity as part of the update contract.

The Beta does not implement telemetry or automatic diagnostic submission. If
you deliberately send a bundle, the recipient and retention terms are the ones
shown by the support channel at that time. You may withdraw a pending support
submission; local technical gate facts may retain only a non-identifying hash.
