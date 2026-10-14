# Installed scheduler and neighboring-lyric regression

Date: 2026-10-15, Asia/Seoul. Result: **local engineering regression verified; U45/Beta remain blocked**. Fetch/fast-forward pull completed first. Base `445b803b91d563b43215d08ca527b33db1429aa5` plus one exact native test capture was built. Product/library/CMake source is unchanged. The generated header embeds the base; exact binaries and source are retained without authenticated clean build lineage.

## Code and scope

`checkInstalledSnapshotInvalidation` in [`test_sample_review_cli.cpp`](../../tests/test_sample_review_cli.cpp) extends the existing signed recipe fixture. It captures installed procedural admission and creates two contiguous notes with distinct lyrics plus a separate region. The edited project changes only the second lyric's surface from Japanese `あ` to `な`. Both saved projects and both selected regions pass the real native signed-package/installed-tree/project binding checks before the scheduler is used. The retained bindings have one installed identity and exact saved-project SHA-256 values.

The regression exercises existing production classes: [`RenderSnapshotFactory`](../../libs/seam-rendering/src/render_snapshot.cpp), [`BackgroundRenderScheduler`](../../libs/seam-rendering/src/render_scheduler.cpp), [`PcmCache`](../../libs/seam-rendering/src/pcm_cache.cpp) and `PhraseRenderPipeline`. It records six sequences of four 6,103-frame windows, with a shorter last window: affected/unrelated cold rendering, both warm repeats, affected rendering after the edit and unrelated reuse after the edit. One worker makes checkpoint progression deterministic. Completion identity/revision/extents and exact float32 words are checked against fresh whole-phrase renders before encoding. Warm paths must add zero processed DSP frames; cold paths must process only the phrase extent through scheduler checkpoint reuse.

The affected pronunciation identity changes, a new compiled performance object is created, and all four affected chunk identities change. The object pointer comparison proves allocation only, not a semantic change to the performance plan. The unrelated region's dependency identities remain equal. Submitting the old affected revision after the new one yields four stale completions with no PCM or markers, while the old immutable cache entries remain readable. These are refusals at submission time; this trace does not exercise late worker completion or drain-time rejection. Reusable content is preserved; obsolete submission is refused by the revision floor.

This is **DIRECT_INSTALLED_SNAPSHOT_SCHEDULER_API**, hosted by `seam_sample_review_cli_tests`. The test explicitly constructs fresh snapshots after the edit. It does not observe production editor dirty callbacks, prove application publication/revision checks, run standalone/CLAP audio blocks, qualify a singer, measure acoustic F0/phase or authenticate past execution. The new diagnostic JSON is not registered with the canonical full-product report. Existing observer records' `schedulerExecution: NOT_RUN_NO_CACHE_USED` still describe those original observations accurately.

## Executed results

The first full build failed on two ambiguous `int` to `JsonValue` conversions in the new trace fields. Explicit `int64_t` literals repaired them. Failed source, build log/receipt and runner are retained. The repaired **full untargeted Release build passed in 10.218 seconds**. Two focused CTest entries passed in **25.889 seconds**, covering **66 performance-snapshot cases and 27 CLI cases**. The new regression is inside an existing CLI case; it is not an additional counted case and has no Python compile guard. Python/full-repository suites and human/host/platform acceptance were not rerun for this unit.

| Stage | Windows | Processed DSP frames | Cache hits |
| --- | ---: | ---: | ---: |
| Affected cold | 4 | 24,000 | 0 |
| Unrelated cold | 4 | 24,000 | 0 |
| Affected warm | 4 | 0 | 4 |
| Unrelated warm | 4 | 0 | 4 |
| Affected after lyric edit | 4 | 24,000 | 0 |
| Unrelated after lyric edit | 4 | 0 | 4 |

Total: **72,000 processed frames, 12 cache hits and 4 stale refusals**. Independently decoded artifacts show **6,344 changed float32 words** between the affected whole renders. The merged phone projections change from `a,a` to `a,n,a`; warm and unrelated projections remain equal. The saved projects differ at exactly one JSON leaf, `/vocalTracks/0/regions/0/lyrics/1/surface`.

The independent reviewer decoded all 12 retained cache payloads and reproduced their checksums and PCM. It also attributed all 240 changed words in window 0, and 240 of window 3's 249 changed words, to the 5 ms phrase edge fades switched on by the edit. These differences do not independently demonstrate neighboring phonetic influence or audible quality; their desirability still needs a renderer decision and listening evidence.

Current chunk identities include the whole frozen phrase context. All four affected keys change, and all four windows show raw differences: 240, 207, 5,648 and 249 words respectively. This trial identifies no unchanged window inside the edited phrase and does not prove optimal per-window cache granularity. The separate unrelated region demonstrates reuse when dependencies match; identical PCM alone would be insufficient to discard a contextual dependency.

The [evidence index](INSTALLED_SCHEDULER_INVALIDATION_2026-10-15.evidence.json) binds **74 artifacts and five preserved inputs**, including the approved reader manifest and four canonical files. There are 27 little-endian raw float32 artifacts, cache files, exact source/binaries/header, receipts, projects and copied public signed-installation provenance. No signing private keys are copied. Saved projects keep historical absolute temporary installation paths; copies are provenance only, not self-contained relocated replay. The test fixture's synthetic actors/reviews are not real production approvals.

The read-only verifier rehashes the retained set, derives the single project edit, checks binding/file identities, decodes finite raw float32 data, recomputes every window/whole comparison, merges phone projections, checks statuses/statistics and confirms exact test counts. Its optional `--source-commit` compares the final test capture to an immutable Git blob. It executes no native binary or project Python module. Cache files are retained/hash checked; their internal payload codec is not independently decoded by this verifier. The initial verifier assumed the wrong public-key field/type spelling; its failed draft manifest/verifier/log and error are retained under `preintegration/` before correcting that verifier-only assumption. Native/CTest results and source did not change. The final manifest replaces an unpublished draft while preserving it; approved parent roots are unchanged.

## Review and remaining work

The existing **Review in English** session APPROVEs `445b803b` for the closed reader, controls and evidence, engineering scope only. Its exact verdict is retained here. The optional P3 wording is corrected: nonzero metrics are refused by schema before execution; coordinated projection/PCM forgeries are exposed by fresh rendering. It subsequently APPROVEs `667c8a19` for this scheduler unit, with no required changes. The exact verdict is pinned in this index and retained in the subsequent audio-format evidence root; the approved scheduler root is unchanged. The P3 pointer-comparison limitation and the submission-time/edge-fade attributions are recorded above. No production, rights, listening or Beta approval follows from either review.

Next: audit the production authoring coordinator's dirty-region, revision and publication path against this trace, extend its connected installed journey where evidence is missing, and then retain real host execution before reviewed semantic integration. Classical/neural continuity, acoustic observations, canonical tolerances, macOS/Windows and DAW/release evidence remain full-scope requirements. The internet recordings remain downloaded/source/hash checked but unqualified; genuine producer/distinct reviewer, rights disposition, coverage, failed-QC policy and listening still block bank admission.

Usable Alpha remains **0/20 recorded passes**, External Beta **BLOCKED**, U45 reconciliation held, canonical tolerance **UNRESOLVED** and the development goal **ACTIVE**. `.github`, README Windows TODO, canonical files, original recordings and the existing stash are preserved.
