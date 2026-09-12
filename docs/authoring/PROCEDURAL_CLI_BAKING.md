# Procedural candidate baking from the CLI

The CLI now exposes the same Final candidate export operation used by native authoring:

```text
seam_voicebank_cli bake-project PROJECT OUTPUT_DIRECTORY [SAMPLE_RATE]
```

The default sample rate is 48,000 Hz. Explicit rates must be integers from 8,000 to 384,000; the recipe/DSP must also support the chosen rate. Every nonempty vocal track must have a saved procedural-recipe reference. Relative recipe paths resolve against the source project file's directory, not the CLI working directory. Missing/mismatched recipes, unsupported articulation, invalid timing and existing output destinations reject rather than silently falling back or overwriting.

The command bakes every nonempty procedural region, including muted material, under the export service's existing 256-candidate and aggregate-note limits. It emits the same candidate WAV/metadata, copied source project, frozen recipes and transactional receipt as candidate export. It does not produce master/stem audio. Vowel-only material uses candidate v1; articulated vowel/frication material uses v2. Successfully committed output is reported as JSON with `state`, `approval`, `candidates` and `receipt`.

The input project is not edited. Output is unapproved material, not an installed singer or an approved take. Existing nonprocedural media references in the copied project are not magically made portable. Generated files still need review and the appropriate source/rights evidence.

## Producer ingestion

After inspection, use the existing canonical producer import operation:

```text
seam_voicebank_cli import-procedural WORKSPACE METADATA WAV RECIPE TAKE PROMPT COVERAGE MIDI OPERATOR UTC [SUPERSEDES]
```

This requires an existing appropriately configured procedural producer workspace. It records strict candidate/recipe lineage and enters MarkerReview; it does not grant marker review, pitch review, source-strategy qualification or approval. Do not fabricate feasibility or license evidence to make an import pass.

## Recovery and U21 boundary

Batch execution is available as `run-generation-batch BATCH_JSON BATCH_SHA256 [MAX_TOTAL_FRAMES]`. It validates a digest-bound list, a shared producer-state expectation, unique targets and aggregate frame limits before rendering, then reuses individually verified completed outputs on retry. Use `import-generated-batch WORKSPACE BATCH_JSON BATCH_SHA256 OPERATOR UTC [MAX_TOTAL_FRAMES]` for one atomic producer collection; do not independently import siblings and silently refresh their stale expectations. Complete retries use read-only `AlreadyCollected` recognition, while partially collected batches reject for explicit reconciliation. See `docs/implementation/GENERATION_BATCH_EXECUTION_2026-09-07.md`.

### Preparing and running a frozen assignment job

```text
seam_voicebank_cli prepare-generation WORKSPACE PROJECT TRACK_ID REGION_ID TAKE_ID JOB_ID JOB_DIRECTORY
seam_voicebank_cli run-generation JOB_DIRECTORY MANIFEST_SHA256
```

Preparation recovers the producer workspace and requires exactly one assignment with the requested `plannedTakeId`. It derives coverage, prompt, pitch layer and active superseded take from that assignment. The take ID must be new, and the selected procedural source strategy must satisfy the existing readiness checks. The score track/region IDs use canonical nonzero 16-character lowercase hexadecimal notation. The region must belong to the selected track, which must have a saved hash-bound procedural recipe. Relative recipe paths resolve against the score file's directory.

The score's sample rate must be an integer from 8,000 to 384,000 Hz; no floating-point truncation occurs. The region's nominal pitches must match the assignment layer. Preparation publishes a new frozen job package and returns JSON with `state: Prepared`, job ID and manifest digest. It does not reserve or mutate the producer assignment. Retain that returned digest and pass it to execution. Existing job directories reject without replacement.

The POSIX integration fixture executes prepare → run → guarded collection as actual CLI processes, then recovers one unapproved MarkerReview take with the expected typed gestures. This does not prove batch scheduling, Windows execution, native orchestration or process-kill recovery.

### Guarded collection

Generation tooling that has retained an original expectation file and its digest can collect a result with:

```text
seam_voicebank_cli import-generated WORKSPACE METADATA WAV RECIPE EXPECTATION SHA256 OPERATOR UTC
```

The command requires exactly these arguments. It verifies the expectation bytes, recovers the current workspace, verifies the recipe and submits to the same canonical importer with the retained expectation. Target take, prompt, coverage, pitch layer and superseded take come from the file, not fresh command-line overrides. Stale state, mismatched output and invalid digest reject. Success reports MarkerReview and the resulting generation; it grants no approval.

A repeated call now returns `AlreadyCollected` only when durable lineage proves collection of that exact original expectation. It reports the take's current state and whether it remains active, without duplicating it, restoring an old assignment or resetting manual edits/review. This read-only path does not need disposable staged metadata/audio/recipe files and does not claim to validate newly supplied staging paths. A colliding take without matching generation proof rejects. Fresh imports still enforce the original state expectation. See `docs/implementation/GENERATION_IMPORT_EXPECTATIONS_2026-09-07.md` for the integrity boundary.

`bake-project` is a bounded project-to-candidate command, not the completed resumable assignment-generation system. Rerunning into an existing destination fails without replacement. Export publication uses the existing transaction machinery, but this command does not claim automatic restart/resume of per-assignment work after process interruption.

U21 still requires immutable job requests tied to inventory/assignment and expected producer generation, worker output budgets, cancellation, verified partial-output reuse, stale-result rejection and canonical current-result publication. Native job management must use those same operations. The CLI baking entry point does not satisfy those requirements by itself.

## Verification

The POSIX integration test launches the actual CLI, resolves a relative recipe from a temporary saved project outside the working directory, strictly loads the generated mixed candidate, and compares its PCM/typed markers with the library export result. It checks source-file preservation, refusal to overwrite, malformed rates and nonempty tracks without recipes. Windows process execution and signal-interruption recovery are not qualified by this test.
