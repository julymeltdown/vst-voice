# Repository staleness remediation: completed task scope

Date: 2026-10-12, Asia/Seoul. **11/11 task dispositions are closed:** nine repair/documentation tasks are complete, and RS-10/RS-11 reach their explicitly allowed NO_CHANGE_RECOMMENDED exits through source review and measurement. This is **100% of this task document's engineering scope**, not a percentage of full Project SEAM product readiness. The additionally authorized installed-recipe export operation is also completed.

The user requested fetch/pull first, preservation of unfinished edits, verified incremental commits/pushes, English, and at most three agents. Fetch and fast-forward pull were already current at `c1a17861ef7df83a3d5a707fea6de85bdb4a785f`. The lead plus two isolated workers performed the work; lead inspected their patches, ran integration checks, and published each coherent code unit. Prior pending source bytes/patch remain retained locally; the unrelated brand-fonts/tooltips stash remains present and unused.

## Closed requirements and source-control units

| Task | Accepted outcome | Evidence / landed unit |
|---|---|---|
| RS-01 | Supported schema-17–21 capture, legacy derivation and current native writer boundary restored | 38 Python cases and actual native pilot/capture; `998def4e` |
| RS-02 | One bounded supervisor with typed exits/signals/streams and truthful refusal assertions | Committed suite and repaired scoped sanitizers; `e63d5556`, `2d739855`, `c1a17861` |
| RS-03 | Current deferred-CI state is SOURCE_READY; release holds remain enforced | Phase13A 188 passes/4 optional skips, identity checks, blocked gates; `4582a7f5` |
| RS-04 | Current documentation/API route established; historical reports labeled | Source/link audit; `c7740aab` |
| RS-05 | Neural version dimensions and production launch guidance corrected | Source-grounded format/runtime comparison; `bc4b69b9` |
| RS-06 | Help and usage errors cause no writes | Existing pilot real-mode/determinism harness and no-write cases; `432d6bb6` |
| RS-07 | Five fixed-input scripts classified; partial/current route stays explicit | Source/AST/path/hash inventory; `bc835c40` |
| RS-08 | Five missing historical targets and six occurrences transparently retained | Exact availability records, no substitute evidence; `3c1ac597` |
| RS-09 | One demonstrated repeated lyric geometry rule removed | Full Release build, V06 3/3, actual AppKit 5/5; `6f8da65cf0288b68809b791dafdd7b0202ec918e` |
| RS-10 | Existing admitted immutable resource boundary retained after current operation trace | Source review below; NO_CHANGE_RECOMMENDED |
| RS-11 | Four warmed scenarios measured; broad identity-rebuild hypothesis ruled out for named loop | [Measurements](../reviews/RS11_BUILD_COST_MEASUREMENTS_2026-10-12.md); NO_CHANGE_RECOMMENDED |

RS-01–RS-08's exact checks/commits remain in the [dated execution report](REPOSITORY_STALENESS_REMEDIATION_EXECUTION_2026-10-11.md); those earlier executions are not represented as rerun today. Its pending/deferred statements are historical and superseded here. The [manifest](REPOSITORY_STALENESS_REMEDIATION_TASKS_2026-10-11.tasks.json) now records every current disposition and preserves original planning metadata separately.

Installed export landed at **`95d093b15fb5e94729c1c7f68c760f527ce497d6`**. Latin `a` in Japanese resolved to warning + `pau`, explaining exactly zero PCM. Kana `あ` produces peak **0.0075130462646484375** with the original **>0.0001** threshold unchanged. Independent review additionally repaired master metadata, output routing/rate preflight, journal/no-replacement races and committed-receipt identity observation. Full Release rebuild and selected CTest passed **7/7, 21.71s**, export **37/37**, CLI **25/25**; scoped ASan/UBSan set-export cases passed **8/8**. [Root-cause report and retained failures](../reviews/INSTALLED_RECIPE_EXPORT_COMPLETION_2026-10-12.md).

