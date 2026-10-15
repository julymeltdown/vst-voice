# Style migration retention durability — 2026-10-15

The legacy style-migration writer now uses the same directory durability and Conflict-only reuse rules as source registration. This repairs the lower-impact sibling pattern identified in the exact `bce11b9a` review. It does not change migration semantics, schemas, qualification or approval policy. Exact independent review of this repair is pending.

## Behavior and code

`ProductionProjectRepository::applyStyleMigration()` previously created a missing `migrations/` directory with `create_directory()`. Its create-new plan write synced that directory, but did not sync the newly created directory entry into its parent. Any write error could also fall through to identical-byte reuse, including an I/O or post-publication sync error.

`libs/seam-voicebank-production/src/style_migration.cpp` now calls `durableCreateDirectories()` for the workspace and migration directory before retention. The existing helper checks real directories, syncs parent entries and resyncs reused contents. Any helper error returns before the plan write or journal commit. After `durableAtomicWriteNew()`, only `ErrorCode::Conflict` can attempt bounded identical-byte reuse. Other errors propagate immediately, even if the destination bytes exist. The existing cancellation checkpoint remains between directory preparation and retention. New-file publication and subsequent generation/recovery behavior are unchanged.

Unlike retained source evidence, the migration plan is not a recovery prerequisite today. That explains the lower impact of this finding; this patch does not add a plan-recovery dependency or claim that damaged historical plans are now checked.

## Regression coverage

One new case in `tests/test_production_style_migration.cpp` contains six forms. It creates real legacy workspace/import history with synthetic actors, declarations and PCM, then uses the public migration operation:

| Form | Required outcome |
|---|---|
| Missing `migrations/` directory | Exact plan retained; one new generation; verified migrated project |
| Identical plan already retained | Legitimate Conflict reuse; one new generation; verified migrated project |
| Migration directory is a regular file | Conflict; unchanged project, journal and tree |
| Migration directory is a symlink | Same refusal; outside target unchanged |
| Retained plan path holds different bytes | Same refusal; existing bytes preserved |
| Retained plan path links to identical outside bytes | Same refusal; link and target preserved |

Refusals compare the in-memory project, recovered project and a recursive inventory containing regular-file sizes/hashes, link targets and directories, including empty directories. Six explicit outcome traces are retained. Existing four tests still cover valid migration, unresolved style, stale/different work and actor/plan validity.

This covers missing-directory success, ordinary Conflict reuse and filesystem conflicts. It does **not** inject an I/O/fsync failure after publication or a directory-helper error. Those wrapper fault tests remain open; existing primitive fault tests are not promoted to wrapper execution. No hardware power-loss guarantee is claimed.

## Exact execution and evidence

Base: `090d1e0903850586cf024961c10aa5988cbedbbb`. The two implementation/test files have patch SHA-256 `2ae6503b732c5731ea70dc76bfed2d523950c972d4f9d68b0a885f26462e1e20`: runtime **4 additions / 9 deletions**, test **60 additions**.

- Required full `cmake --build build/release -j6`: PASS, **32.727 s**, with actual runtime/test compilation and dependent relinking before CTest.
- Focused migration CTest lane: PASS, **5/5 native cases**, **0.519 s**. Six forms belong to one new case.
- Actual tested executable retained; before/after size and SHA-256 agree. HEAD/source diff remained stable; embedded version is the base. Existing core helper and source-registration pins equal base Git blobs.
- Final frozen manifest: `out/evidence/style-migration-retention-durability-2026-10-15/manifest.json`, **6,596 bytes**, SHA-256 `a72b3169653fc65a05834014c82749060e9a8430536273d6f118daa6a4068a2d`, **23 artifacts / 9 pins**. Read-only verifier PASS; it checks identities, source snapshots, compilation/result logs and exact traces without running retained native/tests.

The first draft read-only verifier checked its 20 artifacts correctly but printed a count of six because its summary variable was reused for traces. Draft verifier/manifest/log are retained. The final verifier uses a distinct variable and checks all 23 retained artifacts. No native test was rerun or result altered; the final manifest was frozen after this correction.

No full test suite, Python, new sanitizer, leak, TSan, OS fault, power-loss, Studio, DAW, Windows, acoustic or human check is claimed. The earlier exact `bce11b9a` final answer and metadata are pinned; that approval covers the earlier sibling writers, not this patch. Exact Python `3df28db4` review remains separate, and native range repair `090d1e09` still awaits exact review.

No real recording was changed, registered, imported, played or approved. Genuine source/performer disposition, real producer/distinct reviewer, phone/pitch/style choices, QC/listening and remaining product/host/release evidence remain required. Alpha **0/20**, U45 **HELD**, Beta **BLOCKED**, full goal **ACTIVE**. `.github` and README untouched; Windows TODO.
