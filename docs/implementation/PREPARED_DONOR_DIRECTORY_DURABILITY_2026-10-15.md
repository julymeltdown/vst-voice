# Parent-directory durability for retained donors and assets — 2026-10-15

The prepared-donor importer and immutable asset store now synchronize newly created directory entries into their parents before a later producer journal can refer to the files. Existing checked directories are also flushed on retry, including entries inside them. This is the locally verified repair for the P3 identified in the exact independent review of `dd0b515d`. Independent closure of this repair is pending; no real power-loss test or real sample admission is claimed.

## Code and failure behavior

`core::durableCreateDirectories` collects missing path components back to the nearest real existing ancestor, then creates and verifies them from ancestor to leaf. A checked component must be a directory, not a link. On POSIX, each component's parent and the component itself are fsynced before proceeding to a child. The ancestor/existing leaf is processed too: after a prior failed attempt, existence alone is not evidence that its directory entry or reused file names were flushed. Every reported filesystem or injected failure stops before later descendants. Error returns can leave unreferenced directories; they do not promise filesystem rollback.

The helper uses the existing directory-sync backend. Atomic file writes still flush their temporary bytes and containing directory. The private sync implementation was factored so the new helper can flush a directory itself as well as its parent. A scoped optional stage callback supports deterministic order/refusal tests; production callers supply no callback. The checked chain starts at its nearest existing ancestor. This does not validate every ancestor to the filesystem root or protect against arbitrary concurrent ancestor replacement. On Windows the existing directory-sync backend is a no-op; that platform remains unverified/TODO, and private prepared capture still returns Unsupported.

`ImmutableAssetStore::importFile` applies the helper to the asset root, the raw/derived kind directory and the digest bucket before blob publication or reuse. Existing verified file/size/hash and file-sync/rename/bucket-sync rules remain. The repository applies it to the workspace, `source-evidence`, `packets` and the exact packet directory before retaining notices and before `commitImportedDraft` publishes a generation. Reused directories are processed explicitly rather than trusting an existing leaf to prove the full owned path's durability.

The notice writer now allows byte-identical reuse only after an explicit Conflict from create-new. Other write/flush errors propagate even if a target name was published before the failure. Its existing link-checked resolver and exact byte comparison remain. This prevents successful-looking file reuse from swallowing an I/O error. The directory stage tests do not inject a notice-file fsync failure through the importer; that broader wrapper fault seam remains future work.

## Execution evidence

Source base: `c9ef0d21cf4696a08259d0a4f7b57f637df20003`. Exact five-file source/test patch SHA-256: `1275157bdd9f23ed680554a8a85c2bfa7d7bd5a4943d17d177010253eb305b21`.

Three new native methods check ancestor-before-child order and resynchronization of an existing leaf; six staged failure/retry forms across three levels and two stages; and linked, dangling or non-directory components/ancestors plus empty input, without touching the unrelated target. Existing synthetic inspected imports, packet sharing, refusals, retakes, ordinary asset imports, draft recovery and outcome tests remain in the selected native lanes. The stage callbacks observe code order around actual directory fsync calls; they do not emulate storage hardware or a kernel crash.

| Recorded execution | Result |
| --- | --- |
| Initial full dependent Release build | PASS, 199.119 seconds; first helper design, retained without a test claim |
| Final full dependent Release build | PASS, 7.255 seconds; final directory-self-sync and notice-error propagation changes included |
| Four selected Release CTest lanes | 4/4 PASS, 6.986 seconds; 20/28/8/64 = 120 native case executions |
| Python/native prepared-workspace interoperability | 8/8 PASS, zero skips, 3.100 seconds; actual newly rebuilt CLI |
| Read-only frozen verifier | PASS; 53 artifacts and 11 preserved bindings |

The native lanes are prepared-phone snapshot, production draft, import outcome and general production. The retained root binds every mode's exact patch/source copies, generated version header, logs and receipt. Test binaries/CLI were hashed before and after each run, matched and copied. The helper was introduced in this unit, so there is no old-code failing baseline. The first full rebuild rebuilt the many dependants of the shared header; the final full build completed all affected targets before CTest. No sanitizer, full suite, TSan, leak test, injected operating-system fsync error, native importer-wrapper commit fault or real power-cut execution is claimed.

```sh
python3 -B out/evidence/prepared-donor-directory-durability-2026-10-15/verify.py
# Use --commit COMMIT after integration or later source changes.
```

## Remaining work

The previous exact review and its correction concerning new asset buckets remain immutable and are bound by the adjacent evidence index. Its scaling P3 is independently closed at `a42e8086`. Python schema-5 parity was pushed at `c9ef0d21` and its independent review is still running. This directory repair needs its own exact verdict; the native range-bound test P3 remains open.

Candidate-4 complete notice closure through directory/package/install verification, per-take reviewed declaration completeness, Studio/CLI notice/phone display and controlled genuine admission remain. Genuine source/EULA/performer disposition, producer/distinct reviewer, phone/pitch/style choices, failed-QC policy and listening are not invented. No downloaded audio was registered/imported or played in this unit. The 1,469 original recordings remain unadmitted, with singing/growls/speech identities separate. Original full-product and recording-free voice scope remains unchanged. Alpha **0/20**; U45 **HELD**; Beta **BLOCKED**; goal **ACTIVE**. `.github`, root README Windows TODO and the existing stash are preserved.
