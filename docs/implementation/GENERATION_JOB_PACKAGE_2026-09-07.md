# Frozen generation job package

Status: library preparation, reload, worker execution, verified output reuse and producer-history recognition are implemented. Batch execution/collection details are in `GENERATION_BATCH_EXECUTION_2026-09-07.md`; in-app preparation and full U21/U22 qualification remain incomplete.

## Shared saved-score preparation

An explicit optional `GenerationRecipeSelection` now supplies an immutable resource and style for preparation from a Designer draft. It is owned by value through the native preparation worker. When supplied, preparation validates that resource/style and changes only the decoded score copy's selected-track reference to the package's `recipe.json`; no source song or external recipe file is written. A score without an existing procedural reference can be used only when this explicit selection is provided. The original saved-score path remains unchanged when no selection is supplied.

Score digest checks still precede decoding, and the original producer expectation/pitch/assignment checks still govern package creation and later collection. This is a new explicit request, not recapture of an already-prepared job. The new package owns the selected recipe bytes and records their identity in its expectation; changing the Designer afterward cannot change that package.

Integration tests prepare/reload/render with changed Designer phonation, require its exact recipe hash and different PCM, and verify byte-for-byte source-score preservation, unchanged original recipe identity and unchanged producer state. Wrong styles and score digests reject without creating a job; a plain score works with explicit selection and rejects without one. The native async preparation test also verifies the selected resource survives worker handoff. Strict Debug/Release export suites pass (7.03/2.98 seconds), the Release Studio builds and diff checks pass. A visible Designer-to-preparation action and the corresponding UI lifecycle remain unfinished.

`prepareGenerationJobFromScore(directory, jobId, scorePath, trackId, regionId, producer, plannedTakeId)` now owns the saved-score preparation logic used by the CLI. It requires valid identities and exactly one planned producer assignment; resolves the score absolutely and its recipe relative to the score; verifies the saved recipe identity; validates an integral 8,000–384,000 Hz sample rate; and creates a full Final procedural snapshot for the specified track/region/style. It then delegates to the existing immutable package writer, preserving pitch-layer, producer-readiness and expectation checks. No assignment reservation, producer mutation or approval occurs.

The original `prepare-generation` CLI syntax is unchanged and delegates to this operation. Direct library tests verify relative recipe resolution, exact recipe/producer identity, unchanged producer state, no overwrite, wrong-region/unplanned/ambiguous-take rejection before directory publication, changed-recipe rejection, and independence of previously packaged bytes from later source recipe edits. Actual CLI lifecycle regressions remain enabled. This shared entry point is preparation infrastructure for Studio, not a claim that its in-app score/region selection workflow is already implemented.

`prepareGenerationJob(directory, jobId, snapshot, producer, take)` captures one full Final procedural region and its original producer import expectation. Nominal score pitches must match the requested pitch layer. Existing candidate frame limits apply. A job ID is 1–128 ASCII letters, digits, hyphens or underscores. The directory must be new and its parent must exist; preparing over an existing directory rejects without replacement.

The package contains fixed filenames:

- `project.json`: frozen renderer-local project, bounded to 16 MiB.
- `recipe.json`: exact verified frozen recipe bytes, bounded to 1 MiB.
- `expectation.json`: original generation-import expectation, using its strict bounded format.
- `job.json`: manifest published last with new-file-only durable atomic writing.
- `job.seamjob`: native-open reference carrying `directory: .` and the original manifest digest, written before final manifest publication. It is not included in the manifest hash, avoiding a circular digest; job validity still depends on the final manifest and retained digest.

The manifest format is `com.project-seam.generation-job`, schema version 1. It has exactly seven fields: `formatId`, `schemaVersion`, `jobId`, `projectSha256`, `recipeSha256`, `expectationSha256`, and `sourceProjectId`. The source project ID is a canonical nonzero uint64 decimal string, retaining the original source identity separately from the normalized renderer-local project. The returned prepared job includes the manifest SHA-256; its owning job registry must retain that digest.

`loadGenerationJob(directory, expectedManifestSha256)` reads at most 4 KiB of manifest, verifies the retained digest, checks exact version/shape/IDs/hashes, and verifies the bounded input files against that manifest. It reconstructs a single-track/single-region Final snapshot from the owned project and recipe bytes, using the retained style and sample rate. Render content hash and exact frame count must match the original expectation; incompatible compiler/DSP changes reject rather than silently reinterpret the request. The snapshot restores the original source-project ID. Saved external recipe paths in project metadata are not opened: the verified packaged recipe is supplied directly.

Preparation does not render PCM, assign a take or approve material. It does not change the producer model. Callers should capture a recovered/saved producer state; unsaved state cannot be silently refreshed when the result is collected. Manifest publication is the package commit marker, not proof of completed generation. A failed/interrupted preparation can leave an incomplete directory, which is preserved rather than overwritten or treated as a ready job. Cleanup/retry of incomplete preparation and process-interruption qualification remain future work.

Tests prepare and reload a real mixed frication/vowel Final snapshot, retain source identity and expectation, and render exactly matching PCM from the reloaded job. Existing-directory preparation, wrong manifest digest, changed score bytes and missing recipe reject. Restoring the exact files restores successful loading. These checks prove request reconstruction, not job scheduling, completed-output reuse, source qualification or listening quality.

