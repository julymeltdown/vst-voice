---
title: SEAM development plan, revision 2 — listening-gated completion
date: 2026-09-15
status: active execution plan
supersedes_sequencing: SEAM_REVISED_DEVELOPMENT_PLAN_2026-09-15.md (revision 1)
baseline_commit: 2521951f5f05231b6d411c093ffb9183dcf3f148
input_review: SEAM_SECOND_DEVELOPER_REVIEW_2026-09-15.md
implementation_performed_by_this_document: false
---

# SEAM development plan, revision 2

## 1. What this document is, and what the supplied report is

The report supplied for this revision is **byte-identical** to
[SEAM Second-Developer Review](SEAM_SECOND_DEVELOPER_REVIEW_2026-09-15.md), which this workspace
already adopted at commit `68146ef2` ("Adopt listening-led procedural singer development sequence").
I verified the equality with a file diff rather than by title or description. Its direction is
therefore **already the adopted direction**; this document does not re-decide it.

What was stale was not the direction but the **status** the direction rests on. Nine commits landed
after the review was adopted, and they moved several facts the review describes:

```text
bbc730d9  Offer an installed procedural singer by identity, and only one this build renders
89c17ce4  Bind a procedural manifest to its recipe's canonical encoding
a84e672d  Report an unrenderable procedural singer as incompatible, not untrusted
3489f570  Discover and resolve installed procedural singers by exact identity
5cc5992a  Install procedural singers transactionally with a receipt
71955fc7  Make signed procedural singers a real package family
68dada55  Draw one lane for the timbral channels and keep a refused curve visible
ca454ef7  Record first direct procedural listening packet and repeatable song
b6e51589  Retain bounded listening packets and measure pitch at actual note times
```

So revision 2 does three things. It corrects the review's now-stale observations against current
source. It re-points the critical path at the one constraint that nine commits did **not** relieve.
And it writes the code-level plan for the work that actually remains, so the next implementation turn
does not have to re-derive it.

## 2. Status corrections: review claim versus current source

| Review statement | Current state | Source of the correction |
|---|---|---|
| "The failing test is confirmed housekeeping" and closure is open | **Resolved.** `SOURCE_CLOSURE=PASS`, and the registered run is 166/166 including `seam_tracked_source_closure` | `python3 scripts/verify_tracked_source_closure.py`; `ctest -j 8` |
| "The expression channels have no editing surface" (M4.P1 item 6) | **No longer true.** One drawn lane edits all six timbral channels with per-channel units and bounds, gesture-level undo, and the stored curve preserved beside its refusal | `libs/seam-editor-ui/{include/seam/ui/expression_lane.hpp,src/expression_lane.cpp}`; `seam_expression_lane_tests` 8/8 |
| "The direct procedural route already exists; its distribution lifecycle does not" | **Substantially addressed.** A typed package family, transactional install, catalogue discovery, exact-identity resolution, engine-revision compatibility and native installed selection have landed (D4.1, D4.3–D4.7) | `libs/seam-distribution/*procedural_package*`; `seam_procedural_package_tests` 11/11; `seam_u3_standalone_tests` 4/4 |
| "The new expression channels do not work on the resource the journey installs" | **Still true and still an acceptance condition.** A bank refuses the six timbral channels by name; the lane now makes that refusal visible instead of silently dropping the edit | `expression_lane.cpp` refusal rendering; D2 ledger entry |
| "Nothing has been listened to" | **Still true, and now the project's only binding constraint.** Audio exists; no observation does | `docs/implementation/listening/2026-09-15-d1-02/decision.md` records `LISTENING NOT_REVIEWED` |
| "The 498 generated takes are all unapproved" | **Still true.** Inventory remains preparable, not qualified | Campaign ledger, unchanged |

Read the table as the honest boundary. Implementation uncertainty kept falling while musical
uncertainty did not move at all, and revision 2 exists to stop that from continuing by default.

## 3. What revision 2 changes

Scope does not change. Full-Scope U1–U48, R1–R20 and preserved U60 remain mandatory for Beta GO.
Sequencing and default work selection change in four ways:

1. **The human listening gate is the critical path.** No further capability work is authorized as a
   way of deferring it. D3 (acoustic repair) cannot start without a supplied observation, because the
   plan deliberately refuses to pick a repair from waveform statistics.
2. **Expression breadth is frozen.** Growl landed; no seventh channel is added until the lane has been
   exercised on the retained song. Six channels with a lane is now the position; another channel would
   buy less information than running the lane on real material.
3. **Remaining distribution work is narrowed to two substeps.** D4.2 (review-candidate binding) and
   D4.8 (the connected journey) are what remains of the procedural slice; D4.1 and D4.3–D4.7 are
   implemented and tested.
