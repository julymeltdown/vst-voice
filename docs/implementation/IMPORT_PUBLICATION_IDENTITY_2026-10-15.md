# Exact publication identity for recoverable imports — 2026-10-15

An import whose current-pointer write fails now recognizes its own recoverably committed generation even when publication reopened older take reviews. Recognition requires the exact attempted generation and complete post-transition project SHA-256. It does not ignore review changes or accept merely similar recovered work. Exact `029cc6a9` is independently APPROVED for engineering, closing the originating `f7d2559c` P3 for all import paths and style migration. A separately identified pre-existing source-assessment writer P3 remains open.

## Failure and root cause

`writeGeneration()` copies the draft, applies the append-only review transition, assigns its durable generation, serializes and hashes it, then publishes journal, generation and pointer. The first prepared import raises schema 4 to 5. Because the schema belongs to the review basis, an earlier software-approved take is lowered to marker review in that published copy; review records remain history. The pre-save draft still contains its earlier approval when pointer publication fails.

Previously, `commitImportedDraft()` changed the recovered generation number back to the draft number and compared full serialized content. Its own post-transition generation differed, so it returned “recovery found different work” without the receipt needed for exact pointer reconciliation. This was a misclassified commit, not lost audio or an authorization bypass. The successful-import control also reopens those reviews; this repair preserves that existing policy. Studio already warns before import.

## Publication contract and code

- `repository.hpp` adds transient `ProductionPublicationAttempt { generation, projectSha256 }` and a separate `saveWithAttempt()` method. Existing `save()` signatures and caller behavior are retained. The attempt is not serialized and is not a commit receipt.
- `repository.cpp` clears the output before validation/cancellation checks. After all transitions, serialization/ancestry checks and the final cancellation check, it captures the exact generation and full project digest immediately before journal publication. Cancellation or refusal before that boundary leaves no attempt identity. No mutable repository-wide “last attempt” state or cross-call cache is introduced.
- `repository_import.cpp` uses that local attempt when save fails. Recovery must be newer than the original project, match the exact attempted generation and match the complete attempted digest. Only then does it adopt recovery and return the existing recoverable-commit receipt with `durabilityConfirmed=false`. Incomplete publication and unrelated/stale recovered work remain refusals. The shared helper serves raw, prepared, procedural and batch imports.
- `style_migration.cpp` uses the same publication identity rather than comparing recovered post-transition content with its earlier draft. The style-migration authorization, source retention and eligibility rules are unchanged. Existing style-migration tests pass; this unit does not add a dedicated style-migration pointer-obstruction case.

## Regression through the supported English producer path

The native regression uses an actual generated English inventory beside `producer/`, a frozen English vocabulary/stress contract, two styles and two pitch layers. It registers synthetic producer/reviewer IDs and a synthetic source declaration. A generated tone is imported and accepted through `prepareSampleCandidateReview()` and `commitSampleCandidateReview()` before the first prepared import fills the other style. These are temporary software fixtures, not real singer, listening or rights approvals.

The same test source ran before and after the runtime change:

| Form | Original runtime at `cd6464c7` | Repaired runtime |
|---|---|---|
| Normal first prepared import | PASS; older approval reopens and history remains | PASS; same behavior |
| Pointer directory obstruction after notices are retained | Refuses its own commit with the exact “different work” diagnostic; test fails at the expected receipt assertion | Returns generation N+1 and its complete digest, durability false; recovery agrees exactly, history remains and the pointer reconciles by generation/hash |

Both final forms verify that the older take and new take await marker review, prior review JSON is unchanged, the recovered full project equals the caller, and a duplicate attempt adds no files. A separate new shared-writer case proves cancellation, invalid journal input and stale writers clear a prefilled attempt, while successful publication binds the exact generation/digest. Existing shared-import tests retain unrelated-work refusal and earlier journal/generation/pointer boundaries.

The parent execution rebuilt only the modified test target against unchanged parent runtime sources; its identity record proves that only the snapshot test was modified. All 24 earlier cases and the success control passed before the intended pointer form failed. Initial fixture drafts separately hit compile and inventory/range/layer/language-contract guards; those logs are retained as setup diagnostics and are not the claimed bug reproduction. The final regression binds the exact parent test source/patch and binary, and the read-only verifier checks that this test source is identical to the repaired run's source.

