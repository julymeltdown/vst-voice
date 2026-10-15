# Exact publication identity for source assessments

Source base: `b9849150b35451cc866cdc198f1df855fb7e4218`. The exact `b3998f99de1c63fa3e1479776d2af60095aa19a0` repair is independently **APPROVED for engineering with zero new findings**, closing the separate pre-existing source-assessment P3 found in the `029cc6a9` review. The originating import/style and Studio receipt-test P3s remain independently closed.

A schema-2 producer can hold two source strategies and a take approved through the software review APIs under the first strategy. Registering a second source and importing its take preserves that first approval. The first quality assessment raises the project to schema 3. Its draft explicitly lowers approvals under the assessed strategy; publication's review transition also reopens the first source's approval because the schema belongs to every take's review basis. If the current-pointer write then fails, the old writer compares recovery with its pre-transition draft and returns “recovery found different work” for its own committed assessment.

`source_assessment.cpp` now uses `saveWithAttempt()` and adopts recovery only when its generation and complete encoded SHA-256 exactly match that local publication attempt. It no longer normalizes recovery to the earlier draft's generation. Existing durability diagnostics and exact reconciliation requirements remain. A new optional `SourceQualityAssessmentOptions::beforePublication` hook runs only after evidence retention and before save, for deterministic fault tests; production callers leave it empty. No global hook or callback after publication is introduced.

## Actual regression and limits

One new native case runs normal and pointer-obstructed forms with two real temporary source bindings, a genuine earlier **software** approval, synthetic audio/declarations and a distinct synthetic reviewer. It checks schema 2→3, generation +1, full receipt/caller/recovery hash agreement, reopening of the unassessed source's earlier approval, cleared flags, unchanged review JSON, one recorded assessment, exact pointer reconciliation and duplicate refusal without another generation. No human judgment is represented.

Before changing recognition, the same test and deterministic hook were compiled against the old comparison. The normal form passed; the pointer form printed the exact misclassification and failed `CHECK(result)`: native exit 1, 0 passed/1 failed, 0.444 s. This before-repair binary **includes the new test hook**; it is not a pristine parent binary. Retained snapshots and the read-only verifier prove that removing only that hook/signature restores the exact base `source_assessment.cpp`. The test and header bytes are identical before and after the recognition repair. No fixture setup failure is used as bug evidence.

| Verification | Result | Wall time |
| --- | --- | ---: |
| Focused before-repair build | Rebuilt the one executed native target and dependencies; PASS | Untimed |
| Full fixed Release dependency build | PASS; all targets/dependents refreshed | 66.452 s |
| Release CTest | 140 cases: producer 65, review transitions 24, Studio manifest draft 51 | 6.211 s |
| Focused ASan/UBSan dependency build | Producer + review-transition targets/dependencies rebuilt | 20.235 s |
| ASan/UBSan CTest | 89 cases: producer 65 + review transitions 24 | 87.695 s |

Builds and runs overlapped; timings are observations, not benchmarks. ASan used `detect_leaks=0:abort_on_error=1:strict_string_checks=1`; UBSan used `print_stacktrace=1:halt_on_error=1`. No complete suite, complete sanitizer build, new leak, TSan, OS-fault or power-loss result is claimed. Actual filesystem pointer obstruction is not fsync fault injection.

Frozen root: `out/evidence/source-assessment-publication-2026-10-15`. Manifest **9,111 bytes**, SHA-256 **`22d7803232116cbef388f7471130bacb02a42b2a5d9cff210e2b1cfc1e10d02e`**, **35 artifacts/12 pins**. It retains the exact three-source patch, before/final source snapshots, instrumented old-logic binary/failure, five stable final binaries, build/test receipts/logs and source stability records. The [evidence index](SOURCE_ASSESSMENT_PUBLICATION_IDENTITY_2026-10-15.evidence.json) binds these and the original review. Its verifier reads/hashes records and committed blobs without rerunning captured native code.

The predecessor `7ec877f7` review independently APPROVES the Studio receipt coverage with zero new findings and closes the a7 P3. Its exact final answer is retained at `out/evidence/studio-donor-receipt-independent-2026-10-15/review-7ec877f7.md`: **9,424 bytes**, SHA-256 `4b3dbba606539f2ebe3c2cbce4d9ff409eeaeb8a5d9651f29ed60fe2fda3b2c6`; metadata **471 bytes**, SHA-256 `3776752005532dddf17f7f2064f4da9a7e9df39c7ff92450cba3c488ce9295ac`. This was a read-only source/evidence review, with no reviewer build, native/test/script execution or playback.

Live AppKit remains blocked by the locked Mac. The downloaded sources remain unadmitted; no original audio, human/rights/listening approval or acceptance record changed. Alpha **0/20**, U45 **HELD**, Beta **BLOCKED**, full goal **ACTIVE**. Other writers' optional hardening and wrapper faults retain their separate scope. Stash preserved; `.github` untouched; Windows remains TODO.

## Exact independent closure

The reviewer read the committed source, before/after regression evidence, logs and bindings without rerunning builds, native binaries, tests, verifier scripts or playback. The exact final answer is retained at `out/evidence/source-assessment-independent-2026-10-15/review-b3998f99.md` (**8,711 bytes**, SHA-256 `e543addc9ce419e0b53fe9f3bfc4fd59c94e2d70b7c2e74195172fd8853dfb18`); metadata is **531 bytes**, SHA-256 `62e02e7dbf6ff34439b5f1504d8f6f0e07efa7243ba6ef56f01cd91858a334a6`. The original source-assessment P3 is **CLOSED**. Optional product pointer reconciliation and other writer hardening remain separate work; this verdict conveys no source, human or release approval.
