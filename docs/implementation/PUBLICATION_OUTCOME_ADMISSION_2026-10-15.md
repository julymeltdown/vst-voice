# Publication outcomes share admission with revocation

Date: 2026-10-15, Asia/Seoul. Integrating base:
`45c8b2603b133243be4fdbdf48bf020583565964`. Local engineering verification
passes; this new unit's independent review is **PENDING**.

The exact `c27d1d6f` review approved physical publication/revocation serialization
but identified two P3 follow-ups. Its four late-result controls could not distinguish
a final currency check with an admission lock from the same check without that lock.
Also, terminal counters and reports were written after physical publication released
admission. If cancellation ran in that interval, it could relabel a completed attempt
Cancelled; a replacement could inherit the older published revision. Current-audio
identity checks already refused revoked audio. This repair commits terminal outcomes
under the same admission mutex that cancel, submit, reset and invalidate acquire.

## Code and ordering

In `libs/seam-authoring-runtime/src/render_coordinator.cpp`, the worker still checks
request currency again after acquiring `mutex_`. It now keeps that lock across:

- A failed render's decision to retain the previous Ready publication, its failure
  counter and its Failed report, including the retained revision/quality.
- Physical publication and its Ready or Failed counter/report.
- Publication-slot exhaustion, its failure counter and PublicationBusy report,
  with the prior publication fields copied from one progress snapshot.

The retained-audio failure path now participates in that final locked currency check
too. No changed outcome is counted or reported after revocation has won admission.
The existing early `active_ = false` step is preserved. Cancellation before terminal
admission still observes a Rendering report and cancels it; cancellation after terminal
admission observes a completed outcome and preserves it while revoking current audio.
Replacement inherits the completed published fields in its Queued report.

`updateProgressIfCurrent` takes only `progressMutex_` and invokes no user callback.
The lock order remains `mutex_ -> progressMutex_`, with the physical publication's
existing writer serialization inside admission. `notifyCompletion()` remains outside
the admission block. The test-only `beforeOutcomeReport` hook can park that decision
before its counter/report; it runs under admission, must not re-enter the coordinator,
and must be released before test exit. Audio reads gain no lock. This does **not**
provide one atomic snapshot spanning lock-free audio, atomic statistics and the
separately locked progress reader.

## Discriminating controls

Six native cases are new in `tests/test_authoring_render_coordinator.cpp`:

1. A late worker parks outside admission. Reset then holds `duringReset` with the
   worker's token still current. Releasing the worker cannot change the accepted
   publication/count during the bounded 150 ms observation. After reset releases,
   the result is stale, progress is Idle and current audio is revoked.
2. Cancellation after Ready selection waits for its completed report.
3. Replacement after Ready selection waits and inherits its newly published revision.
4. Cancellation after a failed render waits and retains the earlier audio's context.
5. Cancellation after three pinned slots exhaust publication waits and preserves the
   PublicationBusy report.
6. A completion callback re-enters `cancel()` and returns, checking notification
   remains outside admission.

All held barriers have release guards before thread/coordinator destruction on a failed
assertion. Timing observations alone do not prove OS scheduling. The controlled mutation
below supplies direct discrimination evidence rather than treating a sleep as that proof.

The baseline is the exact parent worker plus two observation-hook calls, with the same
new header/tests. Full untargeted Release rebuild passes in **94.104 s**. The six-case
run yields **2 passes / 4 failures**, all four at cancellation/replacement returning
while the outcome gate is held. The held-reset and callback controls pass on that baseline.

A separate mutation removes **only** the worker's physical-publication admission lock;
the final currency recheck is retained. Full untargeted rebuild passes in **5.240 s**.
The new held-reset case fails at `completed == 1`, while all **four old late-publication
controls still pass**. These executions demonstrate the earlier review's precise gap;
they are not executions of the entire historical `c27d1d6f` checkout.

After repair, full untargeted Release rebuild passes in **4.947 s** and all **six** new
cases pass. Eight focused CTest entries pass: **253 native cases**
(`12/18/48/121/13/7/34`) and **11 Python methods**. Eight Python methods are existing
host/admission paths and three are the previous unit's mocked fixture-ownership controls.
Child native invocations are not additional cases or human/DAW journeys. The separate
`seam_tests authoring_runtime` filter passes **nine** cases in **0.899 s**.

The first focused run passed in **43.952 s**, but its wrapper named absent runtime/song
binaries and the unused generic CLAP host/plugin instead of the actual editor children.
That original script, receipt and streams are preserved. Its coordinator/runtime-filter
and TSan executable bindings are valid; no editor-child binding is claimed from it.
`run.bound.py` corrects the inventory and repeats the focused run in **39.750 s**, hashing
all seven actual native CTest entries, the actual CLAP editor host/plugin, journey children,
the runtime-filter executable and TSan binary before/after. All remain unchanged. The
repeat strengthens execution binding; test counts are **not additive**.

Fresh Debug ThreadSanitizer build of the coordinator and dependent libraries passes
in **6.017 s**. With `TSAN_OPTIONS=halt_on_error=1`, all **48 coordinator cases** pass in
**82.977 s**, with empty stderr. Only that target is claimed for this sanitizer build;
the Release CTest entries were preceded by a full untargeted Release build.

## Retained evidence and boundary

The [evidence index](PUBLICATION_OUTCOME_ADMISSION_2026-10-15.evidence.json) pins
**69 artifacts / nine preserved inputs** under
`out/evidence/publication-progress-2026-10-15`. The **12,298-byte** manifest has SHA-256
`42d1cca6e39a876a93eeced8c375fde8e40bb053fffe5c1514902093191926f4`.
It retains parent/observed-baseline/lock-removed/final sources, baseline/mutant/final
executables, twelve receipts and streams, both capture scripts, TSan configuration,
the exact c27 verdict, scope and a read-only verifier. The verifier checks held hashes,
immutable parent Git blobs, source/binary pre/post bindings, source/build/run mtime
ordering, exact mutation shape, actual results/counts and preserved canonical files.
It executes no retained native binary or runtime script. This is consistent local
source/build/run evidence, not authenticated or hermetic build provenance.

The original c27 review remains APPROVE for that exact older engineering unit. Its two
P3s are addressed by this separate locally verified implementation; independent approval
of this unit is pending. The peer's exact c111cbac review has separately become terminal
APPROVE, with three P3s, and its full verdict is preserved for the next scorer follow-up.
This publication unit does not resolve those scorer findings or approve the later bank
diagnostic/admission/provenance/fixture units.

No recording is downloaded or imported in this unit. The 297 internet WAVs still need
genuine actors, rights/public-license disposition, phonetic coverage, failed-QC policy and
listening before bank admission. Physical device, real DAW, human/VoiceOver/listening,
Windows, acoustic criteria/tolerance ownership, original/classical/neural/R9/released
resources and reviewed integration remain. Canonical Alpha stays **0/20 recorded passes**,
U45 held, Beta **BLOCKED**, full goal ACTIVE. `.github`, README Windows TODO, original
recordings, historical frozen evidence and the existing stash are preserved.
