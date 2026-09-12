# Asynchronous initial workspace recovery

The Designer handoff now starts a recovery worker rather than running repository
recovery inside the UI action. The worker owns an isolated controller and uses
the same inventory/registered-PRODUCER validation as the synchronous route.
It prepares candidate marker lineage before completion; the UI transfers the
prepared state without parsing that metadata again.

Opening participates in Studio's existing busy/poll/cancel lifecycle and reports
OPENING WORKSPACE / ESC CANCEL. The previous Designer draft is not passed to the
worker or replaced. Adoption occurs only on the owner thread after completion,
provided the producer epoch and initial empty Studio context still match.

Cancellation discards the prepared context even if recovery finished before the
owner consumed its result. Failed validation leaves the empty context and epoch
unchanged. Retry is supported after completion. Finish/destruction request
cancellation and join the worker instead of abandoning it. A blocking filesystem
read is not forcibly interrupted: cancellation prevents adoption, but completion
latency still depends on the underlying recovery operation returning.

This API is for initial source-free entry. It rejects an already loaded producer,
nonempty manifest, dirty sample state or another active worker; it does not
implement asynchronous replacement of an existing workspace. CLI/startup callers
retain the existing synchronous method.

## Verification

Full Release build passed. Studio manifest-draft, Designer, export and aggregate
core suites passed in 24.74 seconds. Tests prove deferred adoption, busy conflicts,
cancel-before-consumption, retry, invalid producer rejection, unchanged epoch on
failure, shutdown cancellation and exact recovered project state. Existing
multilingual/nasal/plosive candidate bake/import cases now also recover through
the asynchronous API and compare adopted markers and durable generation exactly.

Logs are retained under `evidence/async-workspace-2026-09-09/`. This was not a
fresh full-suite run, a new live UI test, or a large-workspace latency benchmark.
Native cancellation interaction and resource-budget qualification remain open.
No new whole roadmap unit, source approval or Beta GO acceptance is claimed.

No captured files were missing relative to `session-preservation-iEgsjc`.
Existing dirty work was preserved without staging, committing or pushing.
