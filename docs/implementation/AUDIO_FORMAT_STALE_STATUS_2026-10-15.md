# Audio sample-rate replacement: retained-audio status repair

Date: 2026-10-15, Asia/Seoul. **Locally verified engineering repair; Beta remains blocked.** Fetch and fast-forward pull completed first. The source base is `667c8a19f81263d7d8630eab09e282f1e267e370`; the retained build includes the exact library/test changes below. The generated header embeds the base. This is reproducible local evidence, without authenticated clean-build lineage or human acceptance.

## Trigger and code-level cause

`AuthoringRuntime::reconfigureAudio` replaces the transport format and requests a preview without changing the document revision. In `AuthoringRenderCoordinator::submitWithSources`, the retained Ready audio's `audibleAudioStale` flag previously compared only revision and quality. A pending 44.1 kHz request could therefore leave retained 48 kHz audio labeled current when both revision and quality matched.

The current-publication guard already refused superseded audio; this repair does not change that guard or introduce a new rendering scheduler. It adds the sample-rate comparison to the existing stale-status predicate in [`render_coordinator.cpp`](../../libs/seam-authoring-runtime/src/render_coordinator.cpp). This flag feeds `AuthoringRuntime::audiblePublication`, the native render-status panel and the standalone/CLAP status paths. The existing successful-publication path clears the warning after matching new audio arrives.

The new coordinator case holds a same-revision, same-quality replacement in the debounce phase. It requires the previous 48 kHz publication to remain available, `acquireCurrent` to refuse it, and the queued status to mark it stale. It then completes an immediate 44.1 kHz replacement and requires a new request identity and a cleared stale flag. The existing runtime format/comparison case now explicitly establishes a current canonical render before comparison, holds the actual replacement in Rendering, checks the unchanged revision/quality and retained request identity, observes stale status through `audiblePublication`, and confirms recovered 44.1 kHz publication and transport availability.

## Failure and pass evidence

The original runtime case passed before its new assertions; it already covered comparison retirement and transport recovery. It did not cover the stale flag. The first new-test build failed because a `shared_ptr` was compared directly with a realtime ReadHandle. The first runtime attempt then timed out because the test assumed adding a non-audible performance proposal requests canonical rendering. These were test-setup errors, corrected before interpreting the runtime failure. All failed sources, logs, receipts and the relevant binaries are retained.

Against the fully rebuilt, unchanged baseline library:

- The coordinator case failed exactly at `queued.audibleAudioStale`. Its trace reports `retainedRate=48000`, `requestedRate=44100`, requested/published revision92 and `audibleAudioStale=0`.
- The corrected runtime case failed exactly at `audible.stale`, after establishing the same revision/quality, old 48 kHz request, unavailable current publication and completed comparison retirement.

The regression source is identical before and after the library repair. The **full untargeted Release rebuild passed in 10.304 seconds**. Four focused CTest entries passed in **32.417 seconds**:

| Suite | Cases passed |
| --- | ---: |
| Authoring runtime/performance | 56 |
| Render coordinator | 38 |
| Original singer/song journey | 7 |
| Procedural installed journey | 34 |
| Total | **135** |

There is one new coordinator case and one strengthened existing runtime case; those counts overlap the 135. The repaired queued trace reports `audibleAudioStale=1`. The runtime case also passes its new assertions and clears stale status after publication. Full dependent rebuilding precedes CTest; timestamps and actual binaries are retained. The two installed journeys exercise the existing signed-fixture/runtime/selection/transport paths. Their synthetic actors and development resources do not establish production resource, human or real audio-device acceptance.

## Retention and independent review

The [evidence index](AUDIO_FORMAT_STALE_STATUS_2026-10-15.evidence.json) binds the ignored evidence root `out/evidence/audio-format-stale-status-2026-10-15`: **56 artifacts and six preserved inputs**. These include immutable baseline and repaired source, setup failures, genuine regression failures, binary copies, generated header, all build/test receipts and logs, and the exact prior review. Inputs pin four canonical files and the earlier scheduler and internet-source manifests. The read-only verifier rehashes the set and inputs, compares baseline source with its immutable Git blob, proves the test source stayed identical across the repair, checks the exact library delta and reproduces the receipts/counts. `--source-commit` additionally compares repaired source to an immutable final commit. It executes no native binary. The manifest is 14,149 bytes, SHA-256 `1e16947db05046b6df555b7c8cdc57349bfb754556a64fb49b826436bf2fc34f`.

**Review in English** APPROVEs the preceding scheduler unit `667c8a19`, engineering scope only, with one optional P3 and no required changes. Its exact 12,616-byte verdict is retained and pinned. The [scheduler dossier](INSTALLED_SCHEDULER_INVALIDATION_2026-10-15.md) now limits pointer inequality to allocation, identifies the four stale results as submission-time refusals and records the reviewer's independent cache decode and edge-fade attribution. The approved original root is unchanged.

It subsequently APPROVEs `3a0fa6f1`: the production predicate, both regressions and retained evidence are correct. Its exact 11,568-byte verdict is retained under `out/evidence/audio-format-status-followup-2026-10-15` and pinned in this index. The P2 follow-up fixes test ordering: publication becomes visible before the Ready progress write clears stale status, so the coordinator case now waits for both the new publication and Ready/nonstale progress. Both tests check replacement handles before dereference. Product/library source is unchanged. Full untargeted build (5.031s) and coordinator38/runtime56 = **94 cases** (9.830s) pass; no new cases or forced race reproduction are claimed. The full build includes separate unfinished host-harness work, which is preserved and excluded from this follow-up commit. Exact tests/binaries/header/receipts, hashes and the prior approval are retained in a separate manifest; the approved original root is unchanged. This assertion follow-up's review is pending.

## Remaining full-scope work

This closes a production runtime status defect, not the broader U45 admission gate. Connected installed rendering existed before this repair and remains covered; production host audio-block execution, late scheduler completion controls, acoustic F0/phase, classical/neural continuity, released resources, platform/DAW/release acceptance and owned canonical tolerances still need their evidence and reviewed integration. The performance-plan content and phrase edge-fade questions identified by the scheduler review remain separate work.

The internet female recordings remain a downloaded, source-checked candidate set: 25 clean VocalSet f1 WAVs and 272 Karoryfer female growl WAVs. Source/performer and public-license disposition, a genuine producer/distinct reviewer, useful phonetic/pitch coverage, the failed-QC approval policy and listening still block production bank admission. They are not imported by this repair. Usable Alpha remains **0/20 recorded passes**, External Beta **BLOCKED**, U45 reconciliation held, canonical tolerances **UNRESOLVED**, and the development goal **ACTIVE**. `.github`, README Windows TODO, canonical files, original recordings and the existing stash are preserved.
