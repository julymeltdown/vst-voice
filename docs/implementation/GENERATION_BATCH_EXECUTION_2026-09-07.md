# Bounded generation batch execution

Status: manifest assembly, execution/reuse, repository atomic collection, CLI collection and native assembly/execution wiring implemented. Durable per-job queue and broader qualification remain unfinished.

## Native batch assembly

Shift+B opens an AppKit multi-selection panel for 1–64 prepared job folders or `.seamjob` reference files. Selecting a folder resolves its `job.seamjob` child, allowing sibling prepared packages to be selected together without moving references. A second dialog chooses a new batch JSON destination. Empty/cancelled selection is a no-op. Recording/busy guards, audition stop and captured workspace epoch/generation/selection checks run across the modal boundaries. Plain B retains its pitch-view action; Cmd/Ctrl+Shift+B retains batch execution.

`beginGenerationBatchAssembly(references, destination)` owns reference loading and manifest preparation on the existing Studio worker. It verifies the captured model still matches recovered durable state and supplies that original state hash to `saveGenerationBatch`, which rejects jobs prepared for another state. Successful assembly reports `BATCH PREPARED / NOT GENERATED` without replacing the model or clearing candidate inspection/undo state. It does not reserve assignments or automatically execute the batch. Late external edits can stale the manifest; later execution retains the original expectations rather than recapturing.

Controller tests verify assembly/reload, busy save guards, duplicate rejection, unchanged visible/durable producer state and externally changed workspace rejection without manifest creation. The shared writer has explicit expected-producer-hash rejection coverage. Multi-selection and save-panel interaction remain unverified; the AppKit implementation builds, while other platforms explicitly report unsupported multi-selection. Native queue editing, retained registry and wider interruption qualification remain open.

## Assemble prepared jobs without hand-written JSON

```text
seam_voicebank_cli prepare-generation-batch OUTPUT_JSON JOB_REFERENCE [JOB_REFERENCE ...]
```

Select 1–64 existing `job.seamjob` references. The command loads their retained job digests and calls `saveGenerationBatch`, which shares the execution admission checks for valid frozen packages, one producer-state expectation, unique job/take/coverage-pitch targets and aggregate frames. Default admission is 64 jobs and 32 Mi frames. This is assembly of already-prepared jobs, not automatic score-to-inventory coverage generation.

The new manifest stores job paths relative to its destination parent where possible, preserving original digests. Paths are bounded to 4,096 bytes with ASCII control characters rejected; encoded output must fit the loader's 64 KiB limit. A new-file-only atomic write returns its exact SHA-256. Existing manifests are never replaced. Pre-cancelled requests and failed admission do not publish a manifest; no rendering, producer assignment or approval occurs. The package set is not copied or reserved: later execution still revalidates the original identities and expectations. Moving a manifest without its relative job layout can invalidate those paths.

Library and actual CLI regressions verify round-trip references, duplicate/budget/cancellation rejection, no-overwrite with preserved bytes, and absence of output audio/producer takes after assembly. Native batch-building controls now use this shared operation as described above.

```text
seam_voicebank_cli run-generation-batch BATCH_JSON BATCH_SHA256 [MAX_TOTAL_FRAMES]
```

The batch manifest's retained SHA-256 binds the original job list. The v1 JSON object has exactly `formatId: com.project-seam.generation-batch`, `schemaVersion: 1` and a nonempty `jobs` array. Each job has exactly `directory` and `manifestSha256`. Relative directories resolve against the batch file's directory. Input limits are 64 KiB, depth 4, 1,024 JSON nodes, 4,096-byte strings and 256 collection entries; malformed structures, control characters and invalid job hashes reject. This is caller-retained integrity, not authenticated provenance.