4. **One prerequisite is still missing and is now tracked as a defect.** Revision 1 required a short
   format decision before D4.1/D4.3. That record was never written, even though the code landed. It is
   added as ADR 0022 in this revision's first work package, because a landed format with no written
   decision is a producer/consumer contract that exists only in the implementation.

## 4. Where each package actually stands

| Package | State | Evidence |
|---|---|---|
| D0 growl checkpoint | **Done and published** | `b8fff071`; growl suite 8/8; closure passes |
| D1 listening packet | **Rendered, retained, unreviewed** | 11 cases, 66 WAVs, 16-second melody, 335 hashes checked; `LISTENING NOT_REVIEWED`. `2521951f` adds a comparison tool that reports differences without ranking them, and confirms the retained rerender is 6/6 identical |
| D2 expression lane | **Exit closed** | `68dada55` lane; `f983c3c5` proves all six channels change the rendered audio on a source-filter song and that a sample carrier refuses them by name; `seam_expression_on_song_tests` 3/3 |
| D4.1/D4.3 manifest and pack/verify | **Done** | `71955fc7`, `89c17ce4` |
| D4.4 install and receipts | **Done** | `5cc5992a` |
| D4.5 catalogue and resolve | **Done** | `3489f570` |
| D4.6 native selection, copy-to-edit and reason surfacing | **Done and pushed** | `bbc730d9` selection; `4d3b3739` copy-to-edit; `475e9eba` Designer save guard; `0c43a690` names an unusable singer with its reason; journey 10/10 |
| D4.7 compatibility | **Done** | `a84e672d` |
| D4.2 review candidate | **Done and published** | `5d149f22`; `seam_procedural_review_tests` 10/10 |
| D4.8 connected journey | **Done and published, and it found a real defect** | `9711eecb`; journey 10/10; installed selection now records the identity the renderer validates |
| D3 acoustic repair | **Blocked on human listening** | D1 decision record |
| N1 neural feasibility | **Checkpoint 1 done** | `b78b92cf` input record plus a verifier that reports four of six inputs absent; no training run |
| D5/D6 expansion and qualification | **Not started** | Gate is the first usable original-singer milestone |

## 5. Progress, stated in the four labels rather than a percentage

| Label | Reached? |
|---|---|
| Listening packet reproducible | **Yes** — packet rerenders and its hashes were checked |
| Listening observation obtained | **No** — this is the blocking gap |
| First usable procedural creator loop demonstrated | **Partly** — selection, the lane and audible expression edits are verified end to end, but no person has performed the loop unaided and no listening judgment exists |
| First usable original singer delivered | **Partly** — the whole installed lifecycle is built and journey-tested, but no real producer has signed a package and no creator has used one unaided |
| Full-Scope Beta GO accepted | **No** |

A single global percentage would be fabricated precision: the tracked U-unit acceptance count is a
dated acceptance record, not a code-completion ratio, and the plan explicitly suppresses the
conversion. The defensible statement is that the *procedural creation route* is close to its first
usable milestone, while the *musical question* has zero evidence and the *neural, multilingual and
qualification* scope is largely untouched.

## 6. Work package P1 — obtain the listening observation (human gate, no code)

This is the highest-value action available and it is not an engineering task. The material is already
prepared and level-matched:

```text
/Users/lhs/Downloads/seam-listening-artifacts/2026-09-15-d1-02-audition/
  song-baseline.mp3        <- start here
  song-higher-formants.mp3
  song-breathier.mp3
  dry-vowels.mp3  dry-stops.mp3  dry-affricates.mp3  dry-glides.mp3
  dry-nasals.mp3  dry-melisma.mp3  dry-events.mp3  dry-rhythm.mp3  dry-range.mp3
  dry-articulation.mp3
```

Procedure, in order: (1) audition `song-baseline.mp3`, then its two recipe variants, and record a
preference and the reason; (2) for any phrase whose words are unclear, note whether the failure is
isolated or connected, slow or short, and whether it changes across the range; (3) attempt one
correction through the expression lane and record whether the correction was reachable; (4) assign
A, B, C or D per the review's outcome matrix and write it into
`docs/implementation/listening/2026-09-15-d1-02/decision.md`, replacing `NOT_REVIEWED`.

Until step 4 happens, D3 stays stopped and no acoustic repair is chosen. Korean speakers can supply
the usability and artifact judgments; Japanese pronunciation needs a Japanese-capable listener and
must not be inferred from Korean.

## 7. Work package D4.2 — bind a review decision to an exact candidate (code)

**Status: implemented and published at `5d149f22`.** `libs/seam-distribution/{include/seam/distribution/procedural_review.hpp,src/procedural_review.cpp}`
and `tests/test_procedural_review.cpp` exist and `seam_procedural_review_tests` passes 10 of 10. The
design below is retained as the record of what was built and why, not as outstanding work.

