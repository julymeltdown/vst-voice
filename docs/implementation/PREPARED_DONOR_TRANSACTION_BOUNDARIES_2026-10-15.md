# Prepared donor transaction boundaries — 2026-10-15

The actual prepared-donor importer now has deterministic coverage for cancellation after work begins and for journal, generation and current-pointer publication failures. The tests prove when an import is uncommitted, when verified staged files can be reused, and when the exact import is already recoverably committed despite a failed pointer update. These are synthetic engineering tests, not real recording admission or approval. Exact independent review approved this engineering scope and closed the originating wrapper item; a separate pre-existing commit-recognition P3 remains open.

## Runtime checkpoint contract

`PreparedDonorImportOptions` in `repository.hpp` adds an optional `faultInjector`, with three stages inside `ProductionProjectRepository::importPreparedDonor()`:

| Stage | Work already performed | Authoritative commit state |
|---|---|---|
| `SnapshotValidated` | Original project/source/actor/assignment checks and held snapshot validation | Original generation |
| `RawTakeStaged` | Private WAV capture, actual stored raw audio, inspection and declaration retention in a private project draft | Original generation; immutable staged files may exist |
| `NoticesRetained` | Prepared binding/schema-5 draft and all four packet metadata files retained | Original generation; journal publication has not begun |

At each stage a callback error propagates unchanged. A successful callback is followed by a cancellation check; cancellation returns Conflict with `Prepared donor import cancelled before commit`. The original early-cancellation check remains. No callback runs after publication begins: existing `save()` and exact recovery recognition still determine the actual commit outcome. The private capture's existing RAII cleanup remains active on refusals.

Production callers use an empty default options value; the existing CLI calls compile and run without a new argument. This is source-call compatibility, not a promise of compatibility with a previously compiled C++ symbol. All dependents were rebuilt. No schema, QC threshold, source permission, manual review or qualification rule changes.

## Two new native cases

`tests/test_prepared_phone_donor_snapshot.cpp` adds two cases containing nine forms:

1. Three injected `IoError` forms and three mid-operation cancellation forms, one at each stage. Every form requires its exact diagnostic and observed checkpoint prefix; the in-memory and recovered project remain byte-identical to the original state. Journal and generation file inventories are unchanged. Every pre-existing workspace file/link digest is preserved; private capture directories are absent. The first stage has no file additions; later stages can leave verified immutable files. After notice retention all four exact metadata files must be present. Retrying without the injection commits one take in exactly one new generation.
2. Three real filesystem publication obstructions installed by the final checkpoint, after the initial current-pointer verification and notice retention. These reach `save()` rather than failing an earlier admission check:

| Obstructed destination | Required original result | Required recovery/retry behavior |
|---|---|---|
| Next journal path is a directory | No receipt; `Import did not confirm a commit.` | Original project recovers; removing obstruction permits one import |
| Next generation path is a directory | Same uncommitted refusal | The published incomplete journal remains unchanged; retry appends after its occupied number |
| `project.json` replaced by a directory | Receipt identifies exact committed generation/hash, with `durabilityConfirmed == false` and pointer warning | Recovery returns that exact imported project; repeat import is refused; explicit hash/generation-bound pointer reconciliation restores verification |

Each final state contains exactly one unapproved take and binding, no human review or source-quality decision, and the original stale request cannot import it again. Existing journal bytes are preserved during retries. The current-pointer repair does not append a second import.

The refusal inventory helper covers regular-file hashes and link targets; it does not inventory empty directories. The tests separately check private-capture cleanup. This is not a global rollback claim: content-addressed files staged before refusal may remain. Recovery and current project identity are the transaction boundary.

## Frozen execution evidence

Base commit: `2922ddaa4a100bdfb93ed3d9b91d6e4761e1a750`. Three implementation/test files have exact patch SHA-256 `6b68528ef700cf1540f107accd6e9cb83c260bfe3ad934f9225dfc400e09d9a9`: `repository.hpp`, `repository_import.cpp` and the snapshot test. Runtime/header change is 28 additions / 2 deletions; test change is 116 additions.

| Run | Result | Recorded wall time |
|---|---|---:|
| Full Release build | PASS; changed runtime/header/test dependents rebuilt before CTest | 77.047 s |
| Release CTest, snapshot + shared import-outcome lanes | 2/2 lanes, 24 + 8 native cases PASS; all nine traces | 1.530 s |
| Focused ASan/UBSan build of both tested targets/dependencies | PASS | 15.493 s |
| ASan/UBSan CTest, same two lanes | 2/2 lanes, same 32 cases PASS; same nine traces | 5.193 s |
| Existing actual CLI intake integration methods | 6/6 PASS, zero skips; exercises native capture/import/retake and Python reading of actual persistence | 1.943 s |