`runGenerationBatch` copies the admitted reference list, then loads/verifies every prepared job before starting any rendering. All jobs must carry the same producer-state expectation. Job IDs, take IDs and coverage/pitch targets must be unique. Default limits are 64 jobs and 32 Mi total requested frames; the API permits explicit job limits up to 256 and frame budgets up to 256 times the per-job 32 Mi cap. The CLI retains the 64-job limit and accepts an explicit frame budget. All requested frames count, including jobs whose output may already exist; cached output does not evade admission budgets. Per-job input/renderer bounds remain in force.

Jobs run sequentially through the existing locked worker, export recovery and strict completed-output validation. The batch does not retain all PCM simultaneously. A library progress callback receives completed/total counts after each successful job. Cancellation or a later failure returns an error and leaves completed output on disk. A retry reloads the same digest-bound request and verifies completed outputs before reusing them. The CLI uses the SIGINT/SIGTERM cancellation bridge and reports the output paths, digests and reuse flags only on successful batch completion.

Tests use two planned pitch layers from one initialized producer state. They reject an under-budget request and duplicate references before any output, cancel after the first completed job, then verify first-job reuse plus second-job rendering. Batch-file reload resolves relative paths; subsequent library and actual CLI runs reuse both outputs. Wrong batch digests and malformed/insufficient CLI budgets reject. No producer take is assigned by batch execution.

## Collection boundary

```text
seam_voicebank_cli import-generated-batch WORKSPACE BATCH_JSON BATCH_SHA256 OPERATOR UTC [MAX_TOTAL_FRAMES]
```

The CLI shares prepared-job/batch admission with execution. It recovers the producer and first checks durable collection history. If every original request is already present, it returns `AlreadyCollected` with observed take states/active flags without saving or requiring output folders. A mixture of collected and uncollected requests rejects for explicit reconciliation; a new batch must still match the original producer-state hash.

Fresh collection verifies each existing output under its worker lock using the committed receipt and strict candidate checks, then calls the atomic repository operation. Missing output is never generated as a side effect of collection. Journal-owned recovery may still reconcile an interrupted publication. Success returns `Collected`, count, generation and unapproved status. Prepared input packages and the retained batch digest are required even for history recognition. The collection budget defaults to 32 Mi frames and is capped at 64 times the per-job 32 Mi maximum.

Actual CLI tests collect two pitch layers in one generation, repeat without producer-state changes, and recognize the batch while output folders are temporarily absent without recreating them. A separately initialized fixture with only one original request collected is rejected without changes. The existing-output verifier also rejects an unrendered job without producing audio. Native wiring and dedicated collection crash/race/cancellation qualification remain open.

The execution API deliberately does not collect the batch into the producer. All jobs initially share one producer-state expectation; importing one independently advances that state and can make remaining original expectations stale. The implementation must not silently recapture those requests to bypass the guard. The repository supplies `importGeneratedBatch` for one atomic collection, used by the CLI above. Native orchestration, resource/latency qualification and broader interruption coverage remain open. This is not full U21 or Beta GO acceptance.

## Native Studio batch controller

Batch execution now exposes a coherent atomic progress snapshot with Preflight, Rendering and Collecting phases and completed/total output counts. Owner-thread polling presents `BATCH PREFLIGHT`, `BATCH OUTPUTS n/N` and `BATCH COLLECTING / NOT APPROVED` through the existing native status display. Counts include verified reused outputs; they are not a percentage of producer publication or approval. All outputs may be ready while atomic collection is still pending. The worker owns shared progress state rather than the controller; future collection clears it on success/error, and shutdown cancellation joins before clearing. Very short phases may finish between UI polls. Progress is transient, not a durable queue or job registry.

Tests sample count bounds/coherence during the real batch workflow, require progress removal after success and failure, preserve manual-edit history on retry, and exercise shutdown join/error cleanup without changing the producer model. Phase-by-phase native visual observation and arbitrary-timing cancellation qualification remain open.

Cmd/Ctrl+Shift+B now opens the native “Generate and Collect an Unapproved Batch” JSON picker; plain B retains its prior behavior. Cmd/Ctrl+Shift+I still selects one job. Both generation shortcuts are shown in the intake panel. The app checks recording/worker state, stops audition, captures workspace epoch/generation/selection, and revalidates that context and recording state after the modal dialog. Cancellation returns without starting a worker.

