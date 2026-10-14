# Serialize finished-render publication with request revocation

2026-10-15, Asia/Seoul. **Local R15 engineering prerequisite verified; Beta BLOCKED.**
Fetch and fast-forward pull ran first; base is
`fe340737c0709a45f1d48cdfec8ec0089ea814ce`. This closes the worker's late
check-to-publication interval identified during the earlier runtime review.

## Reproduced defect and code change

`AuthoringRenderCoordinator::workerLoop` checked the latest request token and
then performed `publication_.publish` outside the admission mutex. A newer
submission, cancellation, reset or owner-side invalidation could revoke that
token after the check and return before the old result physically published.
`acquireCurrent` already rejected that result for current playback; the defect
was that the retained publication could still replace the previously accepted
result. The progress guard did not authorize publication itself.

The worker now rechecks the request token and publishes under `mutex_`, which
submission, cancellation and reset already use. `invalidateCurrent`, called by
the owner/debounce control in `AuthoringRuntime::invalidatePreview`, now uses
the same mutex. This makes revocation and publication ordered: a publication
accepted before revocation remains history; a worker arriving after revocation
cannot publish its finished result. A refused old worker increments the stale
counter and leaves the prior publication intact. Progress/completion callbacks
run after the admission lock is released. The serialization is on owner/
debounce control and worker paths; current-audio reads use the existing atomic
publication/token checks. It does not add DSP work to admission.

`RenderCoordinatorHooks::beforePublishAdmission` is a test-only barrier after
the early currency check and immediately before publication admission. It runs
outside coordinator locks. The baseline includes this observation hook and
new test cases but has no repair; retained source verifies that the worker
differs from the parent by only the hook call. It is not presented as an
unmodified historical executable or a race observed in a user's application.

## Deterministic execution evidence

Four new cases render an accepted seed and hold the next finished render at
that late interval. They then perform, separately, same-revision Preview-to-
Final replacement, cancellation, idle reset and debounced-intent invalidation.
The replacement worker is held before rendering, so it cannot hide an old
publication by immediately superseding it. Assertions check the retained
request ID, revision and PCM storage identity, stale/completed counters and
current-audio refusal. The replacement is then released and must publish its
new Final request successfully. RAII releases both barriers on assertion
failure, preventing a regression from hanging coordinator teardown.

After a full untargeted Release rebuild (95.239s), the controlled baseline
fails **all four cases** at `retained->requestId == accepted->requestId` in
0.441s. The unchanged regression source passes all four after the repair in
0.490s. This is an actual late finished-worker interleaving, unlike the earlier
installed-scheduler submission-time refusals.

The final full untargeted Release rebuild passes in 5.327s. Eight focused CTest
entries pass in 42.720s: five Python methods and **246 native cases**
(12/18/42/121/12/7/34), including loaded CLAP full-song, timeline, authoring
characterization/transport, incomplete-Final and installed native journeys.
Nine additional owner-runtime filtered cases pass in 1.065s. These counts are
reported separately; the four new regressions are already within the 42-case
coordinator suite and are not added again.

The existing Debug ThreadSanitizer cache has `SEAM_ENABLE_THREAD_SANITIZER=ON`.
Its coordinator target and all dependencies were rebuilt (34.524s), then that
native executable ran all **42 cases**, with
`TSAN_OPTIONS=halt_on_error=1`, in 73.302s: PASS and empty stderr. Other TSan
targets were not run or claimed rebuilt. This is focused concurrency evidence;
the required full candidate Debug/Release/ASan/UBSan/TSan lanes remain separate.

## Retention and remaining scope

The [index](PUBLICATION_ADMISSION_2026-10-15.evidence.json) binds **42 artifacts
and six preserved inputs**. It retains parent/observed/final sources,
baseline/final Release coordinator binaries, the TSan binary/configuration,
generated header, actual command/time/result receipts and logs. Its read-only
verifier hashes the exact set and inputs, checks immutable parent/optional
final Git source, unchanged regression source, hook-only baseline, counts,
receipts and empty TSan diagnostics. It executes no native code and does not
independently recapture PCM. Native assertions prove retained allocation and
request identity; no acoustic continuity or listening conclusion is drawn.
No authenticated clean-build or historical execution lineage is asserted.

**Review update:** **Review in English** now independently APPROVEs exact
`c27d1d6f5b9a883b48ea8c11c170998b68ac7a04`, with two P3 findings and no required
changes. Its [retained verdict and scope](PUBLICATION_ADMISSION_REVIEW_2026-10-15.md)
confirm physical publication/revocation serialization and lock ordering. The
existing late controls pin the currency recheck, while mutex exclusion is verified
by source reading. Outcome reports still follow lock release and can expose older
published fields to cancellation/replacement. Both follow-ups remain pending.
`ccbb39d6` and `fe340737` also have separate engineering approvals; this does not
approve later units or Beta. Earlier frozen roots and canonical files are preserved.

The contract's R15 case still requires all released resources, languages,
targets and review evidence. This selected development fixture does not close
that case. Request-input/status semantics, canonical tolerances/integration,
qualified classical/neural resources, human Gate A/Gate B, physical-device/
DAW/Windows, signing/install/recovery and release evidence remain. Internet
candidate files and their source manifests are preserved; this unit downloads,
imports and qualifies no recording. Genuine producer/distinct reviewer,
source/performer/public-license disposition, phonetics, coverage, failed-QC
policy and listening remain pending. Alpha is **0/20 recorded passes**, Beta
**BLOCKED**, U45 held, and the full goal **ACTIVE**. `.github`, README's Windows
TODO and the existing stash are unchanged.
