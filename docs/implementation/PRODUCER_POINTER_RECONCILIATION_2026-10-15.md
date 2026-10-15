# Shipped producer pointer reconciliation

Source base: `c7e451970bb15f4af3e77f8a6829e8836f968296`. Local implementation and focused checks pass; exact independent review of this unit is pending. The prior source-assessment P3 is independently closed by `b3998f99` and is separate from this product action.

## User-visible recovery

An import or source assessment can publish its immutable journal/generation successfully and then fail to publish `project.json`. Its receipt then correctly says durability is unconfirmed. Repeating the import risks duplicating work. Previously only the repository API exposed exact pointer reconciliation; neither shipped Studio nor CLI offered the action.

Studio now exposes **RECONCILE PROJECT POINTER** in unit review. Reopen the matching producer, inspect its recovered project, and activate the action. Reopening already recovers immutable generations without requiring a valid current pointer. Consequently recovery works after an app restart and does not depend on retaining an earlier modal or in-memory donor receipt. Dirty editor changes need not be saved by this metadata operation; a valid editor context is captured and the model is preserved.

The CLI now exposes:

```text
seam_voicebank_cli reconcile-producer-pointer WORKSPACE GENERATION PROJECT_SHA256
```

Use the exact generation and full SHA-256 from the confirmed commit receipt, not a guessed generation. Generation must parse completely as a nonzero uint64; the digest must be 64 lowercase hexadecimal characters. Failure returns exit 2 with `Pointer reconciliation refused:`; signal cancellation retains the existing 128+signal convention. Success emits `ProductionPointerReconciled`, the same generation/hash, `pointerReconciled: true`, and `generationAppended`, `nativeImport`, `sourceRightsApproved`, `releaseEligible` all false. A completed reconciliation reports the state at that operation; it does not promise that no subsequent writer can advance the producer.

## Code and invariants

| Location | Responsibility |
| --- | --- |
| `libs/seam-native-ui/src/voicebank_studio_donor_intake.cpp`, `beginProductionPointerReconciliation` | Capture the current producer's normalized workspace, project ID, generation, full encoded hash and editor context; call the repository from an asynchronous worker. No source packet or import is read. |
| `libs/seam-native-ui/include/seam/native_ui/voicebank_studio.hpp`, `ReconciledProductionPointer` | Preserve a separate result bound to its owning workspace/project and exact generation/hash. It does not overwrite historical donor, registration or assessment receipts. |
| `libs/seam-native-ui/src/voicebank_studio_sample_review.cpp`, `pollSampleReviewWork` | Retain completed durable reconciliation before generic stale/cancel discard. Preserve the editor and producer model; if context changed, report completion for the original snapshot and advise reopening. |
| `libs/seam-native-ui/src/voicebank_studio_production_view.cpp` | Three nonoverlapping controls on the existing donor row; separate details identify the last reconciled project/workspace and exact state. No additional vertical row. |
| `apps/seam-voicebank-studio-native/studio_app.cpp`, `sampleReviewAction` | Route the accessible action before modal construction, stop audition, and clear an older bank handoff status when recovery starts so it cannot mask this operation. |
| `apps/seam-voicebank-cli/source_intake_commands.cpp`, `reconcilePointer` | Strict argument validation, existing signal cancellation, repository invocation and metadata-only JSON receipt. Existing donor command diagnostics stay unchanged. |
| `libs/seam-voicebank-production/src/repository.cpp`, `reconcileCurrentPointer` (unchanged) | Under the existing writer lock, require the highest occupied generation/journal number to equal the requested generation; recover and hash the complete project; require exact identity; check cancellation before durable pointer publication. |

There is no new generation, take, assessment, source binding, permission grant or review transition. The action refuses stale identities and newer/incomplete work rather than rolling back. It does not remove a filesystem obstruction. An operator must first inspect and resolve the actual obstruction or I/O problem; the product only repairs a pointer when the existing repository safety checks succeed. Historical unconfirmed receipts remain historical facts; a separately bound successful reconciliation reports later durability.

## Executed checks