Explicit batch selection establishes an unsigned trust input: the handler reads at most 64 KiB and captures the selected manifest's SHA-256. The worker must reload matching bytes. This does not authenticate a maliciously substituted whole batch, and it does not replace any original job digest, recipe or producer expectation inside the batch. Strict manifest/input parsing remains on the worker. The visible entry uses the default 64-job/32-Mi-frame budget; budget editing and per-job queue UI are not implemented.

The current Release Studio was launched with synthetic input at 720×520 and its 1440×1040 Retina capture inspected. The job/batch shortcut label fits without clipping. Exit was 0; fixture generation remained 4, approved count 0 and recording callbacks/frames 0. Temporary capture: `/private/tmp/seam-studio-job-qa.gvOic0/batch-shortcut.png`. This is rendered-label verification, not successful batch-picker interaction or physical input/playback qualification.

`VoicebankStudioController::beginPreparedGenerationBatch(manifestPath, manifestSha256, maximumFrames, occurredAtUtc)` now runs the shared batch workflow asynchronously. The caller supplies the retained batch digest; no digest is silently recaptured. It shares the single-job busy guard, stop source, shutdown join and owner-thread result adoption. Manifest parsing, input inspection, durable-state checks, rendering and collection run on the worker.

The worker checks the captured Studio model against current durable state and recognizes every original request before generation. Complete history returns an unchanged result, preserving manual marker bounds and undo history. Partial history fails with explicit reconciliation required. A fresh batch requires its original producer-state hash, reuses or generates verified output under the existing worker locks, and publishes through one canonical `importGeneratedBatch` transaction. Successful material remains MarkerReview; no approval is inferred. Cancellation does not discard a successfully committed producer result.

Controller regressions cover two pitch layers in one generation, existing-output reuse plus missing-output generation, incorrect selected-manifest digest, insufficient frame budget, busy selection/save guards, manual-edit-preserving retry, external durable edits and partially collected history. Per-job queue presentation, in-app preparation, native picker interaction and real UI cancellation still require implementation/qualification. It does not complete U21/U22 or Beta GO.

## Atomic repository collection contract

`importGeneratedBatch(project, inputs, event, maximumFrames, stopToken)` accepts at most 64 candidate/recipe/expectation inputs. The event action is `import-generated-batch`. It preflights the caller's original canonical producer-state hash, unique take and coverage/pitch targets, current prompt/superseded-take bindings, new take IDs, operator/timestamp and aggregate expected frame budget. Each candidate then uses the same strict recipe/audio/metadata and expectation validation as single-candidate import.

Imports accumulate in a private draft. Internal deferred staging does not save after each candidate and does not recapture a newer expectation; every sibling remains bound to the same original state. The completed draft is published with one canonical writer-locked, expected-generation save and one batch journal event. Only after success is the caller's model replaced. Each take retains its own procedural lineage and canonical expectation digest and enters unreviewed MarkerReview. Retakes use the existing matching-chain rules.

A later input failure, cancellation before publication or stale durable generation leaves the active producer model/generation unchanged. Content-addressed raw files staged before a later failure can remain unassigned, as in existing single-import behavior; this is not a promise to erase all staged bytes. Publication retains the non-interruptible durable save boundary. Producer saves now reject serialized generations above the recovery reader's 64 MiB limit before journaling.

Tests reject an insufficient aggregate budget, fail a missing second WAV after staging the first, and compare both in-memory and recovered producer state with the original. Successful two-pitch collection advances exactly one generation, recovers both unreviewed takes and recognizes each original request through durable lineage. Repeating this low-level collection rejects stale state without changing it; higher-level orchestration must use read-only collected-result recognition for an already-completed batch. Atomic-collection process-crash/cancellation matrix coverage and CLI/native integration remain required.