**Why now.** Signing proves authenticity. It says nothing about whether the voice is wanted, and
nothing today prevents a changed recipe from inheriting an earlier approval. This is the last
correctness gap in the procedural distribution slice.

**New files.**

```text
libs/seam-distribution/include/seam/distribution/procedural_review.hpp
libs/seam-distribution/src/procedural_review.cpp
tests/test_procedural_review.cpp          -> new target seam_procedural_review_tests
```

**Types and rules.**

- `ProceduralReviewBasis`: the exact material a decision is about — `resourceId`, `version`,
  `recipeSha256`, `contentHash`, `engineId`, `engineRevision`, `renderAbi`, `compilerRevision`,
  `sampleRate`, `scoreSha256`, `audioSha256`, and a canonical settings digest. Provide
  `basisDigest()` over the canonical encoding, the same way the manifest digest already binds the
  canonical recipe rather than raw bytes.
- `ProceduralReviewCandidate`: `candidateId`, `version`, `basis`, plus the carried evidence paths and
  their hashes. Freezing is read-only: it copies references, never mutates signed content.
- `ProceduralReviewDecision`: `Accept | Reject` with `reviewerId`, `reviewedAtUtc` and free-text
  basis note. Reuse the shape of `ReviewRecord` where it fits, but store procedural decisions in
  their own record: do **not** manufacture thousands of fake sample-take rows to reuse the bank
  reviewer. The bank reviewer walks unit lists; a recipe has one resource.
- `ProceduralReviewReceipt`: `candidateId`, `basisDigest`, decisions, and `staleEntries` naming any
  decision whose recorded basis no longer matches the current candidate.

**The one invariant that must hold.** A decision resolves to a candidate only when the candidate's
current `basisDigest` equals the digest the decision was recorded against. Change the recipe, the
renderer revision, the render ABI, the score or the audio and the prior approval becomes `stale`,
never silently inherited. Automatic format migration must not transfer it either: a migrated recipe
is a new basis and therefore an unreviewed candidate.

**Tests.** Recipe change invalidates; renderer revision change invalidates; unchanged candidate
re-accepts; a rejection is not upgraded by a later unrelated acceptance; a tampered evidence file
invalidates; a decision recorded against another resource id is refused; and a candidate with no
evidence cannot be accepted. Keep the suite deterministic and offline.

**Exit.** A changed recipe or changed render evidence cannot inherit a prior approval, and the reason
is reported as `stale` with the differing field named. This does not accept any musically qualified
claim; review here is a recorded human decision, not a musical verdict.

## 8. Work package D4.8 — the connected installed-resource journey (code)

**Status: implemented and published at `9711eecb`.** `tests/test_procedural_install_journey.cpp`
passes 6 of 6. Landing it uncovered a defect that made every installed procedural selection
unrenderable: distribution carried the manifest's release version and a manifest-plus-recipe digest as
the resource identity, while the renderer validates the recipe's own id, schema version and canonical
digest. `ProceduralCandidate` and `InstalledProceduralSinger` now also carry a `renderIdentity`
derived from the recipe, and resolution and selection compare against it, so a recorded selection is
an identity the renderer accepts. The journey now exports with the producer source directory and the
package both deleted.

**Why now.** Every piece exists and none of them has been driven end to end. Individual green suites
have repeatedly proved weaker than they looked on this project, so the journey is written as one
automated acceptance path.

**New file.** `tests/test_procedural_install_journey.cpp` -> target
`seam_procedural_install_journey_tests`.

**The happy path, in one test.** Create and edit a recipe -> freeze a review candidate (D4.2) ->
record an accept decision -> pack and sign -> install (D4.4) -> discover and select by identity
(D4.5/D4.6) -> tune a note with one expression channel the resource actually supports -> save ->
reopen -> export. Then **delete or rename the authoring source directory** and repeat open and export,
proving the project resolves the installed resource and not the producer's working copy.

**The failure path, as separate cases.** (a) Tampered package rejected, nothing installed. (b) Package
declaring an incompatible engine revision is present but reported `IncompatibleEngine`, and is not
offered by the picker. (c) Missing resource after deletion reports `Missing` while the project still
opens. (d) Interrupted install leaves no staging or backup directory and no half-installed resource.
(e) Exact relink resolves the same identity against a different root. (f) Intentional replacement
installs a new version side by side without mutating the previous one or an editable draft.

**Exit.** One automated journey plus the six failure cases pass with the source directory unavailable.
Real signing by a producer and fresh observation on an installed machine remain separate evidence and
are **not** claimed by this test.

## 9. Work package ADR 0022 — the procedural format decision (documentation, prerequisite)