Two new controller cases cover actual pointer obstruction/refusal, successful exact repair, preservation of the original unconfirmed donor receipt/diagnostic, late cancellation after durable publication, a valid newer editor model, repeat reconciliation without a new generation, and refusal of an older open producer after another import advances it.

The existing actual-application donor case now destroys its first app instance, obstructs `project.json`, opens a fresh app instance, recovers the producer, drives the accessible reconciliation action, observes refusal, removes only the test-created empty directory and successfully repairs the same committed bytes. Its single take remains MarkerReview, with no source assessment or review added. Dialogs and audio devices are scripted/silent OS substitutes; this is actual app routing and rendering, not live AppKit or human acceptance.

A new Python method invokes the actual native CLI. It verifies malformed/overflow generations and invalid/mismatched digests refuse without workspace mutation; obstruction stays untouched; exact repair restores the entire original workspace snapshot; a repeat is idempotent; a later real import makes the old receipt stale; the independent Python producer reader and native source-quality reader accept the repaired/newer workspace without modifying it.

| Check | Result | Wall time |
| --- | --- | ---: |
| Initial full Release dependency build | PASS, refreshed all consumers | 44.915 s |
| Full Release build after correcting only the unexecuted app test's status selector | PASS, rebuilt app test | 6.897 s |
| Three Release CTest lanes | **82 distinct cases**: 14 sample review + 53 manifest draft + 15 actual app | 11.258 s |
| Python tests invoking native CLI | **7 methods**, PASS | 2.379 s |
| Focused ASan/UBSan dependency build | Two controller targets/dependencies rebuilt | 23.833 s |
| ASan/UBSan controller CTest | **67 cases**: 14 + 53, PASS | 38.727 s |
| Final full Release build after clearing stale bank status on the new app action | PASS, rebuilt all app consumers | 7.321 s |
| Final actual-app CTest rerun | **15 cases**, PASS; not counted twice in 82 | 8.479 s |

Builds and tests overlapped; times are observations, not benchmarks. Sanitizers used `ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:strict_string_checks=1` and `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`. Only `studio_app.cpp` changed after the earlier runs; the exact initial snapshot and final adjustment are retained. All controller/CLI consumed sources stayed identical, and the affected app lane was rebuilt/rerun. Seven final binary hashes stayed stable around that rerun. Executable copies are not retained; their identity records and run logs are retained. Both generated version headers identify source base `c7e45197`, not the later implementation commit.

## Evidence and limits

Frozen root: `out/evidence/producer-pointer-reconciliation-2026-10-15`. Manifest **12,828 bytes**, SHA-256 `1834e33f72104d15d1a972561fb08a5ead585aafa5f364e1fd13c6207a77a04d`, **57 artifacts / 7 preserved bindings**. The [evidence index](PRODUCER_POINTER_RECONCILIATION_2026-10-15.evidence.json) records exact commands, timings, source hashes, capture and verifier identity. The read-only verifier checks the exact committed Git blobs and source patch without launching builds, tests, native binaries or playback. An initial manifest setup referenced the wrong dossier directory; discovery corrected that path before the manifest was frozen. This was not a runtime failure or product-test result.

The final [720×520 harness capture](../../out/evidence/producer-pointer-reconciliation-2026-10-15/producer-pointer-reconciled-after-reopen-720x520.png) was visually inspected: the three donor/recovery controls fit without overlap and the reconciled status is visible. It uses the fallback font. Live AppKit remains unverified because the Mac was locked; the existing unlock question is still pending. No full suite, leak detection, TSan, OS/power-loss/fsync injection, physical recording, DAW, Windows, signing or listening result is claimed. The test's directory obstruction is not a power-loss/fsync test.

All **1,469 downloaded originals remain unadmitted**. This unit does not resolve performer/rights/EULA disposition, genuine producer/reviewer roles, phone mapping/missing phones, pitch/style suitability, failed-QC policy or human listening. The [current source handoff](FEMALE_SOURCE_CURRENT_HANDOFF_2026-10-15.md) remains the source-admission entry point. Alpha **0/20**, U45 **HELD**, External Beta **BLOCKED**, full goal **ACTIVE**. Stash preserved; `.github` untouched; README Windows TODO unchanged.
