# Studio donor import receipt boundaries

Source base: `156b9c1602dd793badec3ed53c1afb5ab2e6def8`. This unit addresses the missing controller coverage identified in the exact `a7f81e07` independent review. The original Studio implementation was approved for engineering with one P3; its report incorrectly described capture-worker cancellation as precommit import cancellation. That wording was corrected in the preceding documentation commit. **The receipt P3 is now locally addressed; independent review of this follow-up remains pending.**

`VoicebankStudioController::beginPreparedDonorImport()` now accepts the existing `PreparedDonorImportOptions` as an optional third argument and moves it into its worker. Production and modal callers leave it empty. This exposes the repository's existing deterministic prepublication checkpoints to controller tests, without adding global hooks or a callback after publication starts. The receipt-adoption implementation itself is unchanged.

Three new native cases exercise four forms against real temporary repositories and synthetic notices/audio:

- At `NoticesRetained`, pause the importer, request cancellation, then release it. Import returns the precise cancellation conflict; recovery, current pointer and the controller's project remain unchanged. The captured inspection remains available. A separate explicit retry commits one unapproved take. Notice/audio staging on the refused attempt is permitted; an unchanged entire file tree is not claimed.
- Wait until recovery observes the published generation, then request cancellation before UI adoption. Polling retains the actual receipt and adopts the matching committed project. This observes cancellation after publication/before UI adoption; it does not inject a stop between individual journal writes.
- While paused before publication, replace the mutable editor manifest with a valid newer model. Run once normally and once with a real directory obstruction at `project.json`. Both forms retain the exact recovered generation/hash receipt and preserve the newer editor model and prior controller project. The second form exposes the original unconfirmed-durability diagnostic; removing the obstruction and reconciling the exact receipt restores verification without another import. The old capture cannot be reused.

The context-change fixture uses the public mutable editor model, not a selection action while the UI is busy. Imported material remains `MarkerReview` with no source-quality or take-review approval. Every receipt is checked against full recovered project SHA-256 and generation. The tests verify existing receipt-before-discard behavior; no previous runtime defect or failing-before regression is claimed.

| Verification | Result | Wall time |
| --- | --- | ---: |
| Full Release dependency build | PASS; changed library/header dependents rebuilt | 50.352 s |
| Release CTest | 80 native cases: sample review 14, manifest draft 51, actual app 15 | 11.477 s |
| Focused ASan/UBSan dependency build | Manifest-draft target and its dependencies rebuilt | 27.043 s |
| ASan/UBSan CTest | 51 manifest-draft native cases PASS | 30.249 s |

The builds/runs overlapped; timings are observations, not benchmarks. ASan used `detect_leaks=0:abort_on_error=1:strict_string_checks=1`; UBSan used `print_stacktrace=1:halt_on_error=1`. Leak detection, TSan, a complete suite and a complete sanitizer build were not run. Actual app tests use scripted dialogs and silent synthetic audio. No live AppKit, screen-reader, listening, rights or Beta acceptance is supplied.

Frozen root: `out/evidence/studio-donor-receipt-boundaries-2026-10-15`. Its manifest is **7,308 bytes**, SHA-256 **`61e023549b49c8fa3bfb377672557945009abf258b9dd8ac7659d5f383a5755d`**, binding **27 artifacts and 10 pins**. It retains the exact three-source patch/snapshots, full logs/command receipts, toolchain/build configuration, generated build identities and four stable binary copies. Sources and binaries match before/after both lanes. The read-only verifier checks exact identities, the four new traces and case counts without executing retained tests or acquisition code. The [evidence index](STUDIO_DONOR_RECEIPT_BOUNDARIES_2026-10-15.evidence.json) binds the frozen manifest and original review.

Live AppKit verification remains blocked by the locked Mac. Real source admission, phonetic/pitch policy, failed-QC disposition, producer/distinct reviewer, listening and installed product/release obligations remain open. The unchanged canonical records remain Alpha **0/20**, U45 **HELD**, Beta **BLOCKED**; the full goal stays **ACTIVE**. No downloaded recording was admitted. Existing stash is preserved; `.github` is untouched and Windows remains TODO.