Revision 1 required this before D4.1/D4.3 and it was skipped. The code should not be the only place
the contract lives. Write `docs/adr/0022-procedural-singer-package-family.md` recording what was
actually built: a separate typed family over the shared signed container; `formatId`
`com.project-seam.procedural-singer`, schema version 1; `.seambank` v1 interpretation unchanged;
explicit mutual family refusal; distribution release version, recipe schema version, recipe content
identity and renderer compatibility kept as distinct fields; and the consequence that a procedural
package carries data for the first-party renderer and never a package-selected executable.

## 10. Work package D3 — repair chosen by observation, under a hard cap

Starts only after P1. The failure class picks the change; the change is not chosen from a backlog.
At most **two focused cycles before a route decision**, each capped at eight active engineering
hours including targeted verification. Each cycle records the failure class, reproduction case, one
main hypothesis, the change, a counterexample, a held-out comparison and the result.

Localize before repairing. For an unclear-consonant result, separate isolated from connected, slow
from short, one pitch from the declared range, and direct from baked, because those four splits name
different owners. If gains appear only on the tuned phrase, damage other classes, or leave the
principal failure unchanged, stop that route and compare alternatives explicitly rather than
extending the budget.

## 11. Work package N1 — bounded neural feasibility

Independent of P1 and can run alongside D4.2. It is a measured experiment, not a production
milestone. The existing intake already documents the concrete blocker: `VOCODER_INTAKE.md` records an
acoustic diagnostic at 48 kHz / 80 mel / hop 256 against published vocoders at 44.1 kHz / 128 mel /
hop 512, so metadata reshaping is not a fix.

Four checkpoints: (1) a feasibility input record naming source rights, label usability and
source-group splits, or naming exactly which input is missing; (2) a small complete signal path —
prove reconstruction through a compatible vocoder **before** a costly acoustic run, then train or
adapt a small real acoustic candidate; (3) a held-out song through the shipped worker, selected in
ordinary rendering, with teardown and cancellation checked; (4) a measured route decision with
training curves, held-out observations, throughput, peak memory and render factor. Write the run
specification with step, time, memory and cancellation bounds before any sustained run. Absent
corpus rights or compute, report the missing input and continue bounded integration work only.

## 12. D5/D6 — preserved route to the complete product

Unchanged in scope, and gated on the first usable original-singer milestone. In order: the
recorded/generated bank production route (repair direct-versus-baked defects, complete marker edit ->
rejection -> retake -> fresh review, then expand approved inventory); paired style blend, which needs
matching coverage and a usable common range before any interpolation is designed; multilingual
coverage; host and standalone qualification; character performance; and finally the exact-candidate
release audit. Neural singing remains a possible practical quality dependency and is not assumed to
be the answer.

## 13. Standing engineering rules for this revision

- Reconciliation ritual per checkpoint: build -> full `ctest -j 8` -> stage by explicit path ->
  `scripts/verify_tracked_source_closure.py` must print `SOURCE_CLOSURE=PASS` -> commit -> push ->
  verify `HEAD == origin/master`. Preserve unrelated dirty files; never blanket-stage.
- Warnings are errors. An unused function or parameter fails the build.
- Each slice gets one `##` entry at the top of
  `docs/implementation/INTEGRATED_SINGER_EXECUTION.md` with `Verified.` and `Not claimed.`
  paragraphs naming the source revision and distinguishing retained from fresh verification.
- Native layout, keyboard and IME changes need native observation; human quality claims need real
  supplied observations. Waveform analysis is not listening.
- Do not add an expression channel, do not expand inventory, and do not build a new evaluator before
  the listening observation exists.

## 14. Immediate next actions

1. ~~Land ADR 0022~~ — published at `9711eecb`.
2. ~~Implement D4.2~~ — published at `5d149f22`.
3. ~~Implement D4.8~~ — published at `9711eecb`, and it located the installed-identity defect above.
4. **Obtain the P1 listening observation (section 6). This is now the only engineering-blocking item
   in the procedural route.** D3 cannot start without it, and no amount of further implementation
   substitutes for it. The expression lane is no longer untested on real material: its audible
   consequence is verified, so the remaining question is musical rather than mechanical.
5. Start N1 checkpoint 1 independently.

The natural next code work, if capacity is available while waiting on listening, is to connect the
D4.2 decision type to the authoring session so a creator can record a decision through the
application rather than only through the library, and to expose trust, qualification and applicable
controls in the installed-singer chooser. Neither unblocks the musical question, and neither should
be allowed to displace the listening gate.

## 15. Schedule, stated bounded rather than precise

Effort to the **first usable original singer** is now dominated by D4.2 + D4.8 + one observed D3
cycle: small, well-scoped code work plus human latency. Effort to **Full-Scope Beta GO** is dominated
by items this plan cannot yet size honestly — corpus rights, measured neural learning, multilingual
phonemization and the qualification matrix — and any single figure for it would be invented. The
honest statement is that the procedural route is close to a demonstrable milestone, and the
remaining scope is gated on evidence that does not exist yet.