## Worker output and completed-output reuse

`runGenerationJob(directory, expectedManifestSha256, stopToken)` reloads the frozen request and takes a nonblocking OS-backed `.worker.lock`. The lock file is persistent and never unlinked; existence alone does not indicate an active worker. Competing cooperative workers reject while ownership is held; process exit releases the OS lock. Windows uses exclusive file sharing and reparse checks; POSIX uses `flock` with regular-file and no-follow checks. Current runtime evidence is macOS, not Windows qualification.

Under that lock the worker invokes the existing export recovery path for `output/`. If no published output exists, it renders through the existing Final candidate export transaction using the packaged recipe directly. Existing output must pass the committed receipt's file/digest checks. Both new and reused output then pass strict candidate loading and match the original style, render identity, rate, exact frame count, score origin and projected typed markers. Corrupt output rejects; it is not automatically replaced. A successful return supplies candidate paths, audio digest and a `reused` flag. Reuse avoids rerendering but still verifies/decodes the output.

Cancellation is checked before work and between phases and passed to rendering/loading. Export publication retains its existing non-interruptible commit boundary. Cancellation observed after output publication can leave valid output for a later verified reuse; no producer assignment occurs here. Recovery inherits the export transaction implementation, but process-kill tests for this combined worker are still required. Partial staging without a recoverable journal may remain and reject rather than being silently erased or declared complete.

Tests cover pre-cancelled work without output, live lock contention, first publication, repeated reuse with unchanged audio modification time, corruption rejection without replacement and reuse after restoring exact bytes. The actual POSIX CLI also rejects while another process holds the worker lock and successfully reuses verified output without rewriting it. The producer still has no takes after worker execution. These are structural/integrity checks, not authenticated provenance or acoustic qualification.

CLI execution is available as `seam_voicebank_cli run-generation JOB_DIRECTORY MANIFEST_SHA256`. It returns JSON with candidate paths, audio digest, `reused` and `approval: unapproved`. It does not prepare a job or collect output into a producer assignment. SIGINT/SIGTERM now request cancellation through the CLI bridge described below; broader process-kill/recovery qualification remains unfinished.

CLI preparation is now available as `prepare-generation WORKSPACE PROJECT TRACK_ID REGION_ID TAKE_ID JOB_ID JOB_DIRECTORY`. It recovers the producer, resolves exactly one planned assignment and a saved recipe, validates integer sample rate and prepares the frozen Final job. Expectation capture now rejects an unready/nonprocedural strategy or an already-existing take ID. The actual POSIX CLI integration test prepares, runs, collects and recovers an unapproved take; unknown planned takes, existing directories and duplicate take IDs reject.

Next: native execution and registry integration; actual process-interruption recovery tests; verified per-assignment batch reuse; recognition of an already-committed producer result. Preparing a job does not reserve a producer generation, so unrelated durable changes still make its original expectation stale. U21 and full Beta GO remain open.

## Abrupt publication-exit verification

A dedicated test-only executable now calls the real job worker and exits via `std::_Exit(86)` at each export publication phase: JournalPrepared, PreviousMoved, DestinationPublished, ReceiptCommitted and BackupRemoved. The parent observes that exact terminal exit before rerunning the same prepared job. No worker destructors run in the child; successful reacquisition therefore exercises OS lock release on process exit.

For the first three phases, recovery rolls back journal-owned uncommitted output and the worker regenerates it. For the last two, the committed output is verified and reused. Every recovered WAV digest equals the uninterrupted reference; another rerun reuses it; no producer take is assigned. This covers the job worker plus existing export transaction together, rather than testing only the exporter in isolation.

The fault callback is an optional library testing seam, not a CLI environment-triggered crash option. A nonterminating callback that requests interruption must yield a worker error: the worker now explicitly requires `ExportState::Committed` before returning newly generated output. A regression test covers that requirement at DestinationPublished.

These are actual separate-process abrupt-exit tests on POSIX/macOS, not power-loss, arbitrary mid-write SIGKILL, pre-journal staging, Windows runtime or CLI signal-cancellation qualification. Those boundaries, native/batch orchestration and committed producer-result recognition remain open; U21 is not accepted by these tests alone. Earlier references above to missing combined-worker interruption tests are superseded only for these five journaled phases.

## CLI signal cancellation

`run-generation` installs scoped SIGINT/SIGTERM handlers. The handler only records the signal using an always-lock-free atomic integer; a normal watcher thread requests stop, keeping stop callbacks and other non-signal-safe work out of the handler. Previous handlers are restored on scope exit. Handler/thread setup failure prevents work from starting. The CLI reports cancellation and returns `128 + signal` (normally 130/143); a fully committed output can remain for subsequent verified reuse.

The dedicated process probe uses that same bridge and raises actual SIGINT before work and SIGTERM at JournalPrepared. Tests observe no output for the first case, valid reusable committed output for the second, identical reference audio after retry and no producer assignment. This verifies the shared bridge and worker at deterministic boundaries, not interactive terminal keyboard behavior, arbitrary signal timing, Windows delivery or cancellation of other CLI commands. The earlier signal-cancellation gap is narrowed to those remaining boundaries. U21 and Beta GO remain incomplete.
