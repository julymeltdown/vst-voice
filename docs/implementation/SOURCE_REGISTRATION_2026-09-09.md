# Explicit source registration

## Capability delivered

An empty source-aware producer Draft can now register and select a source through
the supported repository API and actual CLI, rather than preloading a source in
the initialization definition or editing durable project JSON. This advances
U9/R4/R19/R20; it does not complete those units or authorize Beta GO.

`ProductionProjectRepository::registerSource` verifies the exact current project
digest, registered PRODUCER role, UTC timestamp, new source ID, explicit rights
declaration and captured evidence digest. It accepts human, procedural and
TTS-derived source kinds. Every permission remains explicitly supplied; neither
the CLI nor repository infers rights from an engine name or source kind.

Coverage and listening must remain Not assessed. Registration never approves
units, attributes old takes, rewrites an existing source ID, or changes existing
source bindings. Registering a different source selects it for subsequent work;
previous material retains its original provenance. A Qualified lifecycle is
invalidated rather than silently retaining whole-project qualification.

The exact nonempty evidence bytes (maximum 4 MiB) are retained under
`source-evidence/<sha256>.txt`. The original locator remains the declared source
location. The existing execution path still requires that locator to be available
and unchanged for new imports/generation; the retained copy is not an implicit
permission to ignore changes to the original authorization.

Persistence uses the existing serialized journal writer and recoverable commit
receipt. A stale writer cannot supersede current work; cancellation before commit
does not update caller state. An exact recoverable commit is reported with a
durability warning rather than falsely reported as an uncommitted failure.
Cancelled/failed writes can leave an unreferenced content-addressed evidence copy,
but no source decision is inferred from that copy.

## CLI contract

```text
seam_voicebank_cli register-source WORKSPACE PROJECT_SHA256 ID human|procedural|tts pass|blocked|not-assessed SOURCE_USE TRANSFORM REDISTRIBUTE COMMERCIAL LICENSE LICENSE_SHA256 PRODUCER UTC
```

Each permission argument is exactly `yes` or `no`, in the order shown. The
rights outcome is a producer declaration, not legal verification. A `pass`
declaration without source-use/transformation permission still cannot execute.
An unassessed or blocked source can be persisted honestly but cannot execute.
The initialization receipt supplies the project digest for an empty Draft;
subsequent operation receipts supply the next expected digest.

Example with placeholders, not an authorization declaration to execute:

```text
seam_voicebank_cli register-source /path/to/workspace <current-project-sha256> my-original-source procedural pass yes yes no no /path/to/authorization.txt <file-sha256> <registered-producer-id> <UTC-timestamp>
```

The result names `SourceRegistered`, the committed generation/project digest,
durability status, Not assessed musical outcomes and `releaseEligible: false`.
Registration does not acquire any rights that the producer does not actually own.

## Verification scope

- Three new repository regressions exercise source-free Draft registration,
  retained evidence/recovery, stale snapshots, wrong actors/digests, musical
  self-promotion, cancellation, changed evidence, honest unassessed persistence,
  mixed-source provenance and attempted policy replacement.
- The existing four-case CLI suite now starts without a source and registers
  it through the real executable before import, quality recording, editable draft,
  unit review and publication. Its full journey packages/installs and renders a
  reopened new score without the original producer inputs.
- A new Python test initializes and registers through the actual C++ CLI, then
  validates its journal/workspace through Python. Invalid permission text and
  replaying a stale request leave project bytes unchanged. Python parity/admission:
  **20 tests passed**, 6.851 seconds.
- Full Release build passed. Final focused CTest: **5/5 PASS, 23.61 seconds**.
  Core: **835/835**; producer: **45/45**; source-aware Draft: **12/12**;
  CLI workflow: **4/4**; Studio draft/source workflow: **30/30**.
  Results and counts are retained in
  `evidence/source-registration-2026-09-09/`; these are engineering fixture tests,
  not an actual singer/source or full release qualification.

CLI SHA-256: `6a0cab9b7f246a74903e6003be83ef8263f38ad2878c1909e3148177ab8c4f0e`.

## Preservation and remaining work

Comparison against `session-preservation-toeVko` found no missing captured files.
Existing dirty work was preserved. No staging, commits, pushes or real source
authorization were performed. The registration test declarations are synthetic.

Native setup forms, operator management, selecting an already registered source,
legacy attribution, language/style-aware assignment migration and unit-kind QC
remain open. This source-registration increment does not replace any required
Voice Designer, classical/neural singer, host, independent music or release work.
