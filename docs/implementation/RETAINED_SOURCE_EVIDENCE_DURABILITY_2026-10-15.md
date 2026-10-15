# Retained source declaration and quality-evidence durability — 2026-10-15

The source-quality writer, ordinary imported declaration snapshot and source-registration writer now synchronize the workspace and `source-evidence` directories before publishing or reusing retained bytes. Registration and assessment reuse identical bytes only after an explicit Conflict from create-new. Other write/flush errors return before a generation can reference the evidence. This is the local repair for the pre-existing P3 identified in the independent `78a1b817` review, extended to the matching registration path found during this audit. Exact independent closure is pending.

## Code-level change

In `repository_import.cpp::snapshotSourceEvidence`, two calls to the existing `core::durableCreateDirectories` replace the plain directory creation. The first processes the workspace root; the second processes `source-evidence`. Its existing-directory behavior matters: a previous failed/killed attempt may have published an identical declaration name without finishing its directory sync. Before accepting the existing declaration's exact hash, the directory and its parent are now flushed under the same POSIX contract used by the prepared-notice importer. The existing held read, 4 MiB cap, digest comparison and refusal to overwrite remain.

`source_assessment.cpp::recordSourceQualityAssessment` uses the same two directory calls before create-new. After a write error, identical-file fallback is reached only for `ErrorCode::Conflict`; an I/O/flush failure propagates even if the name was already published. Its resolver, bounded exact-byte comparison, actor/policy/material/quality checks and private draft commit remain unchanged.

`source_registration.cpp::registerSource` had the same unrestricted write-error fallback. It now applies the same synchronization and Conflict-only reuse rules before saving the registered source. This additional repair belongs to the same source-declaration lifecycle; registration still requires an actual declaration and a registered producer. No rights, musical assessment or other approval is generated.

Directory-sync errors stop before the journal commit. Failed operations may leave unreferenced files or directories; there is no filesystem rollback promise. Late repository-save failures retain their existing exact-recovery/uncertain-durability behavior. Public APIs, schemas, file names and evidence hashes are unchanged.

## Verification and boundaries

Source base: `01e78a0c46ccc591a64ebb17ef7359c85f7f2dc1`. Exact three-file implementation patch SHA-256: `2f0bc7241cc84bf0740ef4993fb69e8437876430a3c265b5268d7ac9edb91393`.

The full dependent Release build precedes six selected CTest lanes. Those existing lanes cover the shared directory primitive's three ordering/failure/retry/unsafe-path cases; source registration, captured declarations and source assessment; ordinary and held prepared imports; generation recovery and import outcomes; and sample publication, authentic signed-package/installed verification, including schema 4's two distinct declaration snapshots. Native test executables and the dependent CLI are hashed before/after and copied. Python's existing prepared-workspace parity suite then runs against that newly rebuilt CLI.

No new test cases or old-code failing baseline are claimed. The primitive's existing injected stage tests exercise its own ordering/refusal contract; they do not inject a directory/file fsync error through these three repository wrappers. The fix's error-propagation branches are established by source inspection, with exact independent review pending. No OS-failure injection, process-kill/power-loss run, hardware write-cache guarantee, `F_FULLFSYNC` adoption, full suite, new sanitizer run, TSan or leak test is established. The prior candidate unit's ASan/UBSan results remain scoped to `01e78a0c` and do not cover this patch. Windows retains the directory-sync no-op/TODO boundary.

The adjacent `.evidence.json` index records exact commands, durations, source copies/patches, generated version, native identities, logs, originating review and frozen manifest. Evidence root: `out/evidence/retained-source-evidence-durability-2026-10-15`.

## Remaining work

The exact `78a1b817` verdict independently closed the earlier donor/asset durability P3; this sibling-writer repair needs its own verdict. Candidate-schema4 commit `01e78a0c` is separately under independent review. Its per-take declaration finding is locally repaired, not independently closed yet. The two Python test-gap P3s and the native declared-range test P3 remain open. The migration-plan writer has a similar lower-impact retention pattern; recovery currently does not require that plan, and this source-evidence unit does not change it. The originating review's wrapper fault-test and broader platform-sync observations retain their stated scope.

Next capability work is the before-use notice/phone presentation and controlled prepared-donor workflow, with original license, attribution and donor phones visible beside an explicit assignment. Genuine source/EULA/performer disposition, producer/distinct reviewer, phone coverage, pitch/style choices including MIDI 66, failed-QC policy and listening remain pending. No downloaded recording was registered, imported, played or approved. All 1,469 originals and 312 prepared speech donors remain unadmitted, and original full-product/recording-free voice scope remains unchanged. Alpha **0/20**, U45 **HELD**, Beta **BLOCKED**, goal **ACTIVE**. `.github`, README Windows TODO, prior evidence and the existing stash are preserved.

```sh
python3 -B out/evidence/retained-source-evidence-durability-2026-10-15/verify.py
# Use --commit COMMIT after integration or subsequent source edits.
```

## Recorded results

| Execution | Result |
| --- | --- |
| Full dependent Release build | PASS, 32.922 seconds |
| Six selected Release CTest lanes | 6/6 PASS, 28.633 seconds; 20/28/8/30/2/64 = 152 native case executions |
| Python/native prepared-workspace parity | 8/8 PASS, zero skips, 2.725 seconds |
| Read-only frozen verifier | PASS; 40 artifacts and 13 preserved bindings |

The manifest is 12,745 bytes, SHA-256 `ddfca1fea1c4f943d2d9c67084c73a3b75251a72eabc9d39bbaf33025b32cfbb`. Every stage uses the same three-file patch and stable source base. Seven native executables including the CLI match before/after CTest and are copied; the parity run separately binds and retains its CLI.