## Execution evidence

Source base: `cd6464c77f2d4fb39a2acc449e12d8eeb7adbbef`. Six source/test files have patch SHA-256 `be56e537eaff7bf83c805ba70ed278400920e00d3ca8f450bcd3fef5bfc0d3e8`.

| Run | Evidence |
|---|---|
| Full Release build | PASS; actual compilation/relinking is in the untimed `preliminary-full-release-build.log`. The final dependency-complete up-to-date check passed in 0.072 s before CTest. |
| Release CTest | 3/3 lanes, 39 cases: snapshot 25, shared import outcomes 9, style migration 5; 1.945 s |
| Actual CLI intake | 6 methods PASS, zero skips; 1.776 s |
| Focused ASan/UBSan build | All three tested targets/dependencies rebuilt; 16.685 s |
| ASan/UBSan CTest | Same 3 lanes/39 cases and new traces PASS; 9.443 s |

The full Release build and focused sanitizer build overlapped. ASan uses `detect_leaks=0:abort_on_error=1:strict_string_checks=1`, UBSan `print_stacktrace=1:halt_on_error=1`. These are not benchmarks, leak detection, TSan or a full test/sanitizer suite. The Studio application case was established in `a7f81e07`; this unit rebuilt its dependents but did not rerun that full application journey.

Frozen root: `out/evidence/import-publication-identity-2026-10-15`. Manifest **12,956 bytes**, SHA-256 **`029b2bf249abe6eebe6ab03c94c85ecbec53a2bae36f6fb2da49105ab3485ffc`**, binding **52 artifacts and 14 pins**. It retains the original failing regression, parent runtime sources/binary, final sources/patch, generated build identities, commands/results, seven final binary copies and stable source/executable identities. A read-only verifier rehashes the evidence, verifies exact Git/source and parent-regression bindings, and checks final case counts/traces without running retained native/tests.

The transient attempt does not prove any filesystem write or durability by itself. Exact recovery remains mandatory. Directory-helper/fsync faults in retention wrappers, owned-reader cancellation, late cancellation after publication begins, OS faults and power loss remain outside this unit's evidence. The original Studio `a7f81e07` implementation is independently approved; its receipt-test follow-up `7ec877f7` is under separate exact review. No downloaded recording or original source evidence was changed, imported, listened to or approved. Alpha **0/20**, U45 **HELD**, Beta **BLOCKED**; goal **ACTIVE**. `.github`, README and the pre-existing stash remain unchanged; Windows remains TODO.


## Exact independent verdict and separate remaining writer

The final verdict from **Review in English** (`01a11bad-88f2-7521-849e-c38111dc2197`) is retained at `out/evidence/import-publication-identity-independent-2026-10-15/review-029cc6a9.md`: **14,252 bytes**, SHA-256 `89022c2d77a3c7ed6bef6aff19526a3eeb47ee05dfd5bd24888154adf3a6d985`; metadata **552 bytes**, SHA-256 `aec47b318418cb3942a60962e7d0109fc68a7a671b8e286a9fea315453597575`. The reviewer read exact committed blobs, independently checked the retained identities and parent failure, and ran no build, native binary, test, retained script or playback. Import closure has the parent-failing/fixed-passing regression; style closure is by code review, without a dedicated pointer-obstruction test.

The review found a separate pre-existing P3 in `libs/seam-voicebank-production/src/source_assessment.cpp:155`: a schema-2 project can contain two strategies and a take approved under the strategy not being assessed. The first assessment raises the schema to 3, so publication can reopen that other approval even though the draft lowered only takes bound to the assessed strategy. A pointer failure then makes its old normalized comparison refuse its own recoverable commit. This writer is not repaired by `029cc6a9`. Next implement the same exact attempted generation/full-digest recognition there and reproduce the two-strategy case through genuine software review APIs. Other old-style writers were inspected as currently safe under their existing transitions; their optional migration is not required to close this specific finding. No source admission or human approval follows from either closure.