RS-09's [geometry report](../reviews/RS09_LYRIC_GEOMETRY_COMPLETION_2026-10-12.md) distinguishes controller note anchors from painted editor bounds. Shared logical geometry now drives paint/native placement; real Cocoa marked text, selection and responder identity survive refresh and scroll. Resize/hide cancel safely. Native physical scale 2 was observed; logical cases cover 1×/2×. No VoiceOver or DAW acceptance is inferred.

## RS-10: actual operation I/O and admission trace

Reviewed on completed code `6f8da65c`, by lead and an independent source reviewer. No production architecture change was needed.

| Stage / code | Actual reads and decisions |
|---|---|
| [Project binding](../../apps/seam-voicebank-cli/project_binding_commands.cpp) and [installed operation](../../apps/seam-voicebank-cli/candidate_package_commands.cpp) | One held, bounded project read binds SHA/decoded selection. Signed-package/installation verification reads the signed tree and matches provenance; it is an engineering installation attestation. CLI checks all vocal-track saved pins and canonical recipe location, including muted/solo-excluded selections. |
| [Runtime admission](../../libs/seam-distribution/src/procedural_package.cpp:669) | Reads manifest, recipe and installation receipt per vocal track. Identical selections therefore have repeated reads; relative-location classification can add metadata reads. Each saved reference obtains current resource/pin/style/compiled-engine/trust-policy validation. Restricted `AdmittedProceduralRecipe` owns captured resource bytes and origin. |
| [Frozen capture](../../libs/seam-rendering/src/project_renderer.cpp:85) | File sources perform admission. Already admitted frozen sources compare track, selected reference, resource/style, engine and held digest through `matches`; packaging/render capture calls use this branch without reopening the installation. A bound absolute source lacking admission must re-admit. |
| [Packaging](../../libs/seam-authoring-runtime/src/export_service.cpp:899) | Decodes/canonically encodes held recipe bytes per track, verifies hash and deduplicates output recipe files by hash. Portable packaged references retain installation pins; the original render project and all master/stem outputs consume the same frozen sources. Serialization/hash checks bind output content. |
| [Snapshot](../../libs/seam-rendering/src/render_snapshot.cpp:713) and [pipeline](../../libs/seam-rendering/src/render_pipeline.cpp) | Repeated in-memory decoding/validation checks pronunciation, capability, timing, owned frames and checkpoint contracts. These do not repeat filesystem installation admission. |
| [Export renderer call](../../libs/seam-authoring-runtime/src/export_service.cpp:1071) / other cached calls | This operation supplies `nullptr` for PCM cache, so no cache I/O occurs. Other callers create/validate snapshots before lookup. A cache hit does not replace current file-backed admission. |

The admitted abstraction supplies the required boundary. This conclusion retains per-track I/O and in-memory validation; it does not assert zero duplicated work or globally optimal runtime cost. No recurring unnecessary policy crossing was found that warrants another public trust token or broader graph rewrite. Real installed-export/whole-project refusal regressions and the scratch CLI suite exercise the selected operation.

## RS-11: measured exit and remaining boundaries

At six jobs on the M3 Max, the warmed target build measured **0.039s no-op**, **2.424s leaf**, **54.705s shared header**, and **5.656s actual documentation-only commit**. Compilations were 0/1/235/4 respectively; CLI tests passed after all four scenarios. The generated identity and actual runtime export receipt match the real scratch commit. Existing source-identity CTest passed **17/17 Python cases, 7.22s**. Full options/load/commands/raw-log digests are recorded; no CMake change or invented speedup is claimed. Source identity does not cause a whole dependency rebuild in this measured loop. Broader development economics remain a separate question.

No remediation task or original installed-recipe failure remains pending. Product acceptance still has its separate human listening, source rights, learned-model qualification, host/platform/signing, reviewer and immutable-archive requirements. U45 admission/reconciliation holds remain; missing historical evidence remains UNAVAILABLE and packet 003 remains partial. Windows remains TODO. `.github` has no change. These are explicit product/evidence boundaries, not reopened remediation tasks.

Each code-unit push was followed by local HEAD / origin/master / live remote equality. The closing metadata unit is separately committed/pushed after source closure, manifest/link/evidence checks and review. Local ignored evidence is retained before archiving the three task-created scratch/worker checkouts; those archives preserve recoverable drafts/experiment commit and are not production integration claims.