The sanitizer build overlapped the Release CLI run; times are observations, not benchmarks. The two new native cases contain nine forms per configuration, not nine new cases. ASan uses `detect_leaks=0:abort_on_error=1:strict_string_checks=1`; UBSan uses `print_stacktrace=1:halt_on_error=1`. This is not leak detection or TSan. The sanitized build covers the two tested targets and dependencies, not a full sanitized build or suite.

Frozen root: `out/evidence/prepared-donor-transaction-boundaries-2026-10-15`. Its manifest binds **56 artifacts and 15 pins**, **14,470 bytes**, SHA-256 `64871e920047f60247ca440531977d1434de32dc1a676fa71dab40891aa5922c`. Tested executables/actual CLI are retained with matching before/after hashes. Every execution captures the exact source diff/snapshots and base build version, with stable HEAD and source diff. Unchanged save/recovery/core/helper/test pins match the base Git blobs. The read-only verifier checks identities, compilation/results and exact traces without running retained native/tests.

## Remaining work and acceptance limits

This locally covers the prepared-import wrapper's mid-operation cancellation and journal/generation/pointer failure outcomes. Exact independent review closed the originating wrapper item; the separate pre-existing schema-transition recognition P3 remains open. It does not test cancellation inside the standalone owned reader from `3e27aa27`, directory-helper failures or atomic-write fsync errors inside retention wrappers, cancellation arriving after publication starts, OS faults or hardware power loss. Injected callback errors occur at explicit pre-commit boundaries and are not presented as fsync failures.

The separate native-range `090d1e09` and migration `dc433d80` reviews approved those engineering units and closed their originating items. Exact Python `3df28db4` approval and both P3 closures remain retained. Studio presentation is now implemented in `a7f81e07`; human reading of notices, genuine source/performer disposition, a real producer and distinct reviewer, phone/pitch/style assignment, failed-QC policy, listening and remaining product/host/release evidence are still required. No downloaded recording was changed, registered, imported, played or approved.

No full test suite, OS/power-loss, device, DAW, Windows, acoustic or human acceptance is claimed. Alpha **0/20**, U45 **HELD**, Beta **BLOCKED**, full goal **ACTIVE**. `.github` and README are untouched; Windows remains TODO.


## Exact independent review closure

Commit `f7d2559c1870c53cd55c210841b8928201641bf8` received **APPROVE**. The originating importer-wrapper mid-operation cancellation and injected commit-fault item is **CLOSED**. The exact final answer is retained in `out/evidence/prepared-donor-transaction-independent-2026-10-15/review-f7d2559c.md` (15,644 B, SHA-256 `2a3545ca6734f9c7ef94ff1b7f8d38bd03aac5a350130d355e4ae0eced844a74`), with metadata (605 B, SHA-256 `c9c3ff99ff9e984351e8c644e39ab2e12b1088b48c2deb4b030a987ac746dee3`). The reviewer independently rehashed artifacts and read committed code/logs; no tests, retained scripts or native binaries were executed by the reviewer.

At the exact f7 review, a separate pre-existing P3 was open: schema-4 to schema-5 publication could reopen earlier approvals and make pointer-failure recovery differ from the pre-transition draft. The exact `029cc6a9` follow-up is now independently APPROVED and closes that import/style recognition P3 with strict attempted generation/full-digest matching. The original Studio `a7f81e07` path is also independently approved; its receipt-test follow-up `7ec877f7` is independently approved and closes the a7 receipt P3. Genuine human reading/listening remains absent. Owned-reader cancellation, wrapper helper/fsync faults, cancellation after publication begins and OS/power-loss evidence retain their separate open scopes.


The separate P3 now has a local implementation and the same failing-before/passing-after regression: `IMPORT_PUBLICATION_IDENTITY_2026-10-15.md`. Recognition binds the exact attempted post-transition generation and digest. Exact independent review now closes this import/style P3; the originating f7d2559c wrapper item remains approved and closed.

Exact follow-up `029cc6a9` is now independently APPROVED for engineering and closes the originating import/style commit-recognition P3. Its [report and exact verdict binding](IMPORT_PUBLICATION_IDENTITY_2026-10-15.md) also record a separate pre-existing source-assessment writer P3, still OPEN. This does not extend wrapper, human or release acceptance.
