# Project SEAM External Beta Update and Rollback

Document version: `external-beta-update-rollback-1.0`

Updates are manual and user initiated. The checker retrieves bounded metadata,
validates the offline root policy, delegated update key, channel, platform,
monotonic epoch, package size, package SHA-256, and candidate identity. A
normal downgrade or same-version replacement is rejected.

The full package is copied into a private no-link staging directory. The
application shows the candidate and asks for explicit confirmation before it
creates a sealed installer handoff. The platform installer reopens and
revalidates the exact handoff object, package bytes, publisher, and candidate
identity before changing system files. Running hosts and active operations must
be closed or explicitly cancelled first.

An offline check failure, timeout, bad signature, stale metadata, or hash
mismatch does not change installed files and does not block authoring. A failed
upgrade preserves the coherent predecessor checkpoint or invokes only the
separately authorized signed repair transition. Do not delete the predecessor
until the new installation has passed its launch and host-rescan checks.

If a package or key is revoked, stop distribution, record the opaque candidate
identity, and follow the security response policy. Do not attempt to bypass the
trust store or manually replace plug-in binaries.
