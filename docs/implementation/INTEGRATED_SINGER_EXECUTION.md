# Integrated Singer Execution

## A creator's timbral nudge reached the document but never the renderer

September 16, 2026 — U1.3a of the jointly agreed R4 plan, and the defect that writing its test exposed.

The unit was to cancel a render mid-edit inside the song journey. Driving the edit through the
controller's own command turned up something else: `nudgeFormantShift` advanced the document's
revision while the coordinator's requested revision did not move at all. **The edit was stored and no
render was ever asked for.**

The mechanism is one skipped notification. The twelve timbral nudge and reset commands write through
`EditorSession::executePerformanceResult`, and that path does not run the authoring runtime's
after-command step, which is what turns a document change into a render request. Every other
application-level edit reaches the renderer through `markDocumentChanged`, and these twelve were the
only writers that never called it. A creator pressing the formant nudge would have seen the value
change, saved it, and heard the previous phrase; the change was real, durable and inaudible.

`NativeEditorController::commitTimbralEdit` now exists for this, wrapping all twelve call sites in one
place so a future channel cannot be added without it. The renderer debounces, so a burst of nudges
still coalesces into one render rather than one per keystroke.

This is the class of defect a command-level test cannot see on its own: the fixture asserted the stored
curve, the undo stack and the refusal paths, and all of those were correct. What was missing was the
question of whether the edit reached the audio, and that question only got asked once the journey
looked at the renderer instead of the project.

**Cancellation is now connected in the journey too.** The song test nudges the formant channel,
asserts the document revision and the renderer's requested revision advance together, waits for the
request to be in flight, asserts the superseded phrase is marked stale and refused by
`acquireCurrent()`, cancels, asserts the cancellation is reported as cancelled rather than failed and
that nothing cancelled became current, then retries through the application's own preview request and
asserts publication catches up to the document's revision with the source-filter singer's audio.

Verified. `seam_original_singer_song_journey_tests` 2/2 across five consecutive runs; the full
registered run passes 172/172; `SOURCE_CLOSURE=PASS`.

Not claimed. The cancellation assertions observe the coordinator's published progress through the
application path rather than injecting a render hook; the hook-injected cases in
`test_authoring_render_coordinator.cpp` still own the fine-grained ordering cover. The creator-workflow
observation remains NOT_OBSERVED and no musical judgement is made: in particular, this repair makes a
tuning edit audible, and whether that edit sounds good is exactly what nobody has listened to yet.


## The neural path can now be exercised from the project's own renders, with its limits named

September 16, 2026 — M3.1/M3.2 of the jointly agreed R3 plan. The neural pipeline was blocked on an authorized corpus: training needs a source with permission, admitted labels, and an admitted vocoder. A procedural render supplies the first two in a form no external recording can, because the renderer knows which phone it produced and when, so its phone timeline is a plan rather than an annotation someone had to make. That makes a bounded teacher/student experiment possible now instead of after a corpus is acquired.

`tools/voice_model_training/generated_teacher.py` turns a captured render into the exact label and score documents the existing preparation pipeline already admits, so no downstream stage needs a special case. It is reachable as `python3 -m tools.voice_model_training generated-teacher-labels`, and the output it writes is read by the pipeline's own `inspect_label_config`, which is what the test asserts rather than a private copy of the rule.

Two limits are built into the module because they are the part that would otherwise become a false claim. The emitted phone spans are the renderer's **intent**: they say which phone the engine meant to produce over a span, not that the audio acoustically contains that phone with that boundary. Training on them teaches the student the teacher's articulation, so they can measure whether a student reproduces the teacher and cannot establish that either is phonetically correct; the export records `labelOrigin` as `renderer-intent-not-acoustic-truth`. And permission is still required: `sourceRightsAdmitted`, `labelsAdmitted`, `trainingAdmitted` and `releaseEligible` are all false, and the label configuration carries exactly the seven fields the admission step defines so it cannot smuggle an approval of its own.

The adapter refuses a gap in the phone alignment rather than inferring one, refuses voicing that disagrees with F0, and requires score notes and rests to partition the source frames. Those are the same conditions the admitted validators enforce downstream, applied at the point where the material is created.

Verified. `tools/voice_model_training/test_generated_teacher.py` 8/8 under the registered `seam_voice_model_training_tests` suite, which now runs 86 tests; a generated export round-trips through `inspect_label_config` and produces a schema-3 configuration with one source and one label. A real CLI invocation was run against a captured export and produced the expected configuration. `SOURCE_CLOSURE=PASS`.

Not claimed. No model was trained, no corpus was acquired and no vocoder was admitted, so there is still no learned singer and R9 is untouched. The teacher's own intelligibility is unverified and unknown; a student that matches it would therefore match an unmeasured target. The experiment this enables tests reconstruction, distinguishability and conditioning sensitivity, and none of those is a perceptual identity or musical-quality claim.

## An original singer renders a whole lyric song, and the route it will use is decided once

September 16, 2026 — M1.1–M1.3 of the jointly agreed R3 plan. Two things landed together because they are the same question asked twice: what will render this track's sound, and can a creator actually finish a song with it.

**A latent capability defect was real and is closed.** `RendererCarrier` had only `SampleBank` and `SourceFilter`, and `rendererCapabilities` returned the source-filter control set for anything that was not a sample bank, so a neural track resolved to the full set including the six timbral channels while `render_snapshot.cpp` refuses all six on neural. The lane and the renderer disagreed, and the moment a neural singer became selectable the surface would have offered a control the renderer rejects. The carriers are now exhaustive — `SampleBank`, `SourceFilter`, `Neural` — and each states its own controls, so a new carrier must declare them rather than inherit them by falling through an `else`. Neural consumes the shared compiled pitch, timing, dynamics, vibrato, attack and release path and refuses the six timbral channels by name, exactly as the sample bank does, until an admitted execution contract is shown to consume a control.

The decision now lives in one place. `rendererCarrierFor(track)` replaces the sample-versus-else test at eight call sites in the editor controller and the expression lane, and `resolveSingerRoute` derives the carrier, its capabilities, its availability and its status from the recorded singer. `ResolvedSingerRoute` distinguishes code support, declared coverage, availability and review, because a runnable resource can still be unreviewed and a reviewed one can still be built for an engine revision this build does not render. It is derived on demand: no project schema version was added, because capability is a function of the singer and the region rather than a property to persist.

The picker now states the answer before the choice. `installedSingerOffers()` carries a `capabilitySummary` built from the same resolver, naming the carrier, the controls the singer will actually apply, its declared language and whether a review covers it, so a creator is not told no one control at a time after writing a phrase.

**One installed original singer sings a whole authored lyric song.** `seam_original_singer_song_journey_tests` installs a signed original singer, selects it through the application command, writes a 24-note Japanese lyric song with consonants, rests, unequal durations and a sustained final vowel through the real add-note command, and exports it. The export is decoded and required to carry real signal rather than a header. This extends the covered install journey rather than repeating it.

**Tuning survives undo, save, reopen and export.** The same test draws a formant curve through the expression lane, confirms the exported master changed, undoes and confirms it returns identical to the baseline, redoes, saves, reopens in a fresh session and exports again, requiring the reopened export to equal the tuned export. A re-render that silently lost the installed singer would produce different audio, so the comparison is meaningful rather than merely non-empty.

This is what that journey found: **every committed export pushed a renderer-provenance record onto the creator's undo stack.** The record is metadata, a disclosure about audio rather than a change to it, so the creator's next undo appeared to do nothing, and a project exported repeatedly collected one such entry per export. `recordExportedRendererProvenance` now records only when the renderer it would name differs from what the project already records. The disclosure still happens the first time and after a real renderer change; a no-op record no longer interleaves with the user's edits. This was a real product defect in the owning layer, not a test artifact, and it is the class of thing this journey exists to catch.

Verified. `seam_renderer_capability_tests` 6/6 including the per-carrier matrix that pins all 13 controls for sample, source-filter and neural and asserts neural is not aliased to sample; `seam_singer_route_tests` 8/8; `seam_original_singer_song_journey_tests` 2/2; the full registered suite passes 172/172; `SOURCE_CLOSURE=PASS`.

Not claimed. No listener has judged this song, so no intelligibility, identity or musical-quality claim is made. The fixture's recipe parameters are development screening values, not phonetic qualification. No human has yet performed this session unaided, so the creator-workflow observation remains open. The song is a Japanese lyric test, not reviewed JP/EN/KR coverage. Neural remains mandatory under R9 and is not satisfied by this work.

## IME composition is checked at the minimum window, closing D2's last uncovered clause

September 15, 2026 — D2's exit named IME focus at the minimum window as uncovered. The existing
batch-lyrics case ran at 1280x720, so nothing established that opening composition at the enforced
minimum still anchors the composer somewhere real, or that the input closes exactly once. A composer
anchored off-surface, or left open after a commit, is a defect a comfortable window would hide.

The new case drives the real keyboard route at 1024x768: Shift+L over a selected note opens the
composer, and the rectangle the host receives must be non-empty, start inside the window and end
inside it, because an empty or off-surface anchor is how a composition box becomes invisible while the
state still says it is open. It then cancels and confirms the input closed exactly once and wrote
nothing, reopens, commits, and confirms the input closed again, the review opened, and the committed
lyric is the one that reached the project.

Verified. seam_tests passes 911 of 911 and the registered run passes 170 of 170.

Not claimed. This asserts a usable anchor rectangle and a single close per composition, not a pixel
result and not how a real input method renders at that size; no human has driven an IME at the
minimum window and no assistive technology has been run against it. It also records which route ran:
the request deliberately carries no single lyric identity, because distributing lyrics over a
selection is not one token's edit. No U-unit acceptance or Beta GO state changes.

## A renderer change is recorded, reported, and kept out of the audio identity

September 15, 2026 — D4.7 closed its last gap: nothing recorded which renderer produced a project's
sound, so a renderer change would have migrated the sound silently.

The project now carries the renderer that last produced audio it heard or exported, as the renderer's
own ABI identity plus the compiler revision inside it, persisted at schema 18. A project that records
nothing reads as unknown rather than same, because nothing observed how it sounded and claiming
equivalence would pass a compatibility check nobody earned. When the record differs from this build,
the application raises a notice that names the differing field instead of asserting only that
something changed, and dismissal is keyed to that exact description so a notice cannot reappear every
frame and a genuinely new difference still speaks.

Two design hazards decided the shape. The render completion callback runs on a render worker, so the
record is an undoable edit applied by the owner thread: the synchronous export path applies it
directly, and the background path reports a committed export through a pending flag that the owner
thread consumes once per frame. And the sample, procedural and neural identity builders, the CLAP
offline identity and the export receipt all hash the encoded project, so encoding gained an explicit
option that omits the record. Which build made a sound is a fact about audio that already exists, not
an input to what that audio is; including it would have let the act of recording a renderer change the
identity that decides cache reuse. Recording provenance is a metadata edit, so it schedules no render
and invalidates no audio.

Verified. seam_tests passes 910 of 910 including a new serialization case (the record round-trips, an
older schema loads as unknown, a half record and an oversized identity are rejected, a schema-18
document without the field is malformed, and a stamped project and an unrendered one produce
byte-identical identity encodings) and a new controller case (an unrendered project reports unknown, a
committed export records this build, a foreign record reports changed with the field named, recording
is one metadata revision that leaves the previous master hash untouched, and the background hand-off
applies exactly once). The registered run passes 170 of 170 and SOURCE_CLOSURE=PASS.

Not claimed. No renderer change has been observed by a person: the changed-renderer path is exercised
by writing a foreign identity into the project, and the notice's presentation has been compiled and
unit-tested but not seen on screen with a real build. This records and discloses provenance; it does
not preserve the old renderer's behaviour, and it transfers no acoustic qualification. No U-unit
acceptance or Beta GO state changes.

## A review can be recorded from the application now, not only from the library

September 15, 2026 — native review action added, closing the last bounded item in the plan's procedural
route. The store, the controller methods and the review status were all in place and tested, but a
creator still needed a developer to record a decision. Reading D4.2's own exit condition — that a
prototype review can be recorded and read — the missing half was the entry point.

`IFileDialog` gains a procedural review input and the AppKit adapter implements it with a reviewer
identity field, an explicit accept or reject choice and file pickers for the score and audio that were
reviewed. The identity is required rather than optional, because a recorded decision is a human
attribution and a blank one would store an approval nobody owns. `ReviewInstalledSinger` is a File-menu
item that reaches `reviewInstalledSingerFromDialog`, which names the exact resource in the summary the
reviewer sees, stamps the decision in UTC in the same format the rest of the product's journals use,
and derives a review id from the decision, the reviewer and the resource content hash so the same
person recording the same decision twice is a duplicate rather than a silent second approval.

Verified. `seam_procedural_install_journey_tests` passes 15 of 15 with the new case: the action is
refused with no dialogue when no singer is selected; a cancelled dialogue opens the summary but writes
nothing and creates no store file; a real review records an approval that reads back as accepted and
appears as reviewed in the offer listing; repeating the same decision is refused as a duplicate; and an
acceptance naming missing evidence is refused and does not disturb the stored approval. The registered
run passes 170 of 170.

Not claimed. The dialog is driven by a test double here, so the AppKit presentation, the reviewer field
and the two file pickers have been compiled but not exercised by a person. No human has reviewed a
procedural singer; every decision in these cases was written by the test. This is review plumbing, not
qualification. No U-unit acceptance or Beta GO state changes.

## The journey's export is decoded, not merely measured

September 15, 2026 — export assertion strengthened. The connected journey checked that the exported
master existed, exceeded a 44-byte header and had a digest. A file larger than a header can still be
silence, so an export that produced an empty render would have passed the very test written to prove
the workflow works.

The master is now decoded and required to carry real signal: a 48 kHz rate, at least one channel, a
non-empty sample buffer, finite samples, a peak above the noise floor and nonzero energy. This also
confirms the ordinary render path really produces audio for an installed singer, which the earlier
size check only assumed.

Verified. `seam_procedural_install_journey_tests` passes 14 of 14 with the decoded master, and the
registered run passes 170 of 170. The installed singer selects, tunes, saves, reopens with the
producer's source directory and package deleted, and exports audio that is measurably non-silent.

Not claimed. Passing samples and nonzero energy are not intelligibility, identity or musical quality.
No human has listened to this output, which remains the unreviewed D1 question. No U-unit acceptance
or Beta GO state changes.

## A review file beside the singers is not mistaken for one of them

September 15, 2026 — placement risk checked. The review store lives in the same directory the singer
catalogue scans, because both are user-owned resource data under the support root. That is a real
collision risk introduced by choosing those paths: `reviews.json` and any stray file sit where the
catalogue walks looking for products.

The scan skips anything that is not a real directory, so a plain file is ignored, and the new case pins
that rather than leaving it as a reading of the code. It installs a real singer, creates a real review
store and a stray text file in the same root, and asserts that exactly one singer is discovered with the
right id and trust. The accident this guards against only appears once both features are switched on
together, which is exactly the situation the shipped application is now in.

Verified. `seam_procedural_package_tests` passes 12 of 12 and the registered run passes 170 of 170.

Not claimed. This checks that the catalogue is not confused by the files; it does not test a singer
root containing a partially written store after a crash, which the store's own atomic write is designed
to prevent. No U-unit acceptance or Beta GO state changes.

## The lane's accessible value is asserted, not assumed from the painting code beside it

September 15, 2026 — D2 accessibility coverage added. The plan requires a full accessible value text
for the expression lane, and the automation lane is the row a reader who cannot see the curve depends
on. The code existed in `editor_semantics.cpp`, but no test asserted what it reports, which is how an
accessibility surface silently regresses to an empty string.

The new case reads the semantic tree the way an assistive client would, finds the automation lane node
by its id, and checks the reported value names the channel, the channel's own unit, the point count
and the value at the playhead. It then repeats the check for a channel the selected carrier refuses and
asserts the stored curve and the refusal are still reported, so a refused channel is described rather
than dropped. Finding the node by id rather than by display name also fixed a wrong assumption in the
test itself: the lane node is named `pitch lane`, so matching on the bare lane key found nothing.

Verified. `seam_expression_lane_tests` passes 10 of 10 and the registered run passes 170 of 170.
Reading the description also confirms a refused channel is described as an editable timbral curve only
when the carrier actually supports it.

Not claimed. This checks what the tree reports, not how a screen reader presents it, and no assistive
technology has been run against the lane. IME focus behavior at the minimum window remains uncovered.
No U-unit acceptance or Beta GO state changes.

## The expression lane is checked at the minimum window, not only at a comfortable one

September 15, 2026 — D2 layout coverage added. The lane's exit in the plan asks for focused semantic
and layout checks at the enforced minimum window, 1024x768, and at 1280x800, because a surface that
paints correctly only at a large size is not yet a usable one. The existing painter case ran at
1440x900 and only when an environment variable asked for a capture, so the minimum size and the
channel identity across a resize were untested.

The new case paints the real scene at 1024x768 and at 1280x800, keeps the stored curve and the
channel's own unit and label across both sizes, and then walks all six channels at the minimum size
asserting each one reports its own label and unit. It also paints at the minimum size with a long
Japanese project and region name, because long text is where a compact band runs out of room first and
the product's pilot languages produce exactly that.

Verified. `seam_expression_lane_tests` passes 9 of 9 and the registered run passes 170 of 170. The
assertions are about what the scene reports and whether it paints without failing, not about a
particular pixel result, so they catch a collapsed or overflowing layout rather than pinning a
decoration.

Not claimed. This is a semantic and paint-level check, not native visual observation: no human has
looked at the lane at these sizes, and the plan treats native screenshots and human review as separate
evidence. IME focus behavior and the accessibility tree at the minimum size are not covered here.
No U-unit acceptance or Beta GO state changes.

## A recorded review is now visible where the singer is chosen

September 15, 2026 — review surfacing added. The decision store could record and re-read an approval,
but nothing consumed it: the receipt was write-only, which is the same defect class as the earlier
unreachable capabilities on this project. A review nobody can see at the moment of choosing a singer
does not help anyone decide anything.

`InstalledSingerOffer` now carries `reviewed` and a `reviewDetail` reason, and the offer listing reads
the store so a creator sees whether the resource they are about to use has actually been reviewed. The
report is honest about the failure cases: a decision that no longer matches the resource is described
as no longer matching rather than as approved, a rejection is described as a rejection, and a store
that cannot be read degrades to unreviewed with a reason rather than failing the whole listing.

Verified. `seam_procedural_install_journey_tests` passes 14 of 14 including the new case: an
installation with no decision is offered as selectable and unreviewed; after a real review is recorded
the same listing reports it as reviewed with no caveat; and installing a second version with different
content leaves exactly one of the two versions reported as reviewed, because the approval is bound to
the exact content identity rather than to the producer's version string.

Not claimed. No human has reviewed a procedural singer; the decision in this case was written by the
test. Surfacing review status is not qualification, and a reviewed resource is not thereby a good or
releasable one. There is still no native menu action that records a decision, so a creator can read
review status but cannot yet create one without a developer. No U-unit acceptance or Beta GO state
changes.


## The shipped application can now reach the installed singer it catalogues

September 15, 2026 — reachability defect found and repaired. Every D4 checkpoint passed its own suite,
and none of it was reachable from the application the project actually ships. `NativeEditorApp`
constructed the controller without setting `proceduralSingerRoots`, the renderable engine identity or
the review store path, so in the shipping build the installed singer picker had no roots to scan, the
engine was undeclared so selection refused, and there was nowhere to keep a review decision. A
capability that exists only in tests is not a product capability, and the earlier defects in this slice
were all of this kind.

`ApplicationPaths` now resolves `proceduralSingerRoot` and `proceduralReviewStorePath` beside the
sample-bank roots, under user data rather than the installation, because a procedural singer is a
user-installed resource like a bank and the installation may be read-only. The review store is a file
inside the singer root, so removing the singers removes their decisions instead of leaving approvals
for resources that no longer exist. `NativeEditorApp` supplies both, along with the engine identity,
from one declaration.

The engine identity was also a magic number waiting to drift. `voice_design::kSourceFilterEngineId`
and `kSourceFilterEngineRevision` now name what a recipe targets and what this build renders, and the
recipe's own default engine id derives from the constant, so the value a package declares and the value
a build accepts come from one source of truth.

Verified. `seam_tests` passes 908 of 908 including a new case: the procedural singer root and its
review store are absolute, both live under the user data root, the review store is a file inside the
singer root, and the singer root is distinct from both the bank root and the user data root itself. The
full registered run passes 170 of 170 and the shipping `seam_editor_native` bundle builds with the
wiring in place.

Not claimed. This proves the application configures the roots and the engine identity; it does not
prove a creator can reach the picker through a menu, because no native action opens it yet, and no
installed singer has been selected in the shipping build by a person. No U-unit acceptance or Beta GO
state changes.


## A review decision now survives a restart and is reachable from the application

September 15, 2026 — D4.2 completed. The review binding existed as a library type, which meant a
decision could be validated but not kept: there was no store, so nothing survived a restart, and the
authoring session exposed no way to record or read one. A decision that cannot be persisted is not a
review, it is a calculation.

`ProceduralReviewStore` is a durable, append-only store of decisions keyed by the exact basis they
were made about. It reuses the same validation a direct `recordProceduralReviewDecision` call gets, so
a stored decision is one that could legitimately have been recorded: a forged digest, an anonymous
reviewer and an acceptance with no evidence are all refused before the file is touched. A duplicate
review id is refused rather than appended, an unknown stored kind is refused rather than read as a
rejection, and a corrupt store is reported rather than replaced, because it is the only record of what
a reviewer approved.

Two real defects were found while landing it. The store's read-modify-write was initially unsynchronized
after the type's mutex prevented the store from being moved into a `Result`; it now holds an OS file
lock across the load and the atomic publish, so a second process is excluded as well as a second
caller, and a concurrency case pins that no successful record is lost. Separately, the store dropped
the basis a decision was made about, so a decision could not be resolved after a restart at all — it
would have been reported as carrying no basis. The basis is now persisted through its own codec and is
required on both write and read.

`StandaloneApplicationController::reviewInstalledSinger` and `installedSingerReview` connect the store
to the application for the installed singer a track actually selected: the candidate is built from
that exact resource, the evidence digests are computed from the supplied files, and the caller's own
basis digest is replaced with the candidate's so a forged one cannot take effect. Reading back needs no
fresh evidence. A build with no configured store reports that rather than recording nothing.

Verified. `seam_procedural_review_tests` passes 14 of 14 and `seam_procedural_install_journey_tests`
passes 12 of 12. The store keeps decisions across reopen, refuses a duplicate id, appends a second
decision and lets the later one decide; refuses every decision the library rules refuse without
creating a file; reports a corrupt, wrong-family or unknown-kind store instead of replacing it; retains
every successful record under eight concurrent writers; and through the application, reviewing without
a selected singer or with missing evidence fails, a real review is recorded and read back with the
same basis digest, and a forged digest does not become the stored one.

Not claimed. No human has reviewed a procedural singer; every decision in these cases was written by
the test. The store is not yet driven from a native menu action, so there is no creator-facing control
for it, and no evidence in this checkpoint is a musical judgment. Signing remains authenticity, and a
stored acceptance is a recorded human decision, not a qualification. No U-unit acceptance or Beta GO
state changes.


## The review step is now inside the installed journey, not beside it

September 15, 2026 — D4.8 completed. The connected journey covered authoring, packaging, installation,
selection, tuning, reopen and export, but the plan's happy path also runs a review candidate and a
recorded acceptance between authoring and packaging. The D4.2 review suite exercised that binding on
its own, which is exactly the pattern that hid the installed-identity defect earlier: separately green
pieces that were never driven together.

The new journey case freezes a review basis from the installed singer's own render identity and the
evidence a reviewer examined, records an acceptance against it, and then checks the approval survives
into the installed material. It then does what a creator does next — copies the installed singer to a
draft and edits the recipe — and asserts the approval does not follow. That distinction is precise:
with a changed recipe the edited copy is a *different candidate*, so the original decision is not its
evidence and is neither current nor stale, while the same candidate whose reviewed material changed is
stale. Writing the case exposed that I had conflated the two, and the assertions now pin both.

It also confirms that an acceptance cannot be reattached to edited material: recording a decision
whose carried basis still describes the old recipe is refused even when the recorded digest is updated
to match, because the carried basis and its digest must agree.

Verified. `seam_procedural_install_journey_tests` passes 11 of 11. The new case proves the frozen
basis resolves as accepted with no stale entries; the installed resource's render identity equals the
reviewed recipe digest, so the approval is about the material that was actually installed; editing a
copy yields a different identity whose receipt is unaccepted with no current entries; the same
candidate with changed material is stale with one entry; and reattaching an approval to the edited copy
is refused.

Not claimed. This exercises the decision store as a library. The review decision is still not exposed
through the authoring session, so a creator cannot yet record one from the application, and no human
has actually reviewed a procedural singer. Every decision in these cases was written by the test.
No U-unit acceptance or Beta GO state changes.


## A new render can now be compared against the retained packet, and the tool refuses to rank them

September 15, 2026 — listening regression added. Section 6.3 of the revised plan asks for a compact,
durable, versioned listening reference with new output generated alongside it rather than in place,
and for a byte difference to request investigation rather than veto a deliberate improvement. The
packets retained their hashes but nothing compared two of them.

`scripts/compare_listening_packets.py` compares a newly rendered packet, or a re-rendered artifact
tree, against a retained reference. It matches outputs by case, variant and file role, reports
identical, changed, missing and added separately, and for a change reports which identity fields
differ — the recipe hash, sample rate, channel layout or frame count — so a difference is attributed
rather than left as an unexplained hash. It never writes to the reference, and it refuses to overwrite
an existing report. Its recorded verdict is `UNRANKED`, because no hash and no distance metric can
establish that a voice got worse.

Verified. `tests/test_listening_packet_comparison.py` passes 7 of 7 and is registered as
`seam_listening_packet_comparison`: identical packets report identical; a changed output is reported
with its changed identity field named on both sides; missing and added outputs are distinguished; a
re-rendered tree is located through the reference's own declared paths; a partial rerender is scoped
to one case; the tool exits 3 on a difference, leaves the reference byte-identical and writes an
`UNRANKED` report that it refuses to overwrite; and the D1 packet compares to its retained
`2026-09-15-d1-repeat` rerender as 6 of 6 identical.

Not claimed. The rerender comparison confirms reproducibility for an unchanged deterministic
configuration, which is repeatability and not quality. No reference was promoted, no listening
observation exists, and the tool cannot say whether either packet sounds better. The partial-rerender
case currently covers only the song case, because that is what the retained rerender contains. No
U-unit acceptance or Beta GO state changes.


## The vocoder bridge runs at SEAM's own profile, and that was not previously shown

September 15, 2026 — N1 checkpoint 2's corpus-free step executed. The plan asks for a small complete
signal path that proves reconstruction before a costly acoustic run, and the intake document names the
cheapest honest version of that: build the generator at SEAM's own feature profile and check exact
dynamic output lengths and architecture assumptions, before any training is sized against it.

The pinned MiniNSF generator instantiates at 48 kHz, 80 mel bins, hop 256, n_fft 1024, 20–24 kHz mel
bounds and an [8, 8, 2, 2] upsample product — SEAM's target, not upstream's 44.1 kHz / 128-bin /
hop-512 default. It exports to ONNX, passes native graph inspection, and runs in ONNX Runtime at 1, 3,
16 and 23 frames, producing exactly 256, 768, 4096 and 5888 samples. PyTorch and ONNX Runtime outputs
agree to between 6.5e-09 and 2.8e-08 maximum absolute error, which is float32 accumulation noise rather
than a modelling difference. Strict state loading succeeded for both the full and MiniNSF
configurations.

One real environment defect was found and repaired: the pinned upstream utils module imports
`matplotlib` only to set the Agg backend, and the local model environment did not have it, so the
documented command could not run at all. Installing it into that environment was required to execute
the check; nothing else about the host or the graph changed.

Verified. `docs/implementation/evidence/neural-vocoder-bridge-2026-09-15/` retains the full report and
the command's stderr with per-file digests, and `tools/voice_model_training/test_feasibility_inputs.py`
passes 8 of 8, including that the retained hashes still match their bytes and that the manifest claims
no more than was proved. The run reports `passed: true` with `syntheticInputs: true`,
`singerQualified: false` and `releaseEligible: false`. The three stderr warnings are a torch weight_norm
deprecation, a notice that the legacy TorchScript exporter is still the default, and unapplied constant
folding for opset-10 Slice steps; the export succeeded and every runtime case ran.

Not claimed. This is a geometry and numerical-parity diagnostic on synthetic input. It is not GAN
training, not a vocoder qualification, not an admitted bundle, and not evidence that any voice is
intelligible. No checkpoint was trained, no corpus was used, and nothing was rendered through the
shipped worker. The remaining neural checkpoints still need an authorized corpus, admitted labels and
a rights decision that the feasibility record reports as absent. No U-unit acceptance or Beta GO state
changes.


## The neural lane now says which inputs are missing, and nothing more

September 15, 2026 — N1 checkpoint 1 added. The plan's first neural checkpoint is a feasibility input
record: name what a bounded learned-singer experiment needs, and state which of it exists. The risk
is that such a record becomes a paragraph asserting readiness, so it is written as a machine-checked
gate instead.

`docs/implementation/NEURAL_FEASIBILITY_INPUTS_2026-09-15.json` declares six required inputs with a
status, a location and a binding — a digest for file-shaped inputs, an exact revision for a source
pin. `scripts/verify_neural_feasibility_inputs.py` checks that a declared-present input is actually
present, that its binding is well formed, and that a corpus digest matches the file when one is
required. It exits 3 with `FEASIBILITY_INPUT=INCOMPLETE` while anything is missing, which is the
current and expected state, and 2 for a malformed record.

The record's honest result: four of six inputs are absent. There is no authorized singing corpus
(`tests/singing_quality/corpus` holds four diagnostic scores and a notice that already states it
cannot demonstrate intelligible singing), therefore no admitted labels, no admitted vocoder, and no
training permission manifest with byte-bound evidence. Present are the pilot profile, which is not the
48 kHz / 80-bin / hop-256 training profile, and the pinned upstream checkout with a working local model
environment. The record also lists existing capability explicitly, so missing inputs are not confused
with missing code: the DDPM training primitive, ONNX acoustic export, bundle admission, the shipped
worker and the qualification command all exist, and none of them is a learned singer.

Verified. `tools/voice_model_training/test_feasibility_inputs.py` passes 6 of 6 inside the existing
`seam_voice_model_training_tests` target: a missing declaration is reported rather than ignored and
every required input is named; an unverifiable presence claim is treated as absent while a revision-
bound source pin is accepted; an unrecognised status is refused as an authoring error rather than
silently read as absent; a present input that is not on disk and a corpus digest mismatch are both
reported; and the gate contains no training invocation or subprocess path. The gate reports
`missing=corpus,labels,vocoderProfile,permissionEvidence` on the record as committed.

Not claimed. This is an input inventory, not training admission, corpus approval, expenditure
authorization or a quality result. No training run was started, no model was trained, no vocoder was
admitted and no singing audio was produced. The record's bounded next action — a model-only vocoder
bridge at SEAM's own profile, to catch a feature mismatch before a long run is sized against it — was
not performed. No U-unit acceptance or Beta GO state changes.


## The expression lane is audible on a real song, not only drawn

September 15, 2026 — D2 exit closed. The lane already drew, persisted and refused correctly, but its
exit condition asks for something stronger: that a creator changes audible expression on the song and
that the selected resource really supports the edited controls. Nothing had established that drawing a
curve changed the audio, and the D1 packet's retained song is a source-filter singer, so the condition
was testable without a listener.

`tests/test_expression_on_song.cpp` renders one two-note phrase through the production path
(`RenderSnapshotFactory::createProcedural` then `PhraseRenderPipeline`), edits one channel at a time,
and renders again. Rendering the same project twice is bit-identical, so any later difference is the
edit. It then measures the relative energy difference rather than asserting a directional claim about
the change, because how the edit sounds is a listening judgment and this is not one.

Verified. `seam_expression_on_song_tests` passes 3 of 3. All six channels — formant, breathiness,
tension, airiness, gender and growl — move the rendered audio by more than the noise threshold when
edited on a source-filter singer, so no drawn curve is silently discarded. A sample carrier refuses
all six by name with the channel's own label in the message, and the same project with the procedural
selection recorded allows all six, which is the applicability distinction the plan requires. Stored
curves keep their own unit — semitones for formant, a normalized share for growl, bipolar for gender —
and a stored formant curve changes the audio of a render that otherwise has none.

Not claimed. This proves the edit reaches the audio and that applicability is enforced. It does not
claim the six channels sound good, that they are musically useful, or that a creator would reach for
them; that remains the unreviewed listening question in the D1 packet. The two-note test phrase
reproduces the retained song's shape rather than loading the retained project file itself. No U-unit
acceptance or Beta GO state changes.
## A signed singer this build cannot render is named, not hidden

September 15, 2026 — D4.6 trust and reason surfacing added. Selection previously kept unusable
singers out of the chooser entirely, which meant a creator who had correctly installed a signed singer
for another engine saw only that nothing could be used, with no way to tell that apart from having
installed nothing.

`StandaloneApplicationController::installedSingerOffers` now reports every installed singer with the
resolver's own status, reason and a selectable flag, and both selection and the refusal message are
derived from that one listing so the two cannot disagree. When nothing is usable the refusal names
each blocked singer and why, for example that it needs `seam.source-filter.other` while this build
renders `seam.source-filter.v1`. The chooser itself is unchanged: only renderable, accepted resources
are offered.

Verified. `seam_procedural_install_journey_tests` passes 10 of 10, including the new case: a singer
installed for another engine is reported present with `IncompatibleEngine` and a reason naming the
engine it needs, the refusal message names the singer and the engine, the same installation is
selectable once the build declares the right engine and is reported as `TrustedInstalled` rather than
as a development fixture.

Not claimed. This surfaces what the resolver already decided; it does not add a qualification status,
because no reviewed procedural singer exists yet. The chooser still presents a flat list rather than
grouped trust and capability detail, and no creator has been observed using it. No U-unit acceptance
or Beta GO state changes.
## A creator edits a copy, and the signed installation cannot be rewritten

September 15, 2026 — D4.6 copy-to-edit added, completing the substep. The plan requires that a
creator selecting an installed procedural singer can edit a copy without mutating signed content.
Selection alone did not provide that: the previous slice let a creator select an installed singer,
but there was no supported way to change one, and an open save path could have targeted the
installation directory.

`copyInstalledSingerToDraft` in `libs/seam-distribution/src/procedural_package.cpp` reads the installed
recipe, re-verifies it against the candidate's own render identity so a file replaced since discovery
is refused rather than silently copied, writes the canonical encoding to a creator-owned path, and
refuses any destination inside a protected root. The containment test resolves both paths weakly
canonically, so a symlinked or differently spelled destination cannot escape it. The resource's own
installation directory is protected even when the caller passes no root list, so the operation cannot
rewrite signed content by construction rather than by convention.

The application reaches it through `StandaloneApplicationController::copyInstalledSingerToDraft` and
the new `CopyInstalledSingerToDraft` command and File-menu item. The command resolves the track's
recorded identity through the same exact-identity resolution selection uses, refuses a draft inside
an installation root, writes the copy, then selects the draft as one undoable change so the project
follows creator-owned bytes and can be undone back to the installed singer. Cancelling writes
nothing. The draft starts as the same voice, because that is what the creator chose to edit; editing
it is what produces a new identity.

Verified. `seam_procedural_install_journey_tests` passes 9 of 9, including the three new cases: a
copy is byte-identical in the installation before and after copying, editing the draft produces a
different content identity with the same resource id while the installation still hashes unchanged;
a destination inside the install root or the resource's own directory is refused as a conflict and an
existing draft is not silently overwritten; and through the application, copying without a selected
singer is refused, cancelling writes nothing, a draft inside the install root is refused, and a real
copy is created, recorded on the track, undoable, and leaves the signed installation byte-identical.

Not claimed. No creator has been observed making a musical change to a copy, and no listening
judgment exists, and no creator has been observed reaching for the copy action unaided. The Voice
Designer's save path is now guarded too: `VoiceDesignerSession::beginSave` refuses a destination
inside a declared protected root and also detects an installed singer from its own layout, so a save
aimed at an installation is refused even when the caller declares no root. `seam_voice_designer_tests`
passes 38 of 38 with both cases. That guard protects the paths this repository exercises; it is not a
filesystem-level immutability guarantee against someone with direct access to the directory. No
U-unit acceptance or Beta GO state changes.
or Beta GO state changes.
## The installed procedural route is now connected end to end, and it was not before

September 15, 2026 — D4.8 added, and it found a real defect. The separate pieces of the procedural
slice were each green, but nobody had driven them as one journey. The journey test did what it was
written for: it proved that a project selecting an installed procedural singer could not have sung
with it.

The cause was an identity mismatch at the seam between distribution and rendering. The installer and
catalogue describe a resource with the *distribution* identity: the producer's release version, such
as `1.0.0`, and a digest over the installed manifest and recipe. The renderer validates the *recipe's*
identity: the id inside the recipe, the recipe's own schema version, such as `1`, and a digest over
the recipe's canonical encoding. The installed selection recorded the first and the renderer checked
the second, so `loadVoiceRecipeResource` refused every installed singer with "Recipe file does not
match the requested singer resource identity". Every installed procedural selection was unrenderable,
and no existing test could see it because each layer was only ever tested against its own identity.

`ProceduralCandidate` and `InstalledProceduralSinger` now carry a `renderIdentity`, derived from the
recipe exactly as the voice-design layer derives it, and `resolveProceduralSinger` and installed
selection compare against that identity. The distribution identity remains available and unchanged;
the two are simply no longer confused. The renderer accepts what an installed selection records, and
the journey exports with the producer's source directory deleted and the package removed.

Verified. `seam_procedural_install_journey_tests` passes 6 of 6: the recorded identity loads and
equals what the renderer validates; a creator selects an installed singer, tunes the formant channel
on a new note, saves, then reopens and exports after both the producer source directory and the
package are deleted; an untrusted signature installs nothing and leaves no directory; a resource
built for another engine is catalogued but not offered and cannot be selected; a removed installation
resolves as `Missing` with the resource named; a second version installs side by side with the first
still resolving; and a tampered package leaves no staging or backup directory and does not disturb
the installation already present. `seam_procedural_package_tests` passes 11 of 11 against the
corrected identity semantics, and `seam_u3_standalone_tests` passes 4 of 4.

Not claimed. No real producer has signed a package and no human has listened to a rendered installed
singer. The review decision type from D4.2 is not yet wired into the authoring session, so the
journey does not record a decision through the application. No engine revision is pinned to a
retained build, so reproducing an old sound still requires a retained runnable build. No U-unit
acceptance or Beta GO state changes.

## A review decision is bound to the exact rendering it was made about
## A review decision is bound to the exact rendering it was made about

September 15, 2026 — D4.2 added. Signing a procedural singer proves who produced it. It says nothing
about whether anyone reviewed it, and until now nothing stopped an edited recipe from inheriting an
earlier approval. `ProceduralReviewBasis` now names every fact a decision depends on: the resource
identity, the canonical recipe digest, the installed content hash, the engine and its revision, the
render ABI, the compiler revision, the sample rate, the digests of the score and audio that were
reviewed, and the render settings. Its digest is taken over the canonical encoding, the same way the
manifest digest binds the canonical recipe rather than raw file bytes.

The invariant is one comparison. A decision resolves to a candidate only when the digest the decision
was recorded against equals the candidate's current digest. Change the recipe, the renderer revision,
the render ABI, the compiler, the settings, or the audio that was listened to, and the prior approval
becomes stale rather than inherited. When the decision carries the basis it reviewed, the stale report
names the fields that actually changed instead of only reporting that something did.

Three related rules prevent a manufactured approval. Evidence digests are computed from the files
rather than accepted from the caller, so a review cannot claim evidence it was not performed against.
An acceptance is refused on a candidate that carries no score and audio evidence, because accepting
is a claim that evidence was examined; a rejection without evidence remains recordable. And a decision
whose recorded digest does not match the basis it names, or whose carried basis contradicts its own
digest, is refused rather than stored.

Verified. `seam_procedural_review_tests` passes 10 of 10: a stable digest that survives a JSON round
trip and an invalid-basis refusal; a changed recipe, a changed compiler revision, a changed render ABI
and a changed engine revision each making a prior acceptance stale, with `recipeSha256` named as the
differing field; changed audio invalidating a review whose recipe and renderer are identical; a
rejection after an acceptance withdrawing it and a later acceptance restoring it; refusal of a
forged, malformed, anonymous or self-contradictory decision; refusal of an acceptance with no
evidence alongside a recordable rejection without it; a decision belonging to another candidate not
counting as evidence and not being reported as stale; freezing hashing the real files and refusing
missing evidence; and a wrong-family or malformed basis document refused as `Unsupported`.

Not claimed. No review has been performed on a real procedural singer: these cases exercise the
binding, not a reviewer's judgment. The decision store is not yet connected to the authoring session,
so a creator cannot yet record a decision through the application, and the connected acceptance
journey in D4.8 does not exist. Signature validity and musical approval remain different statuses. No
U-unit acceptance or Beta GO state changes.

## The application offers an installed singer by identity, and only one it can render

September 15, 2026 — D4.6 added. A creator no longer has to know where an installed procedural
singer lives on disk. `SelectInstalledProceduralSinger` scans the configured procedural roots,
offers every candidate this build can actually render, and records the chosen resource as a
`ProceduralRecipeReference` carrying the resource identity, the installed manifest's recipe path and
the selected style, so the track names the installed resource rather than a file the creator
happened to pick.

Two rules decide what is offered. A build that has not declared its renderable engine refuses to
list anything at all, rather than defaulting to permissive; that is a configuration error the caller
must fix, not a browse. Among the candidates that do resolve, an incompatible engine and an untrusted
install are both withheld from the chooser instead of being presented with a caveat a creator would
have to interpret. A singer declaring more than one style asks which style, because taking the first
would silently decide a musical property.

Two defects were found and repaired while landing this. The catalogue's `resourceRoot` came from
whatever spelling of the search root the caller passed, while the installer publishes a canonical
path, so the same installed singer compared unequal to itself and an installed selection would not
have matched the identity it recorded. The catalogue now reports a canonical resource root. Separately,
a zero engine revision in `ProceduralResolveOptions` now means "the caller knows the engine but not the
revision it will render with, so compare the engine only", instead of failing every candidate; an
empty engine still means the caller is browsing and checks nothing.

Verified. `seam_u3_standalone_tests` passes 4 of 4, including the new case: with no declared engine
the listing and the command both fail and the project is unchanged; a singer built for another engine
is present on disk but is not offered, and cancelling the chooser leaves the track untouched; and
choosing the offered singer records the installed id, version and content hash with the installed
recipe path, which one undo removes. `seam_procedural_package_tests` passes 11 of 11 unchanged.

Not claimed. The selected singer is not written into a review candidate, and the connected acceptance
journey in D4.8 does not exist yet, so no end-to-end authoring-to-installed-resource run has been
performed. No installed singer has been rendered or listened to. Editing an installed singer still
does not produce a copy, and no engine revision is pinned to a retained build. No U-unit acceptance or
Beta GO state changes.

## A procedural singer is now a package, not a loose JSON file

September 15, 2026 — D4.6 prerequisite fix. The manifest digest was compared against the packaged
recipe file bytes, but the identity a project stores and the renderer validates is the digest of the
recipe's *canonical* encoding. A package whose recipe decoded to the declared identity but re-encoded
differently would have been refused for the wrong reason, and one whose bytes merely looked identical
would have been accepted without proving identity. Admission now decodes the recipe, re-encodes it,
and compares that digest, and a case packs a recipe whose file bytes differ from the canonical form to
pin the contract.

September 15, 2026 — D4.7 added. `resolveProceduralSinger` now takes the engine id and revision the
calling build can render. A resource whose declared engine or revision does not match reports
`IncompatibleEngine` with both sides named, and that verdict is checked before trust so a playable
but unreviewed resource is not mislabelled as untrusted, and an unplayable but correctly signed one
is not silently accepted. A caller that is not going to render the resource can leave the engine
empty and still browse it.

September 15, 2026 — D4.5 added. `ProceduralCatalogue::scan` walks installed and development roots,
skips staging and backup directories, and recomputes each candidate's content hash from the installed
manifest and recipe instead of trusting the receipt. A receipt that is missing, of the wrong family,
or that disagrees with the bytes downgrades trust to untrusted-installed rather than being believed.
`resolveProceduralSinger` separates missing, version mismatch, missing hash, content mismatch,
untrusted and invalid reference from each other, so a surface can say which one happened. Relink is
the same declared identity resolved against another root and never rewrites the identity.

September 15, 2026 — D4.4 added on top of the package family. `installProceduralPackage` writes the
verified entries into a staging directory, re-checks the installed manifest against the signed one,
publishes a typed receipt carrying the resource family, id, version, content hash, recipe digest,
engine identity and signer, then renames into place with a backup-and-rollback path for replacement.
Installation requires an explicit trusted key; a caller who does not require trust is refused rather
than allowed to install unverified content. A duplicate version is refused without disturbing the
existing installation, and no staging or backup directory survives either outcome.

September 15, 2026 — D4.1/D4.3. The signed container that carried sample banks is now family-neutral:
the entry table, per-entry digests, single Ed25519 signature, path policy, size bounds and durable
publish live in one implementation, and the sample bank is one family over it. The refactor is
behaviour-preserving; the existing distribution cases passed unchanged before any procedural code
was added.

`ProceduralSingerManifest` is a typed second family with its format id, schema version, identity,
display name, language, styles, engine id and revision, declared recipe entry and digest, and
declared phone coverage. `packProceduralPackage` refuses to sign a package whose declared recipe is
not the recipe bytes, whose digest is malformed, whose recipe this build cannot decode, or whose
recipe engine disagrees with the manifest. Declared coverage and reviewed qualification stay
different fields; signing proves authenticity, not musical quality. This closes the
manifest/admission and pack/verify substeps of the plan's procedural distribution slice;
native selection and the connected acceptance journey remain open.

Verified. `seam_procedural_package_tests` passes 11 of 11: canonical recipe-digest binding; engine-compatibility resolution that separates an incompatible engine or revision from an untrusted signer and still browses without a compatibility request; catalogue discovery catalogue discovery that recomputes installed content hashes and downgrades a missing, wrong-family or stale receipt; exact-identity resolution that separates missing, version mismatch, missing hash, content mismatch and untrusted, with relink resolving the same identity against another root; a development root reported as a fixture and refused by a caller requiring trusted installs; transactional installation with a receipt that matches the published content hash; refusal of an untrusted signer and of a caller that does not require trust, both leaving nothing on disk; and the package cases themselves:  typed manifest round trip with bounded
identity, coverage, digest and engine fields; a signed package that verifies and returns exactly the
recipe it declares, with the packaged bytes matching the manifest digest; refusal of a package whose
recipe does not match its declared digest, leaving no artifact behind; mutual family refusal, so a
procedural manifest is not read as a bank and a voicebank manifest is not read as a procedural
singer; and a whole-package tamper that no longer verifies. The aggregate core suite passes 904
cases and the registered CTest run passes 166 of 166.

Not claimed. No native
selection, review-candidate binding or renderer-compatibility policy exists yet, so a creator still
selects a procedural recipe by file path. No package was rendered or listened to, and no engine
revision was pinned to a retained build. No U-unit acceptance or Beta GO state changes.

## The timbral channels now have a lane you can draw on

September 15, 2026 — D2. The six channels that share a region curve are edited through one drawn lane
in the automation band instead of six keyboard nudges. `ExpressionChannelDescriptor` carries each
channel's own unit and bound; `ExpressionLaneModel` captures a draft exactly as the dynamics lane does
and commits one gesture as exactly one undoable command.

The surface is shared; the meaning is not. Formant is semitones with a one-semitone step, gender is
bipolar, and breathiness, tension, airiness and growl are normalized shares with a tenth step. A
channel that the selected singer cannot render is still drawn from the stored project curve, with the
renderer's own refusal printed in the lane; edits are refused rather than silently dropped. The lane
is reachable from the Edit menu, and the same facts are exposed in the automation lane's accessible
value and description.

Verified. `seam_expression_lane_tests` passes 8 of 8: units and bounds per channel; one semitone, one
bipolar and one normalized channel driven through the same insert/move/delete/undo interaction;
out-of-range, nonfinite and beyond-region points rejected rather than clamped; cancellation and a
stale session revision refusing to apply; a bank refusing every one of the six channels by name while
the stored curve stays visible and clearable; save/reload round trip; and the drawn lane reporting
channel, unit, playhead value and refusal. The paint case renders the real scene and is retained by
request.

Review of that render found two defects that the assertions alone did not catch: the pitch empty-state
hint still painted inside the band the expression lane owns, and instruction text overdraw the curve.
The lane now takes the band from pitch when it is open and shows its gesture hint only while the
channel has no points, with the unit stated in the band's right margin. The full Release build and the
registered suite pass together here, and source closure passes once the new files are staged.

Not claimed. This is one shared surface for six existing channels, not a new synthesis capability.
Undo grouping beyond a single gesture, drawn interpolation modes and per-channel lane stacks are not
implemented. The lane was verified on the fixture project and the retained packet's own song is the
next input. No U-unit acceptance or Beta GO state changes.

## The revised plan now has a retained song to edit and compare

September 15, 2026 — D1 technical packet. The existing procedural pilot rendered
11 cases including a 16-second unfamiliar melody across three recipe variants (66 WAVs,
335 retained artifact files, no clipping). The packet lives outside disposable build
output at `/Users/lhs/Downloads/seam-listening-artifacts/2026-09-15-d1-02`; its
manifest and decision record are under `docs/implementation/listening/2026-09-15-d1-02/`.
It retains exact scores/recipes, original audio, measurements, executable and dependency
identities. The retained binary rerenders the melody with six identical WAV hashes.

Verified. All artifact hashes match. The pilot CLI suite passes after repairing its
unequal-duration pitch-analysis windows; reported window ticks/frames now follow the
actual score rather than a fixed note-index stride. The packet is generated with the
bounded `tools/singing_quality/listening_packet.py` orchestrator through the existing
production export path, not a new synthesis implementation.

Not claimed. No human listening or creator judgment exists for the packet yet; no
acoustic repair route is chosen. Candidate WAVs are not installed-bank renders. Retained
binary dependencies do not establish a portable installed application. D1's technical
material is available; its musical outcome remains pending. Continue D2 on this song.

## Growl reaches the source, and a neutral nudge preserves the rest of the phrase

September 15, 2026 — revised-plan D0. Growl now has a normalized region curve,
schema-17 persistence (older schemas load without the curve), compiler revision 14,
snapshot windowing, typed undoable edits and native menu dispatch. Sample-bank and
neural snapshots refuse non-neutral growl requests by name; the source-filter path
modulates the periodic excitation with a half-rate phase accumulator. Reattack and
reset restart that accumulator. The modulation depth bounds periodic gain to 0.5–1.0.

Two earlier DSP approaches were rejected using the sustained-vowel probe. Adding a
55 Hz tone below a 110 Hz fundamental barely changed the measured subharmonic peak
(0.007602 to 0.007674) after tract filtering. Computing half-rate modulation directly
from the wrapping fundamental phase instead repeated at the fundamental rate and
left inter-harmonic energy unchanged (0.4936 to 0.4936). The retained half-rate
accumulator produced inter-harmonic energy 0.493592 to 1.910222 and a 55 Hz peak
0.007602 to 0.664456 in that probe; the tests assert bounded monotone changes and
period doubling rather than treating those recorded measurements as listening proof.

Checkpoint review found a separate editing defect: nudging one point to zero cleared
the entire region curve. The growl nudge now preserves an explicit neutral point when
other points remain non-neutral, collapsing only a wholly neutral curve. A two-point
regression proves the later value and intervening ramp survive and undo/redo restores
the complete prior/next curve. The test also reacquires region state after reset,
avoiding a pointer retained across project replacement.

Verified. The rebuilt growl suite passes 8/8, including schema round trip, legacy
loading, invalid values, carrier refusal, editing and the acoustic bound oracle.
The earlier retained full CTest log reports 163/164 passed before this review repair,
with only source closure failing for three untracked growl files. After the review
repair, all six rebuilt expression targets pass (7.50 seconds), and explicit staging
resolves source closure: the standalone audit and CTest closure target both pass
(0.26 seconds). This is a fresh affected run plus a resolved closure check, not a
claim that the historical full run had no failures.

Not claimed. These measurements cover the sustained-pose fixture, not listener
judgment, female identity or full articulated automation qualification. Shared drawn
expression editing and style blend remain unfinished. No U-unit or Beta GO acceptance
is changed. Execution now follows the revised plan: retain direct-procedural listening
material before adding another product capability.

## Gender is a coupling, and a nudge that lands on neutral now lands on neutral

Gender was the one channel in the section-7 list that could not be built as another single-domain control,
because the list already has both of its halves: the formant channel moves the tract's resonances alone and
the tension channel tilts the source alone. Gender is defined as the channel that moves both, by a fixed
ratio, so that the two halves of one voice stay consistent with each other. `GenderAutomation` joins the
region's curves, the project schema moves to 16 with the readers for 1 through 15 unchanged, and the unit
is bipolar with zero exactly neutral.

The test for a coupling cannot be a pair of independent spectral measurements, because a tract shift
moves a source-shaped measurement and a source tilt moves a tract-shaped one. It is instead exact: a
gender curve of one must render bit-identically to the render where the formant channel carries
`kGenderFormantSemitones` and the tension channel carries `kGenderTiltDbPerOctave / kTensionTiltDbPerOctave`.
The two neighbouring channels are given gender's own amounts, and the resulting audio has to be identical
sample for sample. That claim cannot be satisfied by a channel that moves one half twice, and it is
checked as equality rather than as a threshold. The same case then proves neither half alone is gender,
that a neutral curve is the source that had no curve at all, that the third formant rises with a positive
value and falls with a negative one, that the source-only render leaves the third formant where it was,
and that the fundamental does not move.

That oracle found a defect in the channels beside it, which is recorded here because it was never gender's
alone. A nudge is a tenth of a channel, and ten tenths are not exactly one in binary floating point:
starting from the channel's maximum, ten downward steps produced -1.5e-8 rather than zero. The channel
treated that as a value rather than as neutral, so it stored a point, reported a non-neutral value at the
playhead, and made an edit out of an edit that changed nothing -- exactly the failure the no-op rule was
written to prevent, hiding one step below the resolution of the check that was supposed to catch it. Every
share-based nudge now snaps a target within a millionth of neutral to neutral before deciding whether it is
an edit. Seven orders of magnitude below one step, the tolerance can only catch arithmetic residue and can
never round away a value someone chose, and each of the four affected channels now has a case that walks
ten steps up and ten back down and requires the curve to be cleared.

Verified. `seam_gender_expression_tests` passes 7 of 7. The channel's own cases cover the bipolar bounds
(both directions are ordinary values, only the bound is a mistake), ordering and interpolation, a
save/reload round trip at schema 16, a schema-15 document loading with an empty curve, and an out-of-range
amount inside a schema-16 document being a parse error rather than a clamped value. The capability cases
prove the source-filter carrier advertises Gender while a bank does not, that validating a bank request
fails with the control named, that a bank snapshot carrying a non-neutral curve is refused with gender
named, and that the same bank renders the same region when the curve asks for nothing. The controller
cases run a real editor session in both directions, bound the value at both ends, remove the point when a
nudge lands on neutral, undo every step in order, and prove that a sample-bank track refuses the nudge
with Unsupported while leaving the stored -0.5 curve in place and still clearable. `seam_breathiness_expression_tests`,
`seam_tension_expression_tests`, `seam_airiness_expression_tests` and `seam_formant_expression_tests`
each pass 7 of 7 with their new neutral-cancellation case, and the registered CTest run is reported in the
commit that carries this entry.

Not claimed. Growl and style blend still have no algorithm behind their names, so M3.P2 is not complete,
and style blend additionally needs two compatible aligned styles that this repository does not have.
Gender here is one defined, measured mapping of tract and source; it is not a claim about perceived
identity, about a physiological model of vocal-fold size, or about any particular listener's judgement of
masculine or feminine voice, and the product's own identity rubric is M6 work. The oracle measured the
sustained-pose path; the articulated stream shares both owners but was not measured. There is still no
drawn lane, no curve editor and no inspector applicability row for any of these channels, which is what
M4.P1 item 6 owes. Nothing was listened to, and no unit acceptance changes.

## Airiness is a band the source owns, and it is measurably not breathiness

Airiness and breathiness sit next to each other in the product's control list and are easy to conflate in
the code: both change the aperiodic content of the excitation, and a channel that duplicated its
neighbour would be a control that lies about what the product can do.
`AirinessAutomation` joins the region's curves beside pitch, dynamics, formant, breathiness and
tension, and the project schema moves to 15 with the readers for 1 through 14 unchanged.

The difference between the two channels is bandwidth rather than amount. Breathiness is a balance: it
moves share from the periodic part of the excitation into the aspiration the recipe already declares,
which is a dark, low-passed noise, and it does so in the band the voice is already using. Airiness adds
the part of that same stream the aspiration filter rejected -- one subtraction, `white - filtered`,
which is the high-frequency complement of the filter the source already runs. Nothing new is generated
and nothing is stored: the band exists because the source owns its noise, so the carriers that do not own
it refuse the curve exactly as they refuse the channels beside it.

The two channels were built to be separable and then measured to prove it. At its maximum, airiness takes
the ratio of the 7-16 kHz band to the 1-4 kHz band from 0.0104 to 0.0296, about 2.9 times, while the
level of the render moves by 0.09 percent and the measured periodicity of the source stays at 0.976 from
0.978. The maximally breathy render of the same vowel, measured in the same test and on the same window,
falls to 0.695. An airy source is still a clearly voiced one; a breathy source is not. Both leave the
fundamental where the score put it.

That separation is what the test asserts rather than what its comments say: the airiness case renders the
plain vowel, the neutral curve, the airiest curve and the breathiest curve, and requires the airy render
to keep a periodicity above 0.9 and to exceed the breathy render by more than a tenth. A future change
that made airiness a second breathiness -- or that made the two channels interchangeable -- would fail
here instead of shipping.

Verified. `seam_airiness_expression_tests` passes 7 of 7. The channel's own cases cover bounds, ordering
and interpolation, the rejection of a share above one, a negative share, a nonfinite share and
out-of-order points, a save/reload round trip at schema 15, a schema-14 document loading with an empty
curve, and an out-of-range amount inside a schema-15 document being a parse error rather than a clamped
value. The capability cases prove the source-filter carrier advertises Airiness while a bank does not,
that validating a bank request fails with the control named, that a bank snapshot carrying a non-neutral
curve is refused with airiness named, and that the same bank renders the same region when the curve asks
for nothing. The controller cases run a real editor session: four steps write one point at the playhead
tick with a share of 0.4, a second nudge replaces that point instead of accumulating points, a hundred
steps stop at the channel's bound, each accepted nudge undoes back to the previous curve in order, and
resetting is an edit that undoes to the curve it replaced. On a sample-bank track the same nudge is
refused with Unsupported, the message names airiness and the source-filter remedy, the stored 0.6 curve is
still there and still reports 0.6 at the playhead, and clearing it -- which is always allowed -- removes
it. The acoustic oracle is described above: a neutral curve is bit-identical to no curve, the high-band
ratio rises by more than half again, the source stays above 0.9 periodicity and above the breathy render
by more than a tenth, the recovered fundamental stays within 2 Hz, and the level rise stays under half
again so that the channel cannot stand in for a gain. The whole tree builds and the registered CTest run
is reported in the commit that carries this entry.

Not claimed. Gender, growl and style blend still have no algorithm behind their names, so M3.P2 is not
complete. The band is a bounded subtraction of the source's own noise and says nothing about any measured
breathy or aspirated phonation. The oracle measured the sustained-pose path; the articulated stream
shares the same excitation owner but was not measured. There is still no drawn lane, no curve editor and
no inspector applicability row for any of these channels, which is what M4.P1 item 6 owes. Nothing was
listened to, and no unit acceptance changes.

## Tension is a spectrum a creator can set, and a carrier with no harmonic source refuses it

Tension is the channel that is easiest to fake with a gain and the least honest when it is. A loud phrase
is not a pressed one, and a control that secretly moves the level would make every later comparison --
including a listener's -- measure the wrong thing.

`TensionAutomation` joins the region's curves beside pitch, dynamics, formant and breathiness, and the
project schema moves to 14 with the readers for 1 through 13 unchanged. The persisted unit is the
channel's own: a normalized tilt applied to the harmonic source's own spectral roll-off, where zero is
the recipe's source and one is the most pressed setting the channel admits.

The consumer is again the one place that owns the excitation. `PhonationSource` builds its harmonic
table from the recipe, so tension is applied by rebuilding that table with the tilt in force -- one table
per processing block, exactly as the tract's formant shift is one shift per block, and a block whose
tension equals the applied one costs nothing. A source with no tension curve uses the recipe's own table
rather than a copy that has been through a neutral tilt, and the tilt for a tension of exactly zero is a
factor of exactly one, so the channel is bit-identical to its own absence.

The maximum is a measured bound, and the first measurement was a rejection. This channel was built with
nine decibels per octave at maximum, which took the ratio of harmonic energy above two kilohertz to
harmonic energy around the first formant to about forty-six times the recipe's own and left the
fundamental ten times weaker: that is a different instrument, not a more pressed voice. Four decibels per
octave moves the same ratio by about four times while the fundamental stays within a factor of about two
and the recovered fundamental is unchanged, so that is where the channel's maximum sits. The lesson is
kept in the constant's documentation rather than in a comment about taste.

The measurement itself had to change to say that. A spectral centroid was tried first and reported almost
no movement -- 1865.2 Hz against 1865.2 Hz -- while the renders were plainly different, because one
dominant harmonic pins an average that a moving balance underneath it cannot shift. The oracle now sums
harmonic energy in two bands by searching a narrow window around each expected harmonic of the score's
own fundamental, which is stable against the grid that a fixed-frequency sweep depends on.

The capability decision is the same one its neighbours make. A concatenative bank has no harmonic source
to tilt and an admitted model is not asked to, so a curve that asks for anything is refused with the
carrier named and with the change that would allow it, while a curve that asks for nothing is not a
request and renders as before. Raise Tension (Command-Option-]), Lower Tension (Command-Option-[) and
Reset Tension Curve are reachable from the native menu, one step being a tenth of the channel; every
accepted nudge is an ordinary undoable edit, and a downward nudge at the floor is not an edit at all and
so leaves no undo entry behind.

Verified. `seam_tension_expression_tests` passes 7 of 7. The channel's own cases cover bounds, ordering
and interpolation, the rejection of a share above one, a negative share, a nonfinite share and
out-of-order points, a save/reload round trip at schema 14, a schema-13 document loading with an empty
curve, and an out-of-range amount inside a schema-14 document being a parse error rather than a clamped
value. The capability cases prove the source-filter carrier advertises Tension while a bank does not,
that validating a bank request fails with the control named, that a bank snapshot carrying a non-neutral
curve is refused with tension named, and that the same bank renders the same region when the curve asks
for nothing. The controller cases run a real editor session: three steps write one point at the playhead
tick with a share of 0.3, a second nudge replaces that point, a hundred steps stop at the channel's
bound, every accepted nudge undoes back to the previous curve in order, resetting is an edit that undoes
to the curve it replaced, and a nudge downward at the floor leaves the document and the undo history
untouched. On a sample-bank track the same nudge is refused with Unsupported, the message names tension
and the source-filter remedy, the stored half curve is still there and still reports half at the
playhead, and clearing it -- which is always allowed -- removes it. The acoustic oracle renders the same
vowel four times and compares the renders after matching their levels, so a level change cannot pass as
effort: a neutral curve is bit-identical to no curve, the measured tilt of the level-matched renders rises
from 0.0019 through 0.0027 to 0.0074 as the curve rises, the maximum is more than twice the recipe's own
balance, the recovered fundamental of the tense render is within 2 Hz of the plain one, and the source
stays periodic with a coherence above 0.8. The whole tree builds and the registered CTest run is reported
in the commit that carries this entry.

Not claimed. This is the second of the three source-side channels: airiness, gender, growl and style
blend still have no algorithm behind their names, so M3.P2 is not complete. The tilt is a source-spectrum
change and nothing else -- no formant bandwidth change, no subglottal or laryngeal model, and no claim
that the result matches any measured pressed-phonatory behaviour. The oracle measured the sustained-pose
path; the articulated stream shares the same excitation owner but was not measured. There is still no
drawn lane, no curve editor and no inspector applicability row for any of these channels, which is what
M4.P1 item 6 owes. Nothing was listened to, and no unit acceptance changes.

## Breathiness is a balance a creator can set, and a carrier that has no excitation refuses it

The breathiness channel existed as a name in the capability table and as a unit in the persisted take
lanes, and as nothing a creator could touch: no curve, no command, no consumer. A control the product
advertises and the renderer never reads is a claim the audio contradicts.

`BreathinessAutomation` joins the region's curves beside pitch, dynamics and formant, and the project
schema moves to 13 with the readers for 1 through 12 unchanged: a document written before the channel
existed has an empty curve, and an empty curve is exactly the neutral setting. The persisted unit is the
channel's own, a normalized balance between the periodic and the aperiodic part of the excitation,
because a decibel or microphone-level unit would mean something different for every recipe it was
applied to.

The excitation is where the balance belongs, and the procedural engine has exactly one owner of it:
`PhonationSource`, which both the sustained-pose renderer and the articulated stream create. It applies
one balance per control block, moving a share of the periodic weight into aperiodic weight, so a breathy
phrase is a different production of the same note rather than a louder one, and the arithmetic for a
frame whose breathiness is exactly zero is the arithmetic of a frame that never had the channel at all.

The maximum conversion is measured rather than chosen. The aperiodic generator is much louder per unit of
weight than the harmonic stack, so a share that reads as modest is already a large change in energy: at
one fifth of the periodic weight the rendered phrase stops carrying its own fundamental and rises by
about half its level. Fifteen hundredths of the periodic weight is the last setting that stays clearly
voiced -- measured periodicity falls from about 0.98 to about 0.70, the recovered fundamental stays
within a few hundredths of a hertz, and the level moves by about two percent -- so that is where the
channel's maximum sits.

The capability decision is the formant channel's decision one layer down. A concatenative bank has no
excitation to rebalance and an admitted model is not asked to, so a curve that asks for anything is
refused with the carrier named and with the change that would allow it, while a curve that asks for
nothing is not a request and renders as before. Raise Breathiness (Command-Shift-]), Lower Breathiness
(Command-Shift-[) and Reset Breathiness Curve are reachable from the native menu; one step is a tenth of
the channel, a nudge at a tick that already carries the value in force is not an edit, and every
accepted nudge is an ordinary undoable edit.

Verified. `seam_breathiness_expression_tests` passes 7 of 7. The channel's own cases cover bounds,
ordering and interpolation, the rejection of a share above one, a negative share, a nonfinite share and
out-of-order points, a save/reload round trip at schema 13, a schema-12 document loading with an empty
curve, and an out-of-range amount inside a schema-13 document being a parse error rather than a clamped
value. The capability cases prove the source-filter carrier advertises Breathiness while a bank does
not, that validating a bank request fails with the control named, that a bank snapshot carrying a
non-neutral curve is refused with breathiness named, and that the same bank renders the same region when
the curve asks for nothing. The controller cases run a real editor session: two steps write one point at
the playhead tick with a share of 0.2, a second nudge at that tick replaces the point instead of
accumulating points, a hundred steps stop at the channel's bound, every one of those undoes back to the
previous curve in order, and resetting is an edit that undoes to the curve it replaced. On a sample-bank
track the same nudge is refused with Unsupported, the message names breathiness and the source-filter
remedy, the stored half curve is still there and still reports half at the playhead, and clearing it --
which is always allowed -- removes it. The acoustic oracle renders the same vowel four times: a neutral
curve is bit-identical to no curve, periodicity falls monotonically from the plain render (about 0.98)
through the half setting to the breathiest one (about 0.70), the drop exceeds 0.15, the breathiest render
is still above 0.5 and therefore still recovered as voiced, its fundamental stays within 2 Hz of the
plain render, and its level stays within the same order as the render it came from. The whole tree
builds and the registered CTest run is reported in the commit that carries this entry.

Not claimed. This is one channel of the mandatory expression list: tension, airiness, gender, growl and
style blend still have no algorithm behind their names, so M3.P2 is not complete. The acoustic oracle
measured the sustained-pose path; the articulated stream shares the same excitation owner, which is why
one change covers both, but it was not measured. There is no drawn lane or curve editor for this channel
and no inspector row showing applicability beside its value, which is what M4.P1 item 6 still owes. The
compiler revision advance means previously compiled performance is not reused, and a breathy phrase
renders with the old compiler revision nowhere. Nothing was listened to, the oracle measures the render
rather than a listener's judgement, and no unit acceptance changes.

## The formant channel is an edit a creator can make, and a carrier can refuse

The channel existed, but nothing could author it: no command carried it, no menu item reached it, and
the playhead an edit lands on was not known to the editor at all. A capability with no editing surface
is a fact about the code, not about the product.

`RegionFormantEdit` joins the composite performance edit, so a formant curve is validated, captured
before the change, applied, reverted and reported as phrase audio exactly as a dynamics curve is -- one
channel of intent per edit, and every one of them undoable. The editor now tracks the transport tick
beside the playhead pixel, because a nudge lands at a musical position rather than at a pixel, and
`nudgeFormantShift` and `resetFormantCurve` write the curve through that command.

The capability decision decides the edit as well as the render. A nudge asks the carrier whether it can
move its own resonances; a sample bank cannot, so the edit is refused with the reason and with the
change that would allow it -- select a source-filter singer -- and whatever curve the document already
contains is left exactly as it was, because an edit that cannot be applied must not destroy intent that
was stored. Clearing is always allowed: it is the remedy rather than the request. The native menu now
carries Raise Formant Shift, Lower Formant Shift and Reset Formant Curve, so the channel is reachable
without a project file.

Verified. `seam_formant_expression_tests` passes 7 of 7. The two new cases run a real editor session
and controller: a nudge on a source-filter track writes one point at the playhead tick with the
requested shift, a second nudge at the same tick replaces that point rather than accumulating points, a
hundred-step nudge stops at the channel's own bound, and each of those is undone back to the previous
curve in order; resetting a curve is an edit that undoes to the curve it replaced. On a sample-bank
track the same nudge is refused with Unsupported, the message names the channel and the source-filter
remedy, the stored five-semitone curve is still there afterwards and still reports five semitones at
the playhead, and clearing it -- which is allowed -- removes it. The whole tree builds and the
registered CTest run is reported in the commit that carries this entry.

Not claimed. The nudge is a menu item and a key equivalent, not a drawn lane: there is no curve editor
for this channel, no per-point drag, and no inspector row showing the channel's applicability beside
its value, which is what M4.P1 item 6 still owes. The plug-in host has no playhead, so it cannot nudge
at all. Nothing was listened to and no unit acceptance changes.

## The formant channel moves the tract, and a bank that has no tract says so

Seven of the thirteen expression controls had no storage at all: they existed as capability names and
nothing else. The formant channel is the first of them to become real, because it is the one a
concatenative bank genuinely cannot have and the source-filter engine genuinely can: the engine owns
its own resonances, so moving them is a tract change rather than a claim.

The persisted unit is a semitone shift of the vocal tract's resonance frequencies, bounded at two
octaves, saved as its own region curve and interpolated between points exactly as the dynamics curve
is. The project schema is twelve; a document written before the channel existed still loads with an
empty curve rather than being refused, and a schema-twelve curve outside the bound is a parse error
rather than a clamped value. The compiled performance now carries the shift per frame, so a manual
formant edit is authoritative over the generated curve the same way a manual dynamics edit is.

The engine applies it by re-designing the tract every control block from the resonance frequencies and
bandwidths each band retained, carrying the filter state across, and it applies the same shift to any
pose the tract is moving toward -- otherwise a coarticulation window would slide back to the unshifted
tract. The shift is a delta from the one the tract already holds, so repeated calls cannot compound the
pose away from the one that was authored, and a shift that would put a resonance at or past Nyquist is
refused by cause instead of being clamped to a different vowel. Both procedural paths, the articulated
one and the sustained one, apply it.

Capabilities now answer for the carrier rather than only for a hint. The source-filter carrier
advertises the formant channel; a sample bank does not, because it has no resonances of its own. A
project whose curve asks for a shift is refused by name by the sample-bank and neural snapshot
factories instead of being dropped in silence, and a curve that is entirely neutral is not a request.
The interchange loss reports now name the formant curve alongside the dynamics curve, so an USTX or SMF
conversion cannot lose it quietly either.

Verified. `seam_formant_expression_tests` passes 5 of 5: the curve refuses out-of-range, non-finite
and unordered points, interpolates between them and erases idempotently; a saved curve round-trips
through the project codec while a schema-eleven document without the field still loads as an empty
curve and a schema-twelve curve at thirty semitones is refused as a parse error; the source-filter
carrier advertises the channel while a sample bank does not, and a request requiring it fails by name
against the bank; a bank asked to render a region whose curve asks for seven semitones is refused with
Unsupported while a neutral curve is not; and the acoustic oracle holds -- rendering the same vowel
with a seven-semitone curve moves the frozen spectral centroid between 300 and 4000 Hz up by more than
twenty percent while the autocorrelation fundamental stays within two hertz, and a curve of zero
semitones renders bit-identical samples to having no curve at all. The whole tree builds and the
registered CTest run is reported in the commit that carries this entry.

Not claimed. Nothing in the UI can author this curve yet: there is no command and no inspector row, so a
creator cannot set it without a project file, and the lane work belongs to M4.P1. The shift is applied
at control-block rate rather than per frame, which is a declared smoothing choice and not a measured
one. The neural carrier refuses the curve because admitted bundle metadata still declares no supported
conditioning inputs; a model that can be conditioned will need that declaration before it can accept
one. Nothing was listened to: the oracle measures a fixture render, and no claim is made about how the
shifted vowel sounds. No unit acceptance changes.

## A character package declares its performance, or honestly has none

The dock could draw a mouth, but a character package could not supply one. Its manifest was schema one:
six operating-state assets and nothing else, so the mouth was always the dock's own glyph and there was
no way for reviewed artwork to reach the screen. The converse was also unguarded -- nothing in the
package format said whether a package was a status-only character or a performance turnaround, and
nothing carried the development-only fact that the Phase 13B asset tooling already writes.

Schema two adds declared performance assets. A performance package declares every mouth shape it can be
asked for, because a partially authored turnaround blended with the dock's own fallback drawing is
worse than a status-only character that honestly has no face: a missing or unsafe shape is refused by
name. Every declared asset is checked the way state assets already were -- regular file, inside the
package root, no escape -- and the development flag now travels in the package's own bytes rather than
in its directory name, so a rename cannot promote development artwork. Schema one keeps its exact
meaning: it is status-only, and a schema-one manifest that carries a mouth map or a development flag is
refused rather than silently ignoring the claim.

The presentation loads a declared turnaround whole and exposes it per shape; the standalone host asks
it for the shape the published phrase is currently drawing. A status-only package leaves that lookup
empty and the dock draws its own glyph, which is what keeps the shipped development character working
exactly as before.

Verified. `seam_character_package_performance_tests` passes 5 of 5: a status-only package loads,
declares no performance, resolves no mouth and presents none; a complete performance package loads,
resolves all six shapes to existing files and exposes them; a partial turnaround, a schema-one package
that carries a mouth map, a schema-one package that carries a development flag and an unknown schema
are each refused with the right code; a mouth path that escapes the root is refused as an invariant
violation and a declared-but-absent one as an I/O error; and a development package copied into a
directory named for production still reports itself development-only when loaded.
`seam_character_performance_dock_tests` passes 5 of 5, including the new painting case: a declared
mouth asset changes the dock's pixels, and with reduced motion the artwork and the fallback glyph are
both left undrawn -- identical pixels whether an asset exists or not -- while the label and the level
stay. The tracked character-01 package is schema one and still loads as status-only. The whole tree
builds and the registered CTest run is reported in the commit that carries this entry.

Not claimed. The assets in these tests are flat colours written by the test, not a reviewed turnaround;
no production character artwork exists, and the shipped character remains a development one. Nothing
was looked at by a person, no visual QA was captured from a running window, the plug-in host still
draws operational state only, and no unit acceptance changes.

## The dock says what it is singing, and whether that phrase is still current

The dock drew a mouth and a level, and it was silent about two things a listener can hear and a reader
cannot see. The first is that a phrase can be old: an edit or a failed render leaves the audible
publication exactly where it was, which is correct -- that audio is still what plays -- but a dock that
draws it with no qualification claims the project and the phrase agree. The second is that the mouth
was the only place any of this existed, so a reader that cannot see the dock learned nothing about the
performance at all.

`CharacterPerformanceView` now carries the render's own staleness, the standalone host fills it from
the coordinator's `audibleAudioStale`, and the dock paints the mouth line in the secondary colour with
a STALE marker and draws the measured level in the secondary accent instead of the primary one. The
phrase is still drawn, because it is still what a listener hears; what changes is that the dock no
longer presents it as current.

The dock's accessible value now carries the same facts. It names the character's state, whether the
phrase is singing or closed, the mouth the dock is drawing, the level as a percentage, and, when it
applies, that the project changed after this render. A dock with no phrase publishes none of those
facts rather than an empty performance.

Verified. `seam_character_performance_dock_tests` passes 4 of 4. The new case reads the built
semantic tree: the dock node's value names singing, the open mouth and a level of 80 percent while the
phrase is current, gains the sentence about the project changing after this render once the phrase is
stale, and loses every performance fact when there is no phrase. Painting the stale view differs from
painting the current one. In the session case, a render for a new revision with an unusable source
fails, the audible phrase stays at revision two -- the dock keeps drawing what is playing -- and
`characterPerformanceStale` reports that the project has moved on. The whole tree builds and the
registered CTest run is reported in the commit that carries this entry.

Not claimed. The plug-in host still receives operational state only: it has no host position to map
onto a phrase, so a CLAP, VST3 or AUv2 dock cannot yet say which frame is sounding, and that is host
timing work rather than a presentation one. Which of several simultaneous singers owns the dock is
still the active region's decision. The stale marker is verified on a painted surface, not in a
captured window, and no unit acceptance changes.

## The dock gives up its artwork before it takes the piano roll's room

The character dock had one width and no floor. When the window narrowed, the frame layout kept the
timeline's declared minimum and pushed the dock into whatever was left, so the portrait was scaled into
a strip and the metadata ran past the dock's own edge. That is the failure the dock exists to avoid: a
cropped face does not identify a singer and a clipped name does not name one, and both were being drawn
in the space the piano roll had just lost.

`resolveCharacterDockPresentation` now fixes the collapse order in one place: a dock with room for its
artwork is Full, a dock narrower than the portrait's minimum width is Compact, and a dock narrower than
what the identity, state and performance strip need is Hidden. A width that is not finite hides the dock
instead of drawing a fragment. The painter follows it -- Full draws the portrait, Compact replaces the
portrait with the dock's own background and keeps the metadata and the strip, Hidden paints nothing at
all -- and every metadata line is drawn inside the dock's own width, so a long name is ellipsized
instead of overflowing whether or not a text engine is available. The performance glyph is drawn only
where there is room beside the level bar and only when the host has not asked for reduced motion.

Verified. `seam_character_dock_layout_tests` passes 2 of 2. The policy holds at every boundary: the
portrait minimum and the compact minimum are inclusive, one point below each falls to the next state
down, zero and a NaN width are Hidden. Painting the same dock at 560, 380 and 330 logical pixels with a
declared 180-pixel timeline minimum produces three different surfaces, and one sample point inside the
artwork's own rectangle reads as the portrait colour, then the dock background, then the window
background, which is what "artwork first, then the dock" means on pixels. The frame layout keeps the
timeline at or above its declared minimum in all three cases, and the compact dock still inks its
metadata, so the identity and the performance strip survive the artwork's collapse. The whole tree
builds and the registered CTest run is reported in the commit that carries this entry.

Not claimed. The artwork in this suite is a flat colour, not a reviewed character asset, and no visual
redesign is claimed: this is a collapse policy and one dock measured on a painted surface, not a
reviewed layout or a captured screenshot of a running window. Which of several simultaneous singers
owns the dock is still the active region's decision, the plug-in host still draws operational state
only, and no unit acceptance changes.

## The standalone dock draws the published phrase, and a render is what binds it

A performance read model existed and a dock policy existed, but nothing in the product produced one.
The render still published no singer identity a presentation could bind to, the scene had no field for
a performance, and no host asked for a frame, so a character could describe a phrase it could never
show.

The render now publishes the identity of what it rendered alongside the partition. `RenderedPerformanceIdentity`
carries the resource id, version and content digest, the style, the render revision and the sample rate,
with `pronunciationIdentity` a framed digest over the ordered per-phrase pronunciation digests the
region published, so two different phrase splits cannot hash to one identity. It is filled from the
prepared snapshot for procedural and neural renders and from the resolved voicebank plus the track's own
style selection for sample renders, and an incomplete identity is not published at all rather than
published with a placeholder.

`AuthoringSession` evaluates that published render once per request and revision, on the calling
thread, so the render callback never publishes presentation state from a worker. It then builds the
snapshot through the published-render adapter and answers `characterPerformanceFrameAt(tick)` by
mapping the transport through the project's own tempo map at the snapshot's sample rate. The snapshot
now carries that sample rate, because a playhead cannot be mapped onto a frame coordinate without it.

The dock is drawn from that. `EditorSceneState` gained a bounded performance view, the controller
applies the host's reduced-motion preference to it (the setting belongs to the presentation, not to the
phrase), and the dock paints a mouth label, a measured level bar and a mouth glyph. Reduced motion drops
the glyph and keeps the label and the level, and a playhead outside the phrase says so in the dock
instead of drawing a still mouth. The standalone host binds the dock once per published render and
passes the frame for the current transport position every paint.

Verified. `seam_character_performance_dock_tests` passes 3 of 3: the controller carries a performance
into the scene and applies reduced motion from the host callback while leaving it false when the host
says nothing; painting a dock with a phrase differs from painting it without one, painting it with
reduced motion differs from both, and painting it again after the phrase is cleared restores the
original pixels exactly; and in the standalone session a completed render binds the dock, whose
identity is the procedural recipe's own id and content digest at style `neutral` and revision one,
whose cues are non-empty and whose snapshot validates, whose playhead at the phrase start performs with
nonzero energy and whose playhead four phrases later is closed and not performing, and whose second
render rebinds at revision two with the generation advanced exactly once. Evaluating the same published
render again is not a new phrase and does not advance the generation.
`seam_render_performance_cues_tests` passes 5 of 5, including the new identity assertions and the
framed-digest case, and `seam_character_performance_tests` passes 18 of 18. The whole tree builds and
the registered CTest run is reported in the commit that carries this entry.

Not claimed. The plug-in host still receives operational state only, so CLAP, VST3 and AUv2 do not yet
draw a phrase; there is no captured screenshot or visual QA of a running window, only painted pixel
comparisons. A singer switch that produces no new render leaves the previous binding in place, and
overlapping vocal tracks are not disambiguated: the dock follows the region the render published, which
is the active region rather than a policy for which of several simultaneous singers owns the character.
Expression is still caller-supplied transport rather than analysis, the mouths remain the dock's own
drawing vocabulary rather than a phonetic claim, nothing was listened to, no artwork was reviewed, and
no unit acceptance changes.

## A rendered phrase publishes the phone partition a presentation may draw

The character layer could already describe a performance, but nothing produced one. A render result
published the mix and the unit plan, not the ordered phone timeline, so a presentation had exactly
two bad options: re-derive timing that the renderer never used for the audio, or invent a mouth. The
same gap made the dock's second question unanswerable -- which singer a performance belongs to, and
what a presentation does when the answer changes mid-playback.

`rendering::collectPhrasePerformanceCues` now projects the compiled plan the render actually sang
from into a partition a single mouth can follow, in absolute project frames. It is a projection
rather than a copy because the plan declares more than one mouth can show: a consonant may declare an
onset inside the previous vowel's recorded span, and a phone with no declared onset falls back to its
own nucleus. The rule is therefore explicit -- each phone starts at the later of its declared onset
and the onset already in use, ends where the next phone starts, and the last phone ends where its own
recorded span ends; a declaration that cannot own a single frame is not drawn. `RegionRenderResult`
and `ProjectRenderResult` now carry those spans for the active track and region, collected from the
prepared snapshot itself before the PCM cache decides whether the phrase is rendered or reused, so a
cache hit publishes the same presentation data as a fresh render. The kinds are the dock's own
vocabulary: the product's `cl` and `pau` symbols and role, with vowels and nasals classified by the
phonemizer's own predicates, and everything else a consonant.

The dock now has an explicit singer policy. `CharacterPresentation::followSinger` records which
resource, style and render revision it is following and closes the mouth whenever that identity
changes, because the phrase that was showing belonged to the previous singer. A snapshot that does
not belong to the followed singer is refused with a conflict instead of being drawn, and the first
snapshot adopts its own identity so a single-singer host does not have to bind in two steps.
`native_ui::buildPublishedCharacterPerformance` closes the loop from a published render: it mixes
the published interleaved mix down to mono over exactly the span the cues cover, maps the kinds, and
refuses an empty partition, an unsupported channel count, a mix shorter than the phrase it claims, a
span longer than one presentation may cover and an unordered cue, by cause.

Verified. `seam_render_performance_cues_tests` passes 4 of 4: a real two-note procedural region
rendered through `ProductionProjectRenderer` publishes an ordered, non-overlapping partition whose
frames are absolute (the second note's stop lands at 24000 and its vowel at 26880, not at the start of
the mix), containing at least one vowel and one consonant and ending inside the music; a request that
names another active region publishes none; the product's `cl`, `pau`, vowel, nasal and consonant
symbols classify as declared; and an anchor with no resolved phone is refused with InvariantViolation
while a zero and a one-span bound are refused with InvalidArgument. That suite also records the
finding that forced the projection: the compiled anchors for this phrase are a vowel 0..24000, a stop
with an inferred onset at 24000 and an end of 48000, and a vowel with a nucleus of 26880 and the same
recorded end, so the anchors' own ends are not a partition. `seam_character_performance_tests` passes
18 of 18, including the new singer policy (following the same singer keeps the phrase, following
another style or another render revision closes the mouth, the previous singer's phrase is refused
with a conflict, an incomplete identity is refused with InvalidArgument, and the followed singer is
unchanged by a refusal) and the published binding (a stereo mix whose channels differ is averaged,
the partition becomes cues with the declared mouths, a stop is a closed mouth at its own frame, and
an absent partition, zero channels, a short mix and an unordered partition are each refused). The
whole tree builds and the registered CTest run is reported in the commit that carries this entry.

Not claimed. Nothing draws a mouth yet: the standalone and plug-in hosts still receive operational
state only, so the scene, painter and app wiring for M4.P3 item 3 -- including stop, seek, loop,
overlapping tracks, singer switch at the application level and reduced motion -- remain open, and
frames were read by tests rather than from a running window. Expression is still transport rather
than analysis because the only envelope a snapshot can hold is one a caller supplied. The cue kinds
are the dock's drawing vocabulary, not a phonetic transcription, and a stop is drawn as a consonant
unless the product's own `cl` symbol declares a closure. Nothing was listened to, no artwork was
reviewed, and no unit acceptance changes.

## The dock draws the phrase that is sounding, not the state it is reporting

A character resource describes six operational states -- Neutral, Focused, Rendering, Complete,
Warning and Error -- and the native dock drew exactly one of them. Nothing in the product carried
the performance, so nothing could open a mouth while audio played, and there was no place to put
that fact without overloading Rendering or Complete, which are readiness states and would then be
lying about readiness for as long as a phrase lasted.

`CharacterPerformanceSnapshot` is a bounded, immutable read model built from the same phrase result
that is audible. It keeps only what a presentation can honestly draw: the identity and render
revision of the resource it came from, the phrase span, a fixed analysis window, per-window energy
normalized to that phrase's own peak, an expression envelope that is copied window for window when
one was supplied and reported as zeros with `expressionMeasured` false when one was not, and one cue
per phone span carrying the phone it came from and the shape the dock can show for it. It carries no
file paths, no portraits and no decoded audio. A build refuses what it cannot describe honestly: an
envelope of the wrong length is refused instead of stretched, cues must be ordered, non-empty and
inside the phrase, the sample buffer must cover the span it claims, the identity must be complete
with a real digest, the window length is bounded, and a build that was already cancelled is refused.
Reading is then a pure function of the snapshot and a playhead, so stop, seek and loop are the
caller's own arithmetic over one immutable model, and a playhead outside the span is a closed mouth
with zero envelopes and `performing` false rather than silence being asserted.

The dock holds that model beside its operational state instead of inside it. `CharacterPresentation`
validates a snapshot before accepting it and refuses one it cannot draw rather than replacing what is
already showing, and playing, seeking or clearing a phrase leaves the operational state exactly where
it was: a warning stays a warning while a vowel is on screen. With no snapshot loaded the frame is
closed and not performing, so the absence of a phrase is not drawn as a phrase.

Verified. `seam_character_performance_tests` passes 16 of 16, registered as CTest
`seam_character_performance_tests`: a constant 0.5 phrase produces nonzero bounded energy and zeros
with `expressionMeasured` false; an all-zero phrase stays zero instead of dividing by an absent peak;
two constant windows of 0.25 and 1.0 normalize to exactly 0.25 and 1.0; a 500-frame phrase covers
three windows and its partial last window still normalizes; cue spans resolve to the phone, the
shape and the frame that covers the playhead, including a closed mouth for a playhead between cues;
the span is half-open, so a playhead before the origin, at the end and past the end is closed, zero
and not performing; repeated reads of one playhead are equal around an intervening read elsewhere;
an expression envelope of the wrong length is refused with a message; a supplied envelope is copied
window for window and clamped; unordered, past-the-end and empty-span cues are refused; empty,
inverted and short-buffer spans are refused; a malformed digest, a missing pronunciation identity, a
48001-frame window and a zero-frame window are refused; a cancelled build returns Conflict; and a
tampered snapshot's own `validate()` refuses an inflated energy, a negative expression, a truncated
envelope, a mismatched envelope pair, a missing resource version, a short digest, an empty span, an
unsupported schema and an unordered cue. Then the dock itself: an accepted snapshot plays while
`State::Warning` stays Warning, the same warning survives a playhead outside the span and a clear,
and a tampered snapshot is refused with the previous frame still drawn. `seam_character` and
`seam_native_ui` were rebuilt from the whole tree and the whole registered CTest run is reported in
the commit that carries this entry.

Not claimed. Nobody listened to anything and no artwork was reviewed: the phone-to-mouth mapping is
an engineering default for the dock's own drawing vocabulary, not approved character art and not a
phonetic claim, and no phrase-synchronized production asset set exists yet. Expression here is
transport, not analysis, because the only envelope a snapshot can hold is one a caller supplied. The
frames above were read by the test, not from a running window, so no window, event loop or repaint is
verified. This is M4.P3's performance-presentation item, which is a package implementation statement
and not a unit acceptance, and no unit acceptance changes.

## Every import path that builds a take now carries the style it will be stored under

The style migration moved language and style ownership onto the assignment and the take, and a
style-owned workspace refuses a take whose style is empty. That rule is right, but three import
paths built their take record without the style at all, so each of them failed in exactly the
workspaces the migration produced while continuing to work on a legacy producer, where an empty
style is the valid value. The native retake was fixed first; this entry closes the other two.

The native candidate import now carries the selected assignment's style, and the repository's own
check still does the deciding: a candidate whose declared style differs from its assignment is
refused by name instead of being relabelled to fit. The CLI's procedural collection derives the
style from the assignment that owns the coverage key and MIDI layer, and when no row owns that
coverage it says so, rather than letting the take record carry an empty style that would have been
reported later as a style disagreement.

Verified. `seam_style_owned_candidate_import_tests` passes 3 of 3, registered as CTest
`seam_style_owned_candidate_import_tests`: a style-owned producer imports a generated candidate
through the native action and the take keeps its assignment's style as unapproved marker-review
material; a candidate declared in another style is refused with a conflict and leaves the workspace
unchanged; and the CLI collects the same candidate into a style-owned producer, while a coverage the
inventory does not declare is refused by that cause. The fixture bakes its candidate through the
real export service, so the metadata, markers and style are the ones the product writes, and the
whole registered CTest run is reported in the commit that carries this entry.

Not claimed. The candidate is a procedural render of one recipe in one style, not a singer; the
tests cover one coverage key at one pitch layer, and they do not exercise a candidate produced by a
neural model or by a recording. This closes an import-path defect class, not a qualification. No
unit acceptance changes.

## A retake in a style-owned workspace was refused by the retake itself

The native retake action imports new material over the selected unit's current take. In a
style-owned producer it could not: the import request it builds never carried the assignment's
style, and a style-owned workspace refuses a take whose style is empty, so the action failed with an
incomplete-identity error in exactly the workspaces the style migration produced. A legacy
workspace hides the defect, because there an empty style is the valid value.

The retake now carries the assignment's own style, which is the same style the assignment, the
inventory row and the take's source binding all record. Nothing else in the action changed: it still
derives the retake's take id, still supersedes the current take, still records the importer's own
review, and still starts the new take unapproved.

Verified. The connected regression below now drives the whole M1.P3 item-four route in one chain
and passes: a marker edit on the selected unit through the native action, saved before review; a
rejection decision that leaves the assignment rejected with no candidate and publication refused;
the native retake over that rejected unit with freshly inspected material; the check that the retake
material is not covered by the assessment recorded for the previous material
(`requireTakeSourceQualification` fails for the new take); a fresh evidence capture and assessment
that restores qualification; a new draft from the retaken material whose audio digest is the
retaken take's own; an explicit new review that accepts it; publication; signing; installation; and
a new song that reports complete coverage, exports, saves, reopens and re-exports identically from
the installed bank whose unit digest is the retaken take's raw asset digest. The full Release build
and the registered CTest run are reported in the commit that carries this entry.

Not claimed. This is still a synthetic fixture: the retake material is a generated sine take, the
assessment is a fixture declaration, and no listener judged either take. The marker edit is one
marker of one unit; the rejection and retake cover one assignment at one pitch layer. The route is
proved connected, not musically qualified. No unit acceptance changes.

## The generated take is what the installed bank sings

M1.P3's automated exit names one deterministic lifecycle: source, generation, edit, review,
candidate, package, install, new song. Every part existed, but not in one chain. The campaign's
plan/run/cancel/resume route was exercised by its own suite; the review decision, publication,
signing, installation and new-song export by another. Between them sat the claim the milestone
actually rests on -- that the material a campaign committed is the material the installed bank
sings -- and nothing asserted it.

`seam_original_singer_campaign_workflow_tests` now runs that chain as one test on a style-owned
producer workspace. It plans a one-batch campaign from a recipe and refuses to advance until the
rendered held-out preflight beside it passes; advances to completion and asserts what was committed
is marker-review material rather than a finished resource; records the explicit current
source-quality assessment a style-owned producer requires, through the native source-quality
action, which had no test caller before this and whose only other caller is the app surface; creates
the editable manifest draft from the collected material through the native action and asserts the
draft's audio is byte-identical to the raw asset the campaign committed; proves that publication
before any decision is refused and leaves no directory behind; accepts through the native reviewer
path; publishes through the native publication action; signs and installs the candidate through the
product's own installer; and then, with the producer workspace renamed away, adds the syllable the
assignment covers, sees complete coverage, exports master and stems, saves, reopens and re-exports
with an identical master hash. The end-to-end audio check is the point: the installed bank's unit
file digest equals the collected take's raw asset digest, so the song is singing generated material
rather than a fixture that merely shares a path.

Verified. `seam_original_singer_campaign_workflow_tests` passes 1 of 1, registered as CTest
`seam_original_singer_campaign_workflow_tests`, and the full Release build plus the registered
CTest run are reported in the commit that carries this entry. The covered refusals are the
preflight gate, publication before a decision, and the explicit assessment requirement; the covered
positive path is campaign, draft, review, publication, package, install, new song, export and
reopen.

Not claimed. Every identity here is a synthetic fixture: no listener heard the generated take, the
source-quality outcome is a fixture declaration rather than an independent judgment, and the
workspace carries one phone class at one pitch layer. The campaign in this chain is a single batch;
multi-batch advancement, cancellation and resume remain covered by `seam_studio_campaign_tests`
rather than here. The marker edit, rejection, retake and explicit re-review route was folded into
this chain by the entry above; the real recording journey and a genuine independent reviewer are
still external evidence. No unit acceptance changes.

## A neural graph is admitted from its own bytes, not from a description of itself

Admission checked the frozen manifest, the configuration document, the vocabulary and the execution
identity, and its own comment said what that was not: the mel declaration was compared against the
configuration's own fields, never against the graph files. Two files that each looked individually
plausible and disagreed about the feature representation would be admitted together, and a bundle
could carry arbitrary bytes -- a custom-domain operator, an initializer stored outside the file, a
shape nothing bounds -- into the child that executes them, because nothing had read the graphs.

The graph files are now read. `graph_contract.hpp/.cpp` parses the ONNX ModelProto wire format
directly, with no protobuf or ONNX Runtime dependency in the resource path, under explicit byte,
depth, node, tensor and initializer budgets, and no declared length is trusted before it has been
checked against what is actually left. It reports the IR version, the operator set and producer,
every graph input and output with its element type and rank, the operator set the file uses, and the
initializer count and declared tensor bytes. A dimension the model does not bound is recorded as
unbounded rather than given a default, so a caller that needs a bound refuses on that count instead
of assuming one.

The shape is closed. Unknown fields in any message are refused instead of skipped, so a second
representation cannot ride along unnoticed. Refused by name: external tensor data and a tensor
segment, the three attribute fields that carry a subgraph (this reader does not inspect subgraphs),
a custom operator domain, an operator outside the admitted standard-domain set, a second operator
set, an IR version or opset outside the admitted window, a duplicate tensor name, a graph with no
input or output, a malformed or truncated encoding, and a payload past its byte or tensor budget.

Admission then binds the pair rather than describing it. The acoustic graph's mel output and the
vocoder graph's mel input must each be unique, rank three, floating point, and agree with the
configured layout and bin count on the feature axis and with each other on element type; a dynamic
feature axis is refused, because the vocoder would then have to accept whatever the acoustic graph
happened to emit. The configured vocoder output name must be a declared output, floating point,
rank at most two, and not more than one channel. The prepared handle retains both contracts, so what
the files declared travels with the admitted execution identity instead of being reduced to the
configuration's description of them.

Verified. `seam_neural_worker_protocol_tests` passes 23 of 23 with three added cases. One reports
what a graph declares: IR 9, opset 17, the producer string, the operator set the file uses, node and
initializer counts, declared initializer bytes, and a mel output whose symbolic time axis is recorded
as unbounded while its rank and 80-bin feature axis stay known. One refuses bytes no admitted export
family produces: an unknown model field, a custom operator domain, an unknown operator, a
subgraph-bearing attribute, an externally stored initializer, opset 12 and 22, IR 11, a second
operator set, a duplicate tensor name, a graph with no input, a truncated payload, a non-protobuf
payload, an oversized graph and an over-budget initializer. One refuses a pair of individually valid
graphs that disagree -- 64 bins against 80, an unbounded feature axis, a different mel element type,
a vocoder output name the graph does not declare, and a vocoder that declares two channels -- while
the pair the configuration describes is admitted, so the refusals are the difference and not the
absence of a working path. The four other suites that prepare real bundles pass as well
(`seam_neural_render_tests` 5/5, `seam_neural_phrase_runner_tests` 4/4,
`seam_neural_render_workflow_tests` 5/5, `seam_neural_selection_tests` 4/4), because their
fixtures now build real ModelProto bytes through a shared test-only encoder instead of a placeholder
string. The full Release build passed and the registered CTest run passed 150 of 151; the single
failure is the tracked-source-closure check, which only refuses while a new source file is untracked.

Not claimed. This admits declared structure, not behaviour: no operator semantics, shape propagation,
numerical result or memory bound is checked, and no inference is performed here. The admitted
operator set is a curated standard-domain list, so a legitimate export using something outside it is
refused by name and extending the list is a deliberate change with its own review. In the bundle
path the byte bound is the frozen asset's own admitted length, so a large graph is bounded by the
bundle rather than by a separate graph-specific ceiling, and no peak-memory or CPU ceiling and no
operating-system sandbox is claimed. The production bundle-conditioned launcher, the child's own
re-admission of the exact bytes it loads, Windows supervision, installed-host qualification and any
learned model remain open, so M2.P1 is not complete and no n changes.

## A coarticulation boundary is a transition the plan declares, not a weakened overlap

The plan had one list. A gesture either owned its frames by itself or the phrase was refused as an
overlap, so a consonant could not colour the vowel that followed it, and the only movement the model
could name was an approximant's own last frames. Ordered linguistic spans and acoustic overlap were
the same thing, which is why the boundary between them could not be stated.

They are now two lists. The gestures stay an ordered, non-overlapping partition of the phrase, and a
separate transition plan names every boundary that carries an acoustic overlap: which gesture to
which, the absolute span, the frame count, the mode, and what each layer does across it. The
composition record says whether voicing continues or the release owns the frames, whether the
target's own attack is inside the window, and that noise crosses the boundary with its own source
rather than being retriggered by the tract.

The mode is decided per boundary, and the decision that a boundary is not a movement is a measured
one. Two voiced poses move: one filter runs the whole time and its poles travel from the pose the
tract holds into the next over 20 ms, except where a nasal consonant is one side, because a nasal
tract is a different topology rather than another point in one parameter space and that boundary
keeps the crossfade the two banks were built for. An approximant's own declared motion is the
transition itself, so the renderer no longer infers it from the gesture order. A closure, a pause or
a breath is not a pose, so the next gesture crossfades out of whatever the tract held, and the record
says the release owned those frames. A voiceless frication, plosive or affricate into a voiced next
gesture is a 20 ms crossfade with voicing not continuing, not a movement: a voiceless consonant is
carried by its own noise source, so the tract is not sounding across that boundary and the vowel is
an attack to blend in. Declaring a movement there was implemented and measured first, and it
attenuates the vowel's onset -- the pilot's own diagnostic pitch regression lost four voiced frames
and then read one frame an octave low in the note that begins with a fricative -- so the boundary
declares the crossfade at the same length instead, and the regression passes with every analysed
frame of every note within fifty cents.

`VocalTract::interpolateTo` is the mechanism. One filter runs for the whole window and its
resonance frequencies, bandwidths and gains are re-designed each frame from the pose the tract holds
to the pose it is told to arrive at, with the filter state carried across, the nasal stage moved by
the same smoothed progress, and the final frame landing exactly on the target pose rather than on
its own interpolation. Nothing is invented for the starting side: it is whatever this tract holds.
The renderer reads the plan instead of deriving a window of its own — it looks up the entry
transition at the boundary and the declared motion for an approximant, uses that frame count, and
applies the declared mode. A boundary the plan does not describe keeps the plain crossfade it always
had, and so does a declared movement whose two banks are not two points in one parameter space (a
different resonance count, or a nasal-only side): the phrase renders as it did before this existed
rather than being refused.

Verified. `seam_articulation_context_tests` passes 15 of 15 with three new cases. A vowel, a
voiceless fricative and a second vowel in one note compile into exactly one transition -- `s` to
`i`, a crossfade, 20 ms, starting where the vowel starts, with the composition flags that boundary
declares -- while the three gestures still tile the phrase exactly once. Two vowels in one note,
both poses the tract holds while it sounds, compile into one transition with the other mode: a
formant movement of 20 ms that starts at the second vowel and reports voicing as continuing. The
mechanism is driven directly over a pulse train: inside the window the tract is neither the pose it
started from nor the one it declared, and once the window has closed and the resonators have settled
it holds the pose it declared rather than the one it started from; the window is exactly as long as
it declared itself and is no longer pending; and the same window processed in 97-frame blocks is the
same signal as the whole render, which is what keeps the movement a function of the frame index
rather than of the caller's block size. `seam_singer_pilot_cli` passes, including the diagnostic
pitch assertion that failed while the voiceless boundary declared a movement. The full build and the
registered CTest run are reported in the commit that carries this entry.

Not claimed. Nothing here is intelligibility: no listener has judged a consonant-coloured vowel, and
20 ms is the renderer's own previous window rather than a measured phonetic duration. The moving mode
starts from the pose the tract already holds, so an ordinary voiceless consonant never shapes that
movement; a palatalized consonant does put its own pose in force during its own gesture, and only
then does the onset window start from the consonant's shape. The mode decision at the voiceless
boundary rests on one measured diagnostic rather than on listening, and it is a decision this plan
declares and can revisit rather than a silent one. Aspiration and the burst appear in the record as
which layer owns the frames, not as a measured aspiration model. M1.P2's ten required changes are now
all landed, which is a package implementation statement and not a unit acceptance, and no n changes.

## A gesture that crosses its own note is bounded by the phrase, not the note

The articulation layer refused any gesture whose span left its own score note: a consonant the
creator placed before its beat, or a release that carries into the next note, came back as a refusal
naming the missing feature instead of rendering. The timing layer had accepted those offsets all
along -- an authored phoneme offset is absolute from the note start and may be negative -- so the
score a creator wrote and the audio the renderer produced disagreed about who owned the frames.

The plan now bounds a gesture by the phrase context and by the partition rather than by the note box.
A span may start before its own note or end after it, provided it stays inside the context the
performance defines and the neighbouring gesture owns the frames on the far side of the boundary,
which the ordered non-overlapping plan already guarantees. Every note still needs its own gesture, so
a crossing can never swallow a neighbour: the note that receives the frames keeps material of its own,
and the moving gesture stops where that material begins.

Verified. `seam_articulation_context_tests` covers both halves through the real timing compiler and
the real phonemizer. A vowel that releases twenty milliseconds early and a following note whose
consonant begins exactly those twenty milliseconds before its own beat compile into three gestures
that meet once, the frication sounds on the far side of the boundary, and the same owned range renders
identically in one window or two. The same pre-onset without the vowel yielding is still refused as an
overlap, so the crossing is admitted because the phrase accounts for it rather than because the note
stopped mattering. A coda whose release keeps the opening twenty milliseconds of the next note is
admitted on the same terms, with that note's own vowel starting where the coda ends.

Not claimed. This is timing ownership, not articulation quality: no listener has judged a pre-onset
consonant, and coarticulation between two gestures inside one note -- the other half of the package's
third required change -- remains open, together with the shared transition plan that would describe
it. No unit acceptance changes.

## An event phone is a declared span, not a recorded articulation

The pilot inventory's last 48 refusals were not a missing source but five symbols that are events:
the moraic obstruent `R`, a glottal occlusion `glottal`, a pause `pau`, a closure `cl` and a breath
`br`. The model had no way to say that a span is exactly silent or that a span is noise in its own
right, so the sound the inventory asked for was the one thing the recipe could not describe, and the
Japanese score adapter never admitted `R` or `br` at all.

Recipe schema eleven declares them. `closures` names the symbols whose resolved span is exactly
silent -- the moraic obstruent as the tail of its vowel's note, a glottal occlusion as the span
before its vowel, a pause or a standalone closure as its whole note -- and `breaths` names `br` with
its own source spectrum. Two gesture kinds carry them: `ArticulationGestureKind::Closure`, which
neither lane excites, and `ArticulationGestureKind::Breath`, which the aperiodic lane renders from
the declared source. Nothing is inferred from a symbol: a closure is admitted only for the four
symbols whose semantics it is, a breath only for `br`, and only in a style the recipe declares, so a
symbol a newer build happens to recognise never becomes silence on its own.

Three repairs were needed beyond the schema. The timing plan treated a `Geminate`, `Breath` or
`Silence` role as an ineligible syllable, so a moraic obstruent could never receive a resolved start;
an event phone now occupies the edge its position implies. A phrase can be nothing but event spans,
so the renderer holds no vocal tract rather than borrowing another phone's pose, and the snapshot
validation names the event's own symbol instead of demanding a voiced pose it never renders. And a
candidate from a recipe that declares several families is no longer refused for carrying a
lower-numbered candidate schema: what proves its claim is that every marker finds its own binding in
its own recipe, which the marker loop already checks.

Verified. `seam_singer_pilot NEW_OUTPUT_DIRECTORY events` renders `a R`, `br`, `cl`, `glottal a`,
`pau` and a control vowel, and the CLI regression asserts what makes an event an event: every closure
span is exactly zero and the breath span is not, the candidate is schema eleven with
`closureRevision` and `breathRevision`, and a repeat run is byte-identical. `seam_tests` covers the
unit shape directly, baking an event unit through the export service and loading it back as a
candidate -- the path that used to refuse it for having no voiced pose. The pilot inventory now
prepares 1026 of 1026 assignments, up from 978, and a campaign over the whole inventory plans as 1026
jobs and passes the rendered held-out preflight on all 38 declared classes with none defective: the
first time that preflight could be run over the full inventory.

Not claimed. A closure is silence, so the moraic obstruent, the pause and the closure prepare as zero
audio rather than as a sung consonant; a glottal stop's release is not modelled apart from its
closure; and a breath is a declared engineering spectrum rather than a measured aspiration. No
listener has judged any of them, and a prepared class is not phonetic qualification. Pre-onset
context that reaches before its owning note and coarticulation inside one note remain open, and no
unit acceptance changes.

## The durable legacy migration is the only path into style ownership

The producer schema that carries style and language ownership is an upgrade, and until now only a
plan for it existed: the Python planner verified a legacy inventory, prepared the target state and
refused to apply anything, while every C++ write path refused to cross into the new schema. The
durable operation now exists. `migrate-style` (and the repository method behind it) takes an exact
plan document, checks that it was prepared from this producer's current durable bytes and from a
verified inventory digest, resolves the single style and language the plan declares, and then
receives the plan's own proposed project as a second opinion rather than a trusted answer: the
operation computes the migration itself and refuses unless the two encodings agree.

Three things follow from that. Legacy ownership without a unique style binding is refused by name
("explicit per-assignment evidence") instead of being resolved by a default. An approval taken under
the style-free identity cannot survive into style ownership, because the review basis changed, so the
affected assignments and their takes become review-required while every historical review record
stays exactly where it was. And the generic save still refuses the same transition, so the migration
is a distinct, auditable operation rather than a flag on an ordinary write. The exact plan bytes are
retained under `migrations/<plan sha256>.json` before the generation that depends on them is written,
the journal records `style-migration` with that digest as its subject, and the previous generation stays
readable through the ordinary historical recovery path.

Verified. `seam_production_style_migration_tests ` passes 4 of 4: a resolved plan migrates a real
imported legacy producer as one new generation, retains the plan, leaves the pre-migration generation
readable and refuses to apply the same plan twice; an unresolved plan is refused with the producer
unchanged and no retained plan; a stale snapshot, a plan whose proposed project keeps an approval, a
plan that proposes two styles, a non-producer actor, a bad timestamp, a malformed document and an
already-applied plan are each refused; and a generic save still cannot carry a legacy producer into
style ownership. The cross-language acceptance runs in `seam_public_release_python_tests`: the Python
planner writes the plan, the CLI applies it, the durable project equals the plan's proposed project
with the generation advanced, the plan is retained byte-for-byte under its own digest, and a second
application is refused. That suite passes 92 tests.

Not claimed. This migrates ownership, not qualification: no listening, review or range evidence moves
with it, the plan's own qualification field is `REASSESSMENT_REQUIRED`, and the migrated generation is
release-ineligible.

## A voiced affricate is a closure that carries voicing, its burst and a voiced tail
## A voiced affricate is a closure that carries voicing, its burst and a voiced tail

Japanese じ and じゃ are voiced affricates, and the engine refused `j` by name rather than let an
unvoiced noise pair stand in for it. That refusal was correct while the model had no way to voice a
closure and a frication tail separately inside one gesture; recipe schema ten now declares the three
parts and the articulation plan composes them into one gesture, so the phone keeps one marker and one
identity in the bank. The prevoiced closure and its burst are rendered from the score's own excitation
by the voiced plosive source, and the tail is rendered as voicing through the tract with frication
noise added on top, exactly as a voiced fricative already was. A block never straddles the release
end, so the two ways of being voiced cannot bleed into each other.

Candidate metadata version ten records the same fact: a `voiced-affricate` marker, the affricate
model revision, and the voiced plosive revision its closure is rendered by. The loader refuses a
candidate whose closure is silent, whose tail is unvoiced, whose burst is missing, or whose recipe
does not declare all three.

Coverage moved from 948 to **978 of 1026** assignments. What remains is 48: the two adapter symbols
with the breath sequence (`R`, `glottal`, `br`) and the pause and closure events (`pau`, `cl`), which the
inventory names as units although a pause and a closure are events rather than recorded material.

Rendered evidence: a four-unit campaign (`cv:j:a`, `cv:j:i`, `cv:j:u` and the coda `vc:a:j`) planned,
preflighted and advanced with no hand-edited JSON. The preflight passed with four phrases produced and
zero defective, and the campaign collected four takes in ` MARKER_REVIEW `. Measured on the collected
`cv:j:a` take: the closure window holds low-band energy 2.87e-05 and high-band energy 3.5e-10, and the
tail holds 1.2e-05 low and 3.9e-05 high. The unvoiced control `cv:ch:a` from the retained pilot bank has
exactly zero energy in the same closure window and 6.9e-03 RMS in its burst, which is the difference
between a voiced affricate and an unvoiced one measured rather than asserted. The workspace, campaign,
preflight report and recipe are retained under `build/pilot-01/voiced-affricate-{ws,campaign}` and
`build/pilot-01/voiced-affricate-recipe.json`. Suites: `seam_voice_design_tests ` 39 of 39 and
`seam_articulation_context_tests ` 11 of 11, including a case that requires the voiced closure to carry
energy, its high band to stay below its low band, and the tail to add noise an order of magnitude
above the closure's.

Not claimed. No listener heard anything, no reviewer judged anything and no unit acceptance moved.
The spectra are declared engineering parameters, so nothing here says a `j` sounds like a singer.

## A palatalized consonant is its base's release through its own pose
## A palatalized consonant is its base's release through its own pose

The pilot inventory names `ky gy hy py by my ny ry fy vy`, and the phonemizer emits them as single
phones: きゃ is `ky` plus `a`, not `k` plus `y` plus `a`. The engine refused every one of them
because the style bound only a plosive `k` and the plan had no way to say what makes `ky` different.
Declaring `ky` as a plosive would have been substituted noise, so recipe schema nine declares the
relation instead: `palatalized` binds a phone to the base consonant whose release it borrows and
requires a same-style resonance pose named after the palatalized phone itself. Validation also
requires the base to be bound in the same style, whether as a frication, a plosive, an affricate, an
approximant or a nasal pose, and refuses a phone that names itself as its own base.

The articulation plan copies the base's release under the palatalized phone's own name, so the marker
and the bank unit keep `ky` rather than becoming a second `k`, and marks the gesture with the pose it
has to carry. The stream puts that pose in force from the gesture's first frame even when the gesture
is unvoiced, which is what colours the release and the vowel's onset transition; it also refuses a
plan whose pose and frozen recipe disagree, so a base consonant cannot pass as its palatalized form.
Candidate metadata version nine records the same fact with a per-marker flag and a model revision, so
a collected take cannot be reread as a plain base consonant.

Two smaller repairs came with it. The fricatives the scale needs were unbound: `sh`, `h` and `f` are now
declared unvoiced frication and `z` and `v` are declared voiced frication with the same-phone resonance
pose that source requires, which closed 150 assignments. And `fy` was classified as voiced while `hy` was
not, so ふゃ could never have been rendered as the voiceless fricative it is; the phonemizer's voiceless
list now contains it.

Coverage moved from 498 to 648 with the fricatives and to **948 of 1026** assignments with the
palatalized consonants. The 78 that remain need a voiced affricate source (`j`, which needs a prevoiced
closure and voiced frication), two adapter symbols (`R`, `glottal`) and the closure series (`br`, `pau`, `cl`).

Rendered evidence: an eight-unit campaign (`ky gy hy my ry fy vy` onsets and a `ky` coda) planned,
preflighted and advanced with no hand-edited JSON. The preflight passed with seven phrases produced
and zero defective, and the campaign collected eight takes, all in ` MARKER_REVIEW `, all 24000 frames at
48000 Hz, peaks 0.0336 to 0.1020 and nonzero coverage 90% to 100%, with the 90% being the two
plosive closures. The workspace, the campaign, its preflight report and the recipe are retained under
`build/pilot-01/palatalized-{ws,campaign}` and `build/pilot-01/palatalized-recipe.json`. Suites: `seam_voice_design_tests `
36 of 36, `seam_articulation_context_tests ` 10 of 10 including a case that requires the palatalized
onset to leave the palatal pose and then settle onto the same vowel as the plain one.

Not claimed. No listener heard anything, no reviewer judged anything and no unit acceptance moved.
The burst spectrum is still the base consonant's and the palatalization is the resonance transition,
so this says nothing about whether a `ky` sounds like a singer, and every parameter here is declared
engineering data rather than phonetic qualification.
## A review decision now covers a bank, not one unit

Every lifecycle regression in this repository drove a single assignment and a single take, so
nothing proved that the product's review-to-song path still holds when the material is a bank
rather than one held vowel. The CLI fixture in `tests/test_sample_review_cli.cpp` now builds one
assignment and one take per declared class, and a new case drives four of them -- `sustain:a`,
`cv:s:a`, `vc:a:s` and `vv:a:i` -- through the whole path.

The case captures a review packet and requires that capturing still approves nothing: four units
are left in `MARKER_REVIEW` and the encoded producer bytes are unchanged. Inspection then reports
four units, and one review commits a decision that covers all four -- the check that the single-unit
fixture could not make. Publication commits a four-unit manifest, packaging signs it, and the
installer admits it as `TrustedInstalled` with a content hash equal to the published one.
A new song whose lyrics are `あ` and `さ`, which need the consonant and the vowel sequence rather
than one sustain, then resolves with zero fallbacks, renders above 1e-4 RMS, and exports a master
and stems that match the render.

The last step is the one the plan asks for by name: the producer workspace, the candidate
directory, all four raw WAVs, the quality decision and the license are renamed away, and the saved
song still reopens and renders identically from the installed bank. `seam_sample_review_cli_tests`
passes 7 of 7 and the serial Release suite passes 150 of 150 in 282.50 s.

Not claimed. This is a synthetic fixture bank of four sine-derived WAVs with test identities:
no listener heard anything, no reviewer judged anything, no real weight, range or quality was
measured, and the material is not a singer. The exit it closes is coverage of the lifecycle
shape, so no roadmap unit, package acceptance or Beta GO status changes.

## The inventory campaign runs to completion

The pilot inventory now exists as generated material rather than as a fixture. Two campaigns
were planned, preflighted and advanced to completion against the producer workspace: the first
took one renderable coverage key per class at the lowest pitch layer (166 assignments, three
batches), and the second took the same keys at the two higher layers (332 assignments, six
batches). Both preflights passed before either campaign advanced, and neither run needed a
single hand edit of a JSON file.

Result: **498 takes committed**, 166 coverage keys at three pitch layers each, split cv 210,
vc 210, vv 60, sustain 15 and special 3. Every take is 24000 frames at 48000 Hz, every take is
in `MARKER_REVIEW` and none is approved, no take is silent, peaks run 0.0110 to 0.1571 and
nonzero coverage 90% to 100%. The 90% classes are the stops, whose closure is deliberately
silent for about 60 ms of each unit. Advancement is resumable and idempotent: repeating it on a
completed campaign returned the same status, the same 498 takes and the same durable
generation, so the recovery path holds at bank scale rather than only in a fixture.

This is the exit the package asked for -- multiple phonetic classes and connected phrases
through ordinary immutable rendering, and a campaign that completes and replays without manual
editing -- with one honest qualification: it holds for the 498 assignments the recipe can
prepare, not for the inventory as a whole. The other 528 assignments still cannot prepare at all
because they name a phone with no source model, which is model work rather than pipeline work.

The run also produced no new defects, which is evidence in its own right: 498 candidates went
through export, metadata, collection and repository commit after last entry's two repairs, and
the only thing holding them back is the qualification they were never meant to have.

A listening order is recorded rather than a defect list. Eleven of the 498 takes sit below 0.02
peak, and every one of them is a vowel-only or glide context (`sustain:a` at layer 60 is the
quietest at 0.0110) against a model whose loudest classes are the noise-bearing ones. That is
descriptive, not a threshold: the contexts are simply the first ones a listening pass should
hear. The unsupported side of the list is already retained in the coverage report.

Evidence: `assets/pilots/seam-pilot-01/CAMPAIGN_REPORT.md` records the commands, the counts,
the per-kind measurements and the listening order. The workspace, its eleven generations, all
498 raw take assets and both campaign directories are retained under `build/pilot-01/`, with
audio at `build/pilot-01/producer/assets/raw/<first two hex>/<sha256>.wav` keyed by each take's
`rawAssetSha256`. Campaigns `c312a745...` and `2b7a1c02...` each carry their own passing
preflight report beside them.

Not claimed. A complete bank is not a usable voicebank: these takes are unapproved
marker-review material with no listening, no review decision, no range or style qualification,
no identity assessment and no package, and the recipe is still a diagnostic source-filter
model, so nothing here says whether it sounds like a singer. Only one style and the three pilot
pitch layers exist. No unit acceptance changes.


## A generation job could not collect the candidates its own export wrote

The held-out preflight ran against the real inventory for the first time, and its first two
runs refused phrases for a reason that had nothing to do with sound: the collector rejected
the candidate metadata the export had just written.

The candidate loader demanded one fixed field union per schema version (23 fields for
version seven, 24 for version eight), but the export writes a revision field only for each
gesture family the candidate actually rendered. A recipe that declares one family hides
this: the earlier pilot fixtures rendered affricates or approximants on their own, and their
metadata happened to match. The pilot's maximal recipe declares frication, plosives, voiced
stops, affricates and approximants, so a version-eight approximant unit carries 21 fields and
a version-seven affricate unit carries 22. The loader had also required a release revision of
every candidate from version four upward, which a version-eight candidate that renders only
approximants never writes.

Both are repaired on the loading side, because the export's narrower field set is the honest
one: a field naming a source the candidate did not use would be a false claim. The admitted
fields are now a closed set with a per-version bound, so an invented field is still refused
while a legitimate union is accepted, and the release revision is mandatory for versions four
through seven -- whose own gesture is a plosive family -- and required of a version-eight
candidate only when it renders a plosive-family gesture. `tests/test_inventory_preflight.cpp`
now renders `cv:s:a`, `cv:ts:a` and `cv:y:a` from a single multi-family recipe and requires
every candidate to load, which is the case no earlier test reached because none of them ran a
generation job over a recipe with more than one articulation family.

The same run surfaced a second gap in the reproducible path. A workspace created from a draft
definition declares no source strategy at all, and every import and generation is refused
until one is registered, so the pilot could not generate a single unit. The pilot now
registers its own procedural source against a tracked declaration
(`assets/pilots/seam-pilot-01/source-declaration.txt`) that grants source use and
transformation and explicitly does not assert redistribution or commercial use. It is a
producer declaration, not legal verification, and it cannot authorize a human recording, a TTS
voice or any other source.

Result of the third run: **17 held-out phrases, 17 produced, none refused**, covering 20
phones and all five coverage kinds that prepare, at peaks of 0.011 to 0.157 and 90% to 100%
nonzero coverage over 24000 frames each. The campaign is now admissible, which is what the
gate was built to establish. Two readings are recorded as measurements rather than verdicts:
the stop classes sit at 90% nonzero because their closure is deliberately silent for about
60 ms, and `sustain:a` is the quietest class by about 2.4x against the next quietest, which
makes it the first context to listen to rather than a defect finding.

Evidence: `build/pilot-01/campaign-registered/preflight/report.json` bound to campaign
`c312a745...`, producer `365e8bfd...` and recipe `e759b55f...`, with the seventeen phrases,
their dry candidate audio and their metadata retained beside it, and the reading recorded in
`assets/pilots/seam-pilot-01/PREFLIGHT_REPORT.md`. The multi-family regression is in
`tests/test_inventory_preflight.cpp`.

Not claimed. The retained audio is dry candidate output from an unqualified recipe: no
listener has judged any of it, nothing was collected, and no approval exists. The preflight
establishes that the classes the campaign declares render their own gestures audibly and
reproducibly, and that the campaign may advance; it says nothing about intelligibility,
naturalness or identity. No unit acceptance changes.


## A hint's consonant had no place in the syllable

The previous entry reported that 210 of the pilot inventory's refusals were a structural
gap in the gesture model, because the message was "requires a resolved start and
associated nucleus". That attribution was wrong, and the measurement is what showed it:
the gesture model places post-nucleus gestures perfectly well, and the real cause was the
explicit phone hint.

`inferRole()` derives a token's role from its symbol alone, and an ordinary consonant
infers as an onset. That is right for a lyric, where a consonant written before the vowel
is the syllable's onset. It is wrong for an explicit `note.n` hint such as `"a s"`, which
produced [nucleus, onset]: an onset sitting after its own nucleus, which the timing plan
cannot give a start and the articulation planner correctly refuses rather than placing at
an invented frame. Symbols that carry their own role -- `N`, `cl`, `br`, `pau`, `sil` --
and vowels were never affected, which is why `"k a"`, `"s a"` and `"k a N"` always worked
and only vowel-to-consonant units failed.

An explicit hint now gives an ordinary consonant its place in the syllable: it is a coda
when it directly follows a vowel and no vowel follows it in that hint, and an onset
otherwise. The lookahead keeps the maximal-onset reading of a multi-syllable hint intact,
so `"k a s a"` is still two open syllables, and `"a cl"` and `"cl k a"` keep the roles
those symbols already had. Lyrics are untouched; a hint that mixes a coda with a
following syllable is still refused rather than guessed at.

Measured on the same inventory and recipe as the previous report: prepared assignments
rose from 288 to **498** of 1026, distinct prepared coverage keys from 96 to 166, and the
coda-resolution refusal bucket from 210 to **zero**. Every vowel-to-coda class whose models
exist now prepares (210 of the 450 `vc` assignments; the other 240 refuse for missing
models). Coverage kinds with a prepared class rose from four to five, and no kind is
blocked by the compiler any more: the 528 remaining refusals are 300 missing voiced
models, 186 missing noise models and 42 symbols the Japanese adapter cannot resolve
(`R`, `glottal`).

The rendered unit is two ordered gestures, not one stretched one. A vowel-to-coda note
compiles to a vowel that owns the note up to the coda's resolved start and a coda that
owns the tail, with audio in both spans, and the whole range renders identically in one
window or two. A coda whose start was never resolved is refused instead of being placed at
an invented frame, and a coda that would begin before its own nucleus is a conflict.

Evidence: `tests/test_phonemizer.cpp` asserts the role each hint produces, including the
unchanged `"k a"`, `"k a N"` and `"k a s a"` readings and the intrinsic roles of `cl`.
`tests/test_articulation_context.cpp` asserts the coda plan (ordered, non-overlapping
spans, the refusal of an unresolved coda and of one crossing the nucleus) and renders the
unit end to end from a real hint through the phonemizer, the compiler and the articulated
stream, including chunk equality. `tests/test_inventory_preflight.cpp` renders a `vc:a:s`
inventory class through the ordinary generation job and asserts the marker sequence
`a, s`, so the inventory's own declared class is now an audible pair. The retained
measurement is `assets/pilots/seam-pilot-01/coverage-report.json` with
`assets/pilots/seam-pilot-01/COVERAGE_REPORT.md`.

Not claimed. No listener has judged any coda, so nothing here is intelligibility: the
assertion is that the coda renders as its own gesture with audible audio. This repaired
placement *inside* one note; the plan's phrase context beyond the owning note -- pre-onset
and release intervals that reach past a gesture's own note -- is still open, and the
classes that remain unrenderable need new source models rather than compiler work. No unit
acceptance changes.


## Half of what the pilot inventory cannot prepare is structural, not a missing sound

The plan requires a coverage report showing missing phone classes and transitions before
thousands of jobs run. The inventory declared 342 coverage keys and nothing had ever
checked them against the recipe that was supposed to sing them.

`inspectInventoryCoverage()` now compiles every assignment's inventory score and
snapshot against a recipe and returns a canonical report: one entry per class with its
status, frame span and, when it refuses, the refusal message that was actually raised.
It also derives the declared requirement independently of what happens to prepare, so a
phone named by any assignment stays required even when every assignment naming it fails,
and reports the phones and kinds nothing covers. It renders no audio, collects nothing
and reserves no assignment, and it refuses an inventory larger than its bound rather than
truncating. The CLI command `inspect-generation-coverage WORKSPACE RECIPE_JSON
NEW_REPORT_JSON` writes the report to a new path and exits zero even when the inventory
is incomplete, because the report is the deliverable and its `status`,
`missingPhones`, `missingKinds` and `refusedClasses` carry the answer.

Measured on the real pilot inventory with the pilot's own maximal recipe: 288 of 1026
assignments prepare, 738 refuse; 96 of 342 coverage keys prepare; 20 of 41 phones and 4
of 8 kinds are covered. cv, vv, sustain and special have prepared classes; every `vc`,
`release`, `glottal-attack` and `breath` assignment is refused. The prepared phones are
`N a b ch d e g i k m n o p r s t ts u w y`.

The refusals split four ways: 300 missing voiced models (z, j, v and the palatalized
voiced clusters), 186 missing noise models (sh, h, f and the palatalized unvoiced
clusters, plus the closure and pause events), 42 symbols the Japanese score adapter
cannot resolve at all (`R`, `glottal`), and **210 that are none of those** -- vowel-to-coda
placements of phones such as `t k p b d g s ch ts n m r w y` whose models already exist
and prepare as onsets. Those 210 refusals come from the gesture model itself: a gesture
after the nucleus cannot resolve its own start and associated nucleus. That is the cost
of the gap the plan already lists as items two and three of this package, and it is now
measured rather than asserted.

**Corrected by the next entry:** those 210 refusals are real, but the cause is not the
gesture model. They come from the explicit phone hint, which inferred an ordinary
consonant's role from its symbol alone and therefore wrote a post-nucleus consonant as an
onset. The entry above stands as the measurement; its attribution is superseded, and the
planned coda and context work is not what removed them.

Consequence for the next step: a campaign over this inventory cannot be planned at all,
because planning refuses at the first class that cannot prepare, so the held-out
preflight cannot yet be run across the whole bank. The coverage report is the retained
defect list until the coda and context work lands, and the singable subset today is
onset syllables over twenty phones, vowel-to-vowel, sustain and the syllabic nasal.

Evidence: `tests/test_inventory_coverage.cpp`, registered as CTest
`seam_inventory_coverage_tests`, covers a fully prepared inventory (status, counts,
canonical report fields, no collection) and an inventory whose voiced-affricate class
cannot prepare (the refused class is named, the uncovered phone is listed, the frame span
stays zero), the assignment bound, and the CLI path including its refusal to replace a
retained report. The retained measurement is
`assets/pilots/seam-pilot-01/coverage-report.json` with the reproduction commands and
interpretation in `assets/pilots/seam-pilot-01/COVERAGE_REPORT.md`.

Not claimed. This is snapshot compilation, not audio: no waveform was produced and no
listener heard anything, so it says nothing about quality or intelligibility, only about
which classes the recipe can prepare. The numbers belong to one recipe revision and one
inventory revision, both named by hash inside the report. A `PREPARED` class is not a
qualified class -- the rendered preflight is still what proves audio exists. No unit
acceptance changes.


## A campaign cannot multiply a phone class before its phrases have rendered audibly

The pilot inventory has 2052 assignments and 342 distinct coverage keys. Planning a
campaign compiles each take's performance, which proves a class can be *prepared*; it
does not prove the class renders its own gestures or any audio at all. One defective
phone class would have been discovered only after it had been baked into every unit that
names it, which is the opposite of the order the plan asked for.

A campaign now has to clear a held-out preflight first. The selection is the campaign's
own jobs, chosen deterministically with a greedy cover over two required sets: every
distinct phone symbol any job declares, and every distinct coverage kind. Jobs are
ordered canonically, so the same campaign always yields the same phrase set, and the
bound is a refusal rather than a truncation -- a campaign whose classes need more phrases
than the bound allows is rejected with the count, the phone and kind totals, and the
bound, instead of quietly testing a subset.

Each selected phrase then renders through the ordinary immutable generation path, one
job at a time, into `preflight/phrase-N/`: dry candidate audio, the candidate metadata
and its planned gesture markers. Nothing is collected, so the producer's takes and
durable generation are untouched and the preflight cannot approve anything. The
measurement compares the coverage key with what came out: every phone the key declares
must appear as its own gesture, the phrase must not be silent unless every phone it
declares is a silence event, and the peak and nonzero-frame count are retained per
phrase. A phrase that refuses to render keeps its refusal message as its detail rather
than being dropped.

Advancement is gated on the result. `advanceGenerationCampaign()` now requires
`<campaign directory>/preflight/report.json`, and the report is admitted only when it
names the campaign it was asked about, the campaign's frozen producer hash and recipe
hash, a phrase set that equals the campaign's own canonical selection in order with each
phrase's coverage key matching that job, and a status and defective list that follow from
its own phrase verdicts. A campaign with no report, a stale report, a report that skipped
a class, or a report that did not clear every phrase is refused before a single batch is
prepared, rendered or committed.

Evidence: `tests/test_inventory_preflight.cpp`, registered as CTest
`seam_inventory_preflight_tests`, covers the coverage-complete selection and its
bounded refusal, a passing preflight that retains dry audio and leaves the producer
state untouched, the gate (no report, a failing report, and then a passing report that
does advance and commits unapproved takes), the canonical admission of the report
(status-only rewrite, dropped phrase, relabelled coverage key and another campaign's
digest are all refused), and the CLI command end to end. The Studio campaign suite and
the export-service campaign suite now preflight before they advance, so the existing
plan/advance/cancel/resume paths are exercised through the gate.

Not claimed. The report is local, unsigned evidence: it is verified against the
campaign's frozen inputs and re-derived structure, but a consistent hand edit of a
failing verdict is not detectable without re-rendering, and the retained dry audio is
what a reader checks instead. No listener has judged any preflight phrase, so a passing
report means the classes rendered their declared gestures audibly and nothing more. The
real pilot inventory has not been preflighted yet: planning it still refuses the phones
whose models are absent (`j`, `R`, `by`, `gy`, `cl`, `pau`, `br`, `glottal`,
`v`, `z`, `h`) before a campaign exists, so producing the retained defect list for the
full inventory remains the next step. No unit acceptance changes.


## A liquid or glide is the transition it declares, and it lands on the nucleus

The pilot's Japanese inventory names `r`, `w` and `y`, and none of them could be
rendered: `ら`, `わ` and `や` were refused as unsupported voiced phones, so three
ordinary syllables of the pilot language had no path at all. The remaining two
phone classes of the plan's articulation list are now admitted, and the way they
are admitted is the point -- an approximant is not a vowel with a different
resonance bank, it is a *motion* into the vowel that follows it.

`VoiceRecipe::ApproximantPose` (schema eight) binds a phone and style to its own
resonance pose and to the milliseconds of formant transition that carries it
into the neighbouring vowel. It is an opt-in field: a recipe written before it
keeps its previous meaning, and a phone is never reinterpreted as an
approximant because a newer build would like it to be. A declaration needs a
same-phone, same-style resonance pose to move from, must be between 5 ms and
200 ms, and an unvoiced token or an unadmitted symbol is refused by name rather
than voiced or treated as a vowel.

`ArticulationGestureKind::Approximant` is a voiced, non-aperiodic gesture with a
`transitionFrames` member the plan derives from the frozen recipe and clamps to
the note's own span. The renderer starts that transition exactly
`transitionFrames` before the gesture ends, so the declared motion occupies the
end of the glide and arrives at the vowel nucleus instead of being a short step
at the vowel's own onset. Two details make that truthful rather than
approximate. A transition can only be scheduled at a block boundary, so the
block ends exactly where the motion must begin -- otherwise the same owned range
would render differently depending on how a caller split it, which is what the
chunk-invariance contract forbids. And when the entry crossfade still owns the
tract at that frame, the declared window is compressed to land on the nucleus
rather than dropped, because this tract owns one transition at a time.

Candidate metadata gains schema eight, an `approximant` marker kind and an
`approximantRevision`. A schema-eight recipe also satisfies a schema-seven
candidate, exactly as version seven already satisfied version six.

Evidence: `tests/test_articulation_context.cpp` covers the gesture kind and its
voiced classification, the bounded and clamped transition, the voiceless-token
and unadmitted-symbol refusals, and the two measurements that make the motion a
claim rather than a schedule: the low-band balance of the glide's own span rises
from 0.0013 to 4.78 across the transition, and a five-millisecond declaration of
the same recipe leaves the same window at 0.0025, so the declared duration is the
cause. Whole-versus-chunked equality and the refusal of a plan whose transition
differs from the frozen recipe are asserted in the same case. The pilot's `glides`
fixture renders `ら・わ・や・あ` and the CLI regression checks schema eight,
`approximantRevision`, the marker sequence `r a w a y a a`, that no segment of a
glide is silent, that each glide's own span measurably changes its band profile
rather than holding one pose, and repeatability; run it with
`build/release/seam_singer_pilot NEW_OUTPUT_DIRECTORY glides`. The full configured
Release build and the registered CTest run are reported in the commit that
carries this entry.

Not claimed. Nothing here is intelligibility: no listener has judged `ら`, `わ` or
`や`, and the resonance banks and transition lengths are experimental parameter
choices rather than phonetic qualification. Zero-crossing direction is
deliberately not asserted anywhere, because at these pitches it follows the
excitation's own harmonic spacing rather than the pose; the measurement used is
a spectral band profile, and the pilot regression only asserts that a glide's own
span moves rather than holding one pose, because a glide whose declaration spans
its whole gesture has no static part to compare against. Voiced affricates (`じ`, `ぢ`), pre-onset context
beyond the owning note, and coarticulation with a preceding phone inside the same
note remain unsupported and are refused rather than approximated, so the pilot
inventory is still known not to be generatable end to end. The pilot README says
so. No unit acceptance changes.

## Milestone status, September 14, 2026

This records where the implementation plan's M1 packages actually stand. It is
not a unit acceptance and it does not renumber anything.

M1.P2's ten required changes:

| # | Required change | State |
|---|---|---|
| 1 | Articulation-source description per phone class | Admitted: oral vowel, nasal, frication, voiced frication, released stop, voiced stop, unvoiced affricate, voiced affricate with a prevoiced closure and a voiced tail, approximant, palatalized consonants that borrow a base release, vowel-to-coda placement for ordinary consonants, gesture silence, a declared closure event that is exactly silent for its resolved span, and a declared breath event with its own source. A consonant-to-vowel boundary inside one note now carries a declared transition as well: it is a crossfade, because a voiceless consonant is not carried by the tract and so the tract is not sounding across it. Any event a recipe does not declare is still refused. |
| 2 | Phrase context beyond the owning note | Landed. A gesture may cross its own note boundary when the phrase accounts for it: a consonant that begins before its beat takes frames the previous vowel releases, and a coda may keep the opening frames of the next note. The plan is bounded by the phrase context and the partition rather than the note box, a placement the phrase does not account for is still refused as an overlap, and every note keeps a gesture of its own. |
| 3 | Ordered spans separated from a bounded transition plan | Landed. The gesture list is still an ordered non-overlapping partition and a separate transition list names every boundary that carries an acoustic overlap -- its span, its frame count, its mode and what each layer does across it -- and the renderer applies the declared mode instead of deriving a window. |
| 4 | Chunk-invariant rendering | Done, including the new gesture. |
| 5 | Versioned semantics for old resources | Done. Recipe schemas 1-11 and candidate schemas 1-11 are each read with their own meaning, the articulation plan is revision 13, and the transition model is revision 1. |
| 6 | Shared inventory generation planner | Landed (`inventory_generation`), consumed by the campaign. |
| 7 | Resumable campaign orchestration | Landed (`generation_campaign`), one bounded batch per advance. |
| 8 | Prepare-render-collect transaction with durable receipts | Landed, including conflicts on external edits and recovery of an uncertain commit. |
| 9 | Aggregate budget preflight | Landed: per-batch, aggregate frame and estimated-byte limits, retained-storage inspection and cancellation. |
| 10 | Held-out pilot phrase set before the full inventory | Landed, run and followed through: both preflights passed, and the campaigns behind them completed with 498 takes committed as unapproved marker-review material across three pitch layers (report in `CAMPAIGN_REPORT.md`, defect list in `coverage-report.json`). The fricative, palatalized, voiced-affricate and event repairs took coverage to 1026 of 1026 assignments, so a campaign over the whole inventory now plans as 1026 jobs and the rendered held-out preflight passes all 38 declared classes with none defective. The inventory is generatable end to end as declared; a prepared class is still not phonetic qualification. |

So all ten are landed. That is a package implementation statement, not a unit acceptance: the
acoustic result is unreviewed, and M1.P3 still has the real-recording journey and an independent
reviewer open. Its connected campaign/review path now runs inside one regression
(`seam_original_singer_campaign_workflow_tests`), so what is left there is the marker-edit and
rejection/retake route inside that same chain rather than a missing connection.

M2.P1 stands where its own entries leave it. Landed: the bounded process primitive and its single
implementation in platform, the dependency direction out of neural, the versioned deployment and
request/response metadata boundary, the data-only bundle and its manifest, and now operator-level
graph admission of the two graph files. Open: the production bundle-conditioned v2 launcher, the
child's own admission of the exact bytes it loads, a Windows process backend and any resource
ceiling that is a real bound rather than best-effort sampling, plug-in host supervision, and a real
learned acoustic/vocoder model. None of that is implied by admitting a graph's declared structure.

M1.P1's last open required change landed: the durable C++ legacy migration
operation with its retained receipt and history-transition verification, applied
through `migrate-style` against the Python planner's own plan. A generic save still
cannot upgrade a schema, which is deliberate: the migration is a distinct,
evidence-backed operation.

M1.P1's coverage-report requirement is satisfied: `inspect-generation-coverage`
retains a canonical per-class report of what a recipe can prepare, and the real pilot
inventory's result is committed at
`assets/pilots/seam-pilot-01/coverage-report.json`. It is what makes the next repair
choice evidence-based rather than a guess, and the inventory's own declared classes are now all
prepared: 1026 of 1026 assignments, every one of the 342 keys, 41 phones and 8 kinds, with nothing
refused. The coda, frication, palatalized, voiced-affricate and event refusals are gone, and the
campaign over the whole inventory plans as 1026 jobs and passes its rendered held-out preflight.

## An affricate is one gesture, not a stop followed by a fricative


The pilot's Japanese inventory names `ts`, `ch`, `j`, `r`, `w` and `y`, and the articulation
model could not produce any of them: `つ` and `ち` were refused with "has no explicit frication or
released-stop source", so two ordinary syllables of the pilot language had no path at all.

An affricate is now its own recipe pose and its own gesture. `VoiceRecipe::AffricatePose` (schema
seven) binds an explicit release spectrum, an explicit tail spectrum and how long the release
lasts; it is an opt-in field, so a recipe written before it keeps its previous meaning and a
phone is never reinterpreted as an affricate because a newer build would like it to be. The
loader admits schema seven without a voiced-feature flag, because the extension itself is
unvoiced. `ArticulationGestureKind::Affricate` carries the parts: a silent closure, the declared
release, and then frication until the vowel nucleus, with the split derived deterministically
from the note's own span (the tail keeps at least 20 ms and at least half of what remains after
the release; the closure takes the rest). A note with no room for a closure, a release and a tail
is refused with the millisecond requirement in the message rather than compressed, and a voiced
affricate is refused by name -- prevoiced closure plus voiced frication is a different model, and
an unvoiced noise pair is not substituted for it.

The aperiodic lane renders the two parts inside the one gesture, so they cannot overlap each
other, and its closure frames stay exactly silent while the release and the tail are not. A
chunked render of the same owned range from the same context is identical to a whole one, which
is what keeps the new gesture inside the existing chunk-invariance contract.

Candidate metadata gains schema seven, an `affricate` marker kind and an `affricateRevision`; the
plosive revision is recorded for affricate-only candidates too, because the release is a plosive
source. A schema-seven recipe also satisfies a schema-six candidate, since it carries everything
version six did, exactly as version six already satisfied version five.

Evidence: `tests/test_articulation_context.cpp`, registered as CTest
`seam_articulation_context_tests`, covers the gesture kind and split arithmetic, the short-note
refusal and its message, the voiced-affricate refusal, that frication, plosive and affricate stay
distinct gestures, the silence/release/tail rendering, and whole-versus-chunked equality. The
pilot's new `affricates` fixture renders `つ・ち・た・さ` and is checked by the existing CLI
regression for schema seven, the marker sequence, silent closure, nonzero release and tail, and
repeatability; run it with `build/release/seam_singer_pilot NEW_OUTPUT_DIRECTORY affricates`.

Not claimed. Nothing here is intelligibility: no listener has judged `つ` or `ち`, and the burst
and tail spectra are experimental parameter choices rather than phonetic qualification. Voiced
affricates (`じ`, `ぢ`), liquids and glides (`r`, `w`, `y`) and pre-onset context beyond the
owning note are still unsupported and are refused rather than approximated, so the pilot
inventory is known not to be generatable end to end; the pilot README says so. No unit acceptance
changes.

## The editor shows what its bounce is doing, including when it refuses

The plug-in surface never fed the editor's render status panel. Only the standalone application
did, so inside a DAW the panel showed its default state, the loop control -- which is enabled only
when audible audio exists -- was inert there, and a Follow Host bounce that refused for want of a
reported range left the creator with no visible reason at all.

`EditorRuntime::refreshRenderStatusView()` now projects the actual state into that panel: the
offline preparation when one has been attempted, and the ordinary render coordinator otherwise,
with the offline diagnostic preferred because it names what the host has not reported and what
would have to be recaptured. `renderStatusView()` exposes the result for readers. It is refreshed
when the controller is rebuilt, when a preview is published, when the timing authority changes,
and on every exit path of `prepareOfflineRender` through a scope guard, so refusals are shown and
not only successes.

`tests/test_host_timeline_capture.cpp` asserts both directions on a real editor runtime with the
production demo bank: after a Fixed Audio bounce the panel reports Ready with audible audio, and
after a Follow Host bounce that a silent host cannot authorize it reports Failed with the
uncovered span, the recapture instruction and no audible audio.

Not claimed. The panel reports; it does not stop a host from writing a file, which stays a
host-qualification concern in this package and M5.P2. No DAW was run, and the state shown is the
editor's view of SEAM's own render pipeline rather than any host's bounce result. No unit
acceptance changes.

## A creator can choose the bounce timing without leaving the editor

The choice became part of the project, and nothing in the interface could make it. A musician
working inside a DAW has no menu bar of ours, so the control belongs in the shared editor scene
rather than in a platform menu.

The toolbar now carries it beside the loop control: `BOUNCE: SCORE` when a final render is timed
against the score's own tempo map, `BOUNCE: HOST` when it follows the host's report. It is painted
in the shared scene, hit-tested with the same shared layout geometry, and exposed as the semantic
node `toolbar.bounce` with a role, a name and a value that state the current choice, so it is
reachable by pointer and by assistive technology through one geometry.

Two boundaries are deliberate. The control is only shown when the surface actually connects the
choice: `bounceTimingAvailable` is false wherever no callback is registered, so a surface that has
no host-timed bounce does not show a button that does nothing. And the control reports the
project's authority rather than the last thing clicked -- every controller rebuild re-reads it --
so a project reopened in Follow Host shows Follow Host, and a refused change leaves the control
where it was rather than displaying a state the project does not have. The CLAP editor wires the
callback to `setOfflineTimingAuthority`, which persists the choice into the document.

`tests/test_native_ui.cpp` covers the pointer path, the accessibility `Activate` path, the refusal
path (the callback fails, the control does not move) and the surface that never registered the
callback. Layout is asserted against the control it follows, so the new control cannot overlap the
loop control, and it inherits the existing guards that hide toolbar controls that no longer fit.

Not claimed. There is no keyboard shortcut for this choice yet and the standalone application menu
does not offer it, so today it is reachable by pointer and by assistive technology in the editor
surface that supports a host-timed bounce. No installed DAW bounce was run: the nine tuples and
the real host reporting cadence remain M5.P2, and no unit acceptance changes.

## A project says which timing its bounce follows

Follow Host worked in the runtime and could not be asked for. The authority was a session value
with no control, no host parameter and nothing in the saved document, so a musician could not
choose it at all and a project that had been bounced against the host's timing would have
reopened silently bouncing against its own map.

The choice now belongs to the project. `domain::BounceTimingAuthority` is a project setting with
`fixed-audio` and `follow-host` as its persisted names, and schema 11 writes it into the settings
block. A project written before the choice existed meant its own tempo map, so schema 10 and older
decode as Fixed Audio; a schema 11 document that omits the field, or names an authority this build
does not implement, is refused rather than quietly rendered as something else. Choosing an
authority in the editor runtime writes that setting, so the state a host stores carries the
decision, and reopening a project adopts it -- which is also why an imported document can no
longer inherit whatever authority the previous session happened to be using.

`tests/test_serialization.cpp` covers the round trip, the persisted spelling of both choices, the
schema 10 default and both refusals. `tests/test_host_timeline_capture.cpp` adds the editor-level
case: choosing Follow Host marks the project, the encoded state contains it, a reopened runtime
adopts it, a project that never made the choice stays Fixed Audio, and switching back is persisted
as clearly as switching away.

Not claimed. There is still no control in the native editor and no CLAP parameter, so the choice is
reachable only through the runtime API and the saved document; wiring a visible control is the
remaining half of this package. No installed DAW bounce was run, so this is persistence and
identity evidence, not host qualification -- the nine tuples remain M5.P2. Writing schema 11
changes the encoded bytes of every newly saved project, which is why it is a version bump rather
than a silent addition; older documents keep loading unchanged, and no unit acceptance changes.

## The host's transport reaches the editor without blocking the audio callback

The editor runtime could capture, freeze and validate a host tempo map, and nothing in the
product ever gave it one. The CLAP entry already read the host's transport on every block for
live note placement, but that value died with the callback; the September review of this area
warned specifically that calling a locking setter from the audio thread would damage normal
playback, so the fix could not simply hand the block's report to the runtime.

`seam/clap_editor/host_transport_publication.hpp` and `src/host_transport_publication.cpp` add
the handoff. The audio callback is the only writer and stores atomics only; the owner thread is
the only reader, and a seqlock sequence makes a snapshot that straddled a publish detectable
rather than usable. Nothing here allocates, locks or waits on either side, and a torn read is
counted and refused instead of being mixed into one report.

The decision whether a report is worth forwarding lives on the audio thread, because that is the
only place the previously forwarded report is known. The first report is forwarded, as is any
change of tempo, meter, loop state or playing state, and any position movement of at least half a
beat or a quarter second. A host that reports on every block therefore stops growing the acquired
map without adding information, and the quantum stays well inside the one-beat coverage tolerance
the frozen authority requires. `requestCallbackIfNeeded()` asks the host for a callback once per
undrained report, so a host that ignores the request is not asked again for the same one.

`plugin_entry.cpp` publishes in `process()` and drains on the main thread: the owner clears the
pending flag before draining, so a report published during the drain raises a fresh request
instead of being lost, and both `on_main_thread` and the GUI timer drain because a host is free to
deliver only one of them. Only the owner thread touches the runtime, through
`EditorRuntime::setHostTimelineState`, which records the report into the capture and -- under
Fixed Audio -- deliberately does not invalidate a document-timed bounce.

`tests/test_host_transport_publication.cpp`, registered as CTest
`seam_host_transport_publication_tests`, covers the forwarding rules at and around the quantum,
one-shot delivery per snapshot, one callback request per undrained report, and a two-thread case
that publishes 2000 reports whose tempo encodes their position, so a snapshot that mixed two
reports would be visible: every delivered snapshot was self-consistent, no torn read reached the
reader, and the final report was delivered.

Not claimed. Every transport in these tests is synthetic, and the bounce authority is still a
runtime setting with no persisted project or plugin value and no control a musician can reach, so
a real DAW user cannot yet choose Follow Host for a bounce; completing that choice is the next
step in this package. Real host reporting cadence, the nine required tuples and the offline-song
qualification remain M5.P2 work. No unit acceptance changes.

## A Follow Host bounce is prepared from a frozen host timeline

The tempo map from the previous change told the render what the host had said, and two things
were still wrong around it. There was no typed authority to inspect: the map was a private member,
the host's meter and loop state were never recorded at all, and "has this bounce gone stale?" was
answered by comparing a hash of everything the host had ever reported. That last question is the
one the host asks most often, because it reports on every audio block -- so a transport that was
simply advancing threw away a ready bounce.

`seam/clap_editor/prepared_host_timeline.hpp` and `src/prepared_host_timeline.cpp` add
`HostTimelineCapture` and the immutable `PreparedHostTimeline` it freezes. The capture accumulates
what the host reported -- beat positions, tempo, meter, loop boundaries and the sample rate it was
running at -- bounded at 4096 reports and 256 meter segments, and detects a backward jump as a
seek rather than pretending the sample relation stayed monotonic. Freezing a range produces the
authority one bounce rests on: project identity and revision, sample rate, project resolution,
project offset, the requested musical range, the tempo segments that determine that range
(including the bounding observations that make coverage decidable), the host's meter segments,
loop semantics, report and seek counts, and two identities -- one over the capture content, one
over the frozen preparation.

Validity is now judged on the range, not on the whole history. `HostTimelineCapture::describes()`
answers whether the host still says the same musical thing about the frozen range: every segment
the preparation depends on, no new tempo or meter inside the range, the same loop semantics and
the same sample rate. Advancing the transport and reporting tempos beyond the end of the score are
not changes; a tempo or meter change inside the range is. `EditorRuntime::setHostTimelineState`
acts on that answer, so a prepared bounce survives ordinary playback and is dropped -- with its
audio -- exactly when the authority it was rendered from changed. A Fixed Audio bounce is defined
by the document's own map, so host reports no longer invalidate it at all.

Preparation refuses rather than approximates. An empty or inverted range, an out-of-range sample
rate or offset, a non-positive gap tolerance, a sample-rate change during capture, meter segments
beyond capacity, a host that is looping without stating usable musical loop boundaries, and a
range the host never covered are each named failures; the coverage refusal carries the uncovered
span, how many tempo observations the host has actually reported, and what to recapture. The
frozen authority's identity enters the Follow Host timing identity alongside the substituted tempo
map, so a bounce cannot be attributed to a host map it was not rendered with.

`tests/test_host_timeline_capture.cpp`, registered as CTest `seam_host_timeline_capture_tests`,
covers the frozen range and its sample extent, refusal of an uncovered range and of an undeclared
gap, a loop that states no boundaries, a meter change, a seek that keeps the map while changing the
identity, a tempo change that rewrites it, a stopped host, a rate change during capture, and the
integrated case: a two-beat score whose document says 60 BPM renders 48000 frames under a 120 BPM
host and about twice that after switching back to Fixed Audio, while an in-range tempo change
drops the frozen authority and an out-of-range one does not.

Not claimed. Every host report in these tests is synthetic. A real DAW's reporting cadence, its
loop and meter semantics across a full bounce, and the nine required installed tuples remain M5.P2
work, and R13 is not closed by this. The live transport mapper still converts beats to seconds at
the instantaneous tempo for realtime note placement; the offline bounce no longer depends on that
approximation, but M5.P2 owns fixing the live path and its addressing semantics. No unit acceptance
changes.

## A Follow Host bounce is authorized by the host's own tempo history

Follow Host final rendering had an authority flag and no acquisition path: the final bounce
refused every request because nothing in the product ever learned more than the single BPM the
host happened to be reporting. One instantaneous tempo cannot certify a later tempo event, so
the refusal was correct and the missing half was the history.

`seam/clap_editor/host_tempo_map.hpp` and `src/host_tempo_map.cpp` add that history. The map
records what the host actually reported as `beats` plus `bpm`, keeps it in musical order rather
than arrival order, and is bounded at 4096 observations. Non-finite or out-of-range reports are
refused rather than clamped, re-reporting the same position with the same tempo is idempotent,
and the revision advances only when a tempo really changed. Its identity is a canonical hash of
the observed history, not of the revision counter, so two hosts that reported the same map
produce the same timing identity. `EditorRuntime::setHostTimelineState` records an observation
whenever the host supplies both a beat position and a tempo; the accessor returns a copy.

`prepareOfflineRender` now authorizes Follow Host against coverage instead of a single value. It
asks the map to cover beats `0..projectEndBeats` with an engineering tolerance of one unobserved
beat, and when the map cannot speak for that range it refuses with the exact uncovered span and
the number of observations it has. When the map does cover the score it becomes a render-only
project tempo map, the caller's own saved map is untouched, and both the substituted project and
the observed events enter the render identity, so two bounces cannot share an identity while
following different host tempo histories. The override is cleared when preparation returns, so a
later Fixed Audio bounce cannot inherit host timing.

Two defects surfaced only when the path was driven end to end, and both are fixed rather than
worked around. The render-only override was being cleared *before* the render was requested: the
guard was assigned from a temporary into `std::optional`, so the temporary's destructor cleared
the override in the same statement that installed it. A debug trace showed the override stored
and then cleared microseconds before `requestPreview`. The guard is now constructed in place and
its copy and move operations are deleted, which is what makes the early clear impossible rather
than merely absent. The second defect was that the offline session still held the identity
computed *before* the host map was built, so the session rejected its own later failure and
publication as stale and left the host reading "preparation is pending". Preparation now
re-begins the session with the identity that will actually be rendered.

`tests/test_host_tempo_map.cpp`, registered as CTest `seam_host_tempo_map_tests`, covers the
versioned history, coverage at both ends and across a bounded gap, identity stability under
re-reporting, refusal of an uncovered score and of a single observation, and the audible case: a
two-beat region renders 48000 frames under a 120 BPM host, roughly twice that under a 60 BPM
host, with a different timing identity for each.

Not claimed. The one-beat coverage tolerance is a declared engineering default, not a measured
threshold, and a real DAW's tempo reporting cadence has to be exercised in the installed-host
qualification that M5 still owes. The audio is the production demo bank fixture, so this is
timing, identity and routing evidence, not a singer. No unit acceptance changes.

## The render path is verified with the shipped neural worker, not the probe

Every neural render test until now executed the transport probe, which returns silence
and performs no inference. That proved routing, publication identity and caching, and
it proved nothing about the worker the product ships. The production worker had its
own checks, but they drove it directly from Python; nothing connected "this worker
admits a bundle and executes ONNX" to "the authoring render path publishes what the
worker produced".

`tests/test_neural_production_render.cpp` closes that seam. A new CTest,
`seam_neural_production_render`, prepares a real ONNX bundle with the CLI, asks the
test binary for the phones one deterministic phrase actually needs, builds the
vocabulary from that answer, and then renders the phrase with
`AuthoringNeuralPhraseRunner` pointed at `seam_neural_worker`. The check passes only
when the published render names `seam.neural-worker.v1`, reports the admitted bundle
identity and provider, and contains finite, in-range, non-silent audio. Observed
locally: 96000 interleaved samples rendered through the shipped worker with every
sample nonzero.

Two details are deliberate. The test is driven in two phases through environment
variables, because the phrase's phones come from the C++ phonemizer while the bundle's
ONNX bytes come from the Python tooling; handing the vocabulary across that boundary in
the wrong direction would let a test pass against a bundle that cannot sing the phrase.
And the render must report its own outcome on stdout, which the driver asserts on, so a
phase that silently did nothing cannot be mistaken for a completed render.

Not claimed. The bundle carries arithmetic fixture graphs, so this is execution and
routing evidence, not a singer. It also does not change the workflow suite's probe use:
those tests remain the ONNX-free coverage of routing, and both the transport-probe and
production-worker paths are now exercised.

## A neural candidate can be qualified without being approved

M2.P3 lists seven commands against tools/voice_model_training: admit, prepare,
label-report, split, train, export and qualify-candidate. The first four and the export
side already exist in the tracked tree -- the roadmap note that called this directory
absent is stale, and I am correcting it here rather than repeating it.
qualify-candidate did not exist anywhere.

It exists now as the qualify-candidate subcommand of python -m
tools.voice_model_training, in the house style of the other commands: a captured
configuration with its own SHA-256, bounded inputs, and no overwrite of an existing
output. The configuration names an admitted bundle by directory and manifest identity
plus 1..256 held-out items with their phones, frame counts, target F0 and gain, and 2..5
repetitions. The command drives the production worker over every item and records each
automatic criterion separately: bundle admission, response binding to the exact request
bytes, vocabulary coverage of the held-out phones, determinism across repeated
identical requests, finite non-silent audio, and runtime when a per-item budget is
declared.

What makes it useful is what it refuses to say. Intelligibility, identity and
musicality are always UNRESOLVED, because they need independent listeners; the verdict
is FAILED when an automatic criterion fails and UNRESOLVED otherwise, so the command
has no way to print QUALIFIED, always sets releaseEligible false and records no
approval. A failing run still writes its dossier and exits 4, which is what keeps a
rejected candidate auditable instead of silent. Exit 0 means that nothing automatic
failed; it does not mean approved.

Verification. tools/voice_model_training/test_qualification.py covers the policy and
the command with an injected runner: identical runs pass; differing audio across
repetitions fails only determinism; silence, nonfinite samples and a wrong response
length fail audio or binding; a worker exit status and a budget overrun are attributed
to their own criterion; a held-out phone outside the vocabulary fails coverage; a
passing dossier is still UNRESOLVED with no approval and no release eligibility; and
the command refuses a bundle manifest that differs from its capture and refuses to
overwrite an existing dossier. tools/neural_runtime/check_candidate_qualification.py
runs as the new CTest seam_neural_candidate_qualification: it prepares a real ONNX
bundle with the CLI, drives the actual production worker through the command, and
asserts that every automatic criterion passes while the verdict stays UNRESOLVED, that
a missing phone fails coverage with exit 4, and that a closed output is refused.

Not claimed. The fixture graphs are arithmetic constants, so this qualifies the
machinery and not a voice: no held-out singing was produced and no listening happened.
The train command in the contract is still documented as an upstream invocation rather
than a first-party subcommand, and the honest remaining M2 work is a lawful source set,
a real training or adaptation run, and a vocoder export for the same candidate.

## One channel at a time can be regenerated and accepted

M3.P3 item 3 asks for "full and selected-range/channel regeneration", and AE3 asks that
accepting a take preserve locked channels. The range half landed last; the channel half
did not exist in any form. A proposal always carried all four channels the backend
generates, and accepting one always accepted all four, so a creator who liked a take's
pitch but not its dynamics had no way to say so.

Both paths are now channel-scoped. `proposeAutomaticPerformance(scope, channels)`
generates only the requested lanes, and `acceptPerformanceTake(take, scope,
channels)` selects only those channels of that take. The surface learns what it may
offer from `performanceTakes()`, which now reports the channel ids each take actually
carries; the macOS take submenu builds `Accept Channels` from exactly that list. The
dispatcher refuses a channel the take does not carry with `Conflict` and an
unrecognised id with `InvalidArgument` rather than quietly widening the decision to
every channel, because a widened acceptance would claim audio the backend never
generated. Regeneration additionally refuses a channel the production backend cannot
generate (anything outside pitch, dynamics, attack and release) with `Unsupported`,
so the surface never believes it asked for something it did not get. Channel lists are
deduplicated, and an empty list keeps the old meaning: every channel the take carries,
or the backend's full supported set when proposing.

The File menu gains a `Regenerate Selected Notes (Channel)` submenu over those four
channels. It always proposes over the current note selection and therefore refuses when
there is none, which is the repair workflow the exit criterion describes: keep what
sounds right, regenerate the channel that does not.

Verification. `tests/test_standalone_project_lifecycle.cpp` adds the case: a
dynamics-only regeneration produces a proposal with one lane while the earlier
four-channel proposal is untouched; the reported take channels match exactly; accepting
that take for dynamics selects one selection on that channel; a channel the take does
not carry is `Conflict`; an unknown id is `InvalidArgument` for both accepting and
proposing; and a channel the backend cannot generate is `Unsupported`.
`tests/test_file_dialog_contract.cpp` extends the surface default case so a dispatcher
that implements nothing also refuses a channel-scoped proposal and decision.

Not claimed. Channel scope is per channel, not per combination: there is no control for
"pitch and attack but not dynamics", and the comparison path accepts a channel list
without a surface control that uses it yet. Locked channels are still honoured by the
per-frame ownership rule rather than by the accept path removing them, so accepting a
take for a channel the creator has locked produces a selection that the compiler
supersedes rather than a refusal. No listening or musical judgement is claimed.

## A proposal can cover an unlocked span instead of the whole region

M3.P3 item 3 asks for full and selected-range regeneration, and the exit criterion ends
with "regenerate an unlocked range". Until now every proposal covered the whole
selected region, so repairing one phrase meant re-proposing material that already
sounded right and then deciding again about all of it.

The File menu now offers `Propose Automatic Performance Over Selected Notes` beside
the whole-region command. Both go through one capture owner; the difference is the
range it captures, which is the span the creator has selected rather than the region's
full duration. The action is refused with `Conflict` when no note in the region is
selected, and when the selection reaches outside the region, so a surface cannot
regenerate a span that does not exist. `PerformanceEditScope` now names both scopes
for proposing and deciding, which also removed a duplicated selection-span
implementation: the accept path and the propose path resolve the creator's selection
through one helper.

Verification. `tests/test_standalone_project_lifecycle.cpp` adds the case: the
whole-region command still captures the region's full duration; selecting the second
note and regenerating captures exactly that note's span and leaves the first proposal
and every accepted selection untouched; and with no selection the command is refused
and publishes nothing.

Not claimed. The proposal still carries the four channels the production backend
generates, so a channel-scoped regenerate remains open, and the caller cannot yet name
an arbitrary tick range -- the scope is the creator's note selection. Regenerating an
unlocked range also still means "decide again about the take covering that range";
nothing here merges two proposals automatically.

## Generated timing now lands through the ordered timing solver

M3.P3 item 4 asked for generated timing to be resolved through the ordered timing
solver before audio exists, so that generated F0, manual offsets and manual vibrato
compose once. The generated *pitch* half of that already worked; timing did not. The
compiler accepted only pitch, dynamics, attack and release, so a proposal carrying a
timing lane could not be rendered at all, and there was no other consumer for the
microsecond offsets it carries.

A timing proposal is now read where the syllable would otherwise start and displaces
that syllable's nucleus through the same plan the authored offsets use. The rules are
explicit because the two kinds of offset mean different things: an authored offset is
an absolute position relative to the note start and always wins, while a generated
offset is a displacement from the score position with a neutral zero. Manual
replacement on the timing channel suppresses the generated value exactly as it does
for pitch, so a locked syllable is never moved by a proposal. Because the displacement
is applied before anything is placed, ordered nuclei, automatic ends and the release
inside the region are all evaluated against the moved anchors -- a proposal that would
reorder two nuclei or push a syllable before the output timeline is refused with
`Conflict` instead of being clamped. A procedural onset gesture now starts where its
syllable's nucleus actually resolved, so a displaced syllable moves its consonant with
its vowel instead of stretching against a fixed score boundary.

Lane sampling moved into the domain as `samplePerformanceLane`, because the
per-frame evaluator and the timing plan must agree on what a lane value is at a tick.
It keeps the existing rule that a null point is never bridged: a voicing transition is
discrete, and interpolating across it would fabricate pitch.

Verification. `tests/test_phoneme_timing.cpp` adds the case: a 30 ms proposal moves
both nuclei by exactly 1440 frames at 48 kHz with the automatic end following the
moved nucleus; manual replacement restores the score timing; an authored offset stays
absolute for its own token while the proposal still displaces the other syllable; and
a proposal that would reorder the nuclei is refused. `tests/test_performance_compiler.cpp`
adds the guard case: an admitted timing selection never becomes amplitude, and the
per-frame evaluation still reports neutral gain.

Not claimed. Item 4 is not complete. A generated timing proposal moves phoneme
anchors, not the compiled note span the pitch and amplitude envelopes use, so a
displaced syllable currently sounds with its gesture moved while its envelope stays on
the score grid. Reconciling the two needs the note spans themselves to become
proposal-aware, which is a larger change to voice allocation and articulation windows.
Until then the boundary is enforced rather than assumed: a generated displacement that
would place a syllable outside the note span the score defines is refused with
`Conflict`, so no proposal can ask for a gesture that no attack, release or vibrato
phase could sound. A thirty-millisecond proposal inside the note still lands exactly,
and the note-span bound is covered by the same test case.
The production backend still emits no timing lane at all, so this is the path a
timing-predicting backend will land on, not evidence that a useful timing proposal
exists yet. No listening or musical judgement is claimed.

## Two takes can be compared from one playhead

M3.P3 item 5 asks for alternate-take audition, accept/reject, comparison with the
previous accepted take, and persistence. Decisions existed; comparison did not. The
previous slice's revision fix is what makes this possible at all -- before it, holding
two proposals at one musical revision was impossible once either was accepted.

The comparison is a held pair of selection states rather than a second audio path.
`beginPerformanceComparison(take, scope)` applies the candidate through the same
merge rule as accepting it, and keeps the region's previous accepted selections
alongside the exact list the merge produced. `swapPerformanceComparison()` applies
the other side with the replace rule, so a swap restores the state the creator
actually heard rather than a recomputed guess; `endPerformanceComparison()` releases
the held state and keeps whichever side is sounding, because ending a comparison is a
choice, not a revert. Every swap is an ordinary undoable edit, so a creator can undo
out of a comparison without leaving a half-applied state behind.

The creator hears both sides through the normal preview transport at one playhead:
the applied side is what the renderer publishes, and swapping re-renders it. This is
matched playback position by construction -- there is no second buffer, no second
device and no resampling step that could drift against the live timeline. Comparison
state is session state: it names a take and two selection lists, the project remains
the only owner of what is applied, and the state is dropped when the document is
replaced.

The macOS menu grows `Compare This Take` in each proposal's submenu, plus `Swap` and
`End Comparison` items that show which side is applied and are disabled when no
comparison is held.

Verification. `tests/test_standalone_project_lifecycle.cpp` adds the comparison case:
two proposals exist, the first is accepted, comparing the second applies it while the
previous state is held and every candidate selection names the compared take; a swap
restores the exact previous list and a second swap restores the candidate; undo and
redo move between the same two states; ending keeps the applied side and refuses a
further swap with `Conflict`; an unknown take is `NotFound`, an already accepted
candidate and a nested comparison are both `Conflict`.
`tests/test_file_dialog_contract.cpp` extends the surface default case: a dispatcher
that implements nothing offers no comparison and refuses all three calls.

Not claimed. There is no instantaneous A/B crossfade, no automatic pass-by-pass
alternation, and no per-channel comparison: the creator swaps explicitly, and each
swap re-renders through the ordinary preview path, so the gap between sides is a
render latency rather than a sample-accurate cut. Comparison also cannot outlive an
edit that moves the material: a swap after the score changed is refused by the same
revision rule that protects acceptance.

## A decision keeps what it does not cover, and no longer invalidates its siblings

Three connected defects sat between the decision commands and a usable alternate-take
workflow.

**Acceptance advanced the ownership revision, so siblings went stale.** Accepting one
proposal incremented `revision.ownership`, and take currency is whole-revision
equality. The moment a creator accepted proposal A, every other proposal B captured
for the same material was refused with "generated from a different musical,
pronunciation or ownership revision" -- exactly the comparison the alternate-take
requirement asks for. That axis belongs to manual ownership: it says the creator
locked a channel, which genuinely invalidates a take made before the lock. Deciding a
take is not a manual ownership edit, so acceptance no longer advances it. Manual
ownership edits, lyric and pronunciation changes, and score edits all still refuse a
stale take, and a render is still invalidated on acceptance because the snapshot
identity hashes the phrase project, which contains the accepted selections.
This supersedes the 2026-09-06 U6 status note that recorded the increment.

**A decision replaced the whole selection state.** Selecting three notes and accepting
a take discarded what had already been accepted on the others. `SetAcceptedPerformanceCommand`
now takes `PerformanceAcceptanceMode`: `Replace` keeps its old meaning for callers
that mean it, and `Merge` retains every existing selection the decision does not
cover while each new selection replaces only what meets it on the same channel. The
native decision path merges, so accepting a take on note 2 leaves note 1's accepted
material and manual ownership untouched. Composition needs each selection's interval,
including note-scoped ones, which are resolved against the region's current notes
rather than against a stale identity.

**An unknown take was dereferenced before it was checked.** The acceptance loop found
a take with `find_if` and immediately read `take->capturedRevision` without testing
for `end()`. The existing refusal test passed only because comparing garbage against
the revision happened to fail. A missing take is now `NotFound` with the take identity,
and the state is untouched.

Verification. `tests/test_automatic_performance.cpp` adds the merge case: a decision on
one note and one channel survives a later decision on another note and channel; the
same note on the same channel is replaced rather than duplicated; the merged state
validates; and one undo restores exactly the previous selection state. The existing
selection case in `tests/test_performance_commands.cpp` now asserts that acceptance
leaves the revision untouched. `tests/test_standalone_project_lifecycle.cpp` extends
the selected-notes case: after a second note and a second proposal, accepting on the
new note keeps both decisions, four selections on each note under their own take
identities.

Not claimed. A take still cannot be accepted for a channel subset, and there is still
no audition or comparison at matched playback position, so M3.P3 items 3 and 5 remain
open. The merge rule is deliberately span-local: it does not attempt to order several
takes over the same channel, it simply refuses overlapping claims by replacing them.

## A decision can cover the selected notes instead of the whole take

Deciding a proposal was all-or-nothing: a surface could accept the take over the
span the backend generated, or reject it. M3.P3 item 3 asks for selected-range
regeneration, and accepting a whole phrase when a creator wants to keep three notes
of it is exactly the manual work the feature exists to remove.

`PerformanceTakeScope` now separates `WholeTake` from `SelectedNotes`. A
selected-notes decision resolves the creator's current note selection inside the
region, spans it from the earliest selected note to the latest, and refuses a
selection that is empty or that reaches outside the span the take was generated for.
The range is never clamped: a span the backend did not produce is refused together
with the take identity, so a surface cannot claim generated data that does not
exist. Both scopes keep the zero source offset the previous slice established, which
is what keeps the take-to-region mapping honest.

The macOS submenu gains `Accept Over Selected Notes` beside the whole-take items, so
the decision is reachable without a new dialog.

Verification. `tests/test_standalone_project_lifecycle.cpp` adds the case: an
unselected region is refused with `Conflict`; selecting the note and accepting with
`SelectedNotes` produces one accepted selection per channel the take carries, each
covering exactly that note's span with a zero offset under the chosen take identity.

Not claimed. This is a range decision, not a channel decision: the surface still
accepts every channel a take carries at once. Decisions also replace the region's
accepted selections rather than merging with them, and there is still no audition or
comparison at matched playback position. M3.P3 items 3 and 5 therefore remain open.

## The product can decide a performance take, not only propose one

A proposal nothing can accept is an expensive way to write a file. The menu could
propose an automatic performance, but no reachable path could accept or reject one:
every decision would have had to go through an interface no creator can open.

`IApplicationCommandDispatcher` now carries the decision. `performanceTakes()`
lists the proposals recorded for the selected region that still await a decision,
and `acceptPerformanceTake()` / `rejectPerformanceTake()` decide one by identity.
The defaults stay honest -- an empty list and an `Unsupported` refusal -- so a
surface that cannot record a decision shows nothing instead of a menu whose choices
would quietly do nothing.

A take is named from its own record: generator, version, seed, tick span and the
channels it carries, never from its position in a list. Reordering proposals
therefore cannot make a creator accept material they did not pick. Accepting means
selecting the take over the span it was generated for, on every channel it carries,
with a zero source offset -- the same mapping the previous slice proved for partial
ranges. Rejecting goes through the command that records the decision on the take
itself. A rejected proposal leaves the list; an accepted one stays listed with its
state so the menu can show the current choice.

The macOS menu adds a `Performance Take` submenu: one entry per proposal, each with
its own `Accept This Take` and `Reject This Take` items carrying the take identity,
and the accepted entry ticked and disabled.

Verification. `tests/test_standalone_project_lifecycle.cpp` adds the controller
case: two proposals are listed with distinct labels and neither is accepted;
accepting the first selects every channel it carries with a zero offset and leaves
the second proposal Proposed; undo restores the region without a choice; rejecting
the second records the decision, keeps both takes and drops the rejected take from
the list; a repeated identity is `Conflict` and an unknown one is `NotFound`; and
the rejected take plus the accepted selection survive a project JSON encode/decode
round trip, so the audit trail outlives the session.
`tests/test_file_dialog_contract.cpp` adds the surface default case: a dispatcher
that implements nothing answers with an empty list and a refusal.

Not claimed. This is reachability, not selection quality. A creator can decide a
take but still cannot audition or compare two of them at matched playback position,
and the surface decision is whole-take: selecting a sub-range or a channel subset
from the native product is not implemented. M3.P3 items 3 and 5 therefore remain
open, and whether deciding takes saves creator work is still the M6.P2 study.

## A performance proposal can now be decided, not only created

A proposal that cannot be rejected is not a decision, and the previous slice could
only add one. `RejectPerformanceProposalCommand` records the decision on the take
itself, so a rejected proposal keeps its identity, generator, seed and captured
revision instead of disappearing. The refusals are the substance: an unknown take is
`NotFound`, a take that is not still `Proposed` is `Conflict`, and a take that an
accepted selection still references is `Conflict` rather than a silent dead
reference. That last rule is not cosmetic -- `RegionPerformanceState::validate`
skips rejected takes while it walks accepted selections, so rejecting a selected
take would otherwise leave an accepted selection that occupies the selection list
and replaces nothing. The decision moves no revision axis, so proposals captured
before the rejection stay acceptable and a creator cannot invalidate a pending
proposal by accident.

Partial-range acceptance is the other half of the same requirement, and it already
had a mechanism without proof: an accepted selection carries `sourceTickOffset`,
which maps the selected musical range onto the ticks of the captured take
(`selection.sourceTickOffset + range.startTick`). The regression accepts the middle
of a region from a take that covers all of it, then proves the refusal that keeps
the mechanism honest -- shifting the same selection past the captured span is
refused by `validate()` as an unmapped span instead of being silently clamped.

Verification. `tests/test_automatic_performance.cpp` adds three cases: rejection
keeps the take, leaves acceptances alone, leaves the revision axes untouched, and
survives undo and redo; rejection refuses an unknown take, an accepted take and a
repeated decision, including an expectation captured before an interleaved edit;
and partial acceptance selects a sub-range, validates, and refuses a shifted
unmapped selection.

Not claimed. This is the decision half of M3.P3 item 3. Alternate-take audition and
comparison, regeneration of a rejected or partial result, and the native accept and
reject surfaces are still missing: the menu can propose, but nothing in the running
application can yet accept or reject what it proposed.

## Automatic performance is a real proposal, not a placeholder

The only automatic-performance backend was the deterministic reference one: pitch
was each note's written pitch plus up to four cents of seeded jitter, dynamics
copied the region's automation, and attack and release were two constants. Nothing
in the product called it at all, so the capability existed only as a lifecycle
fixture.

`generatePhraseAwarePerformance` is the production backend, identified as
`seam-phrase-proposal` version 1. It derives shaping from the compiled score rather
than from noise. Phrases are runs of notes with no rest between them; the pitch lane
arcs toward the phrase centre, opens with an onset scoop when a phrase begins on a
consonant and the phonemizer reports that symbol as an onset, and settles the last
note of a phrase exactly on its written pitch. The dynamics lane modulates the
region's own automation instead of replacing it -- a phrase arch, a phrase-start
accent, an accent after a leap of five semitones or more, and a damping on the final
note, all clamped into the channel unit range. Attack follows articulation (legato
or slurred long, staccato short, phrase-initial short) and release follows phrase
position, articulation and a stop coda in the resolved pronunciation. Seeded
humanization is deliberately tiny: a different seed cannot be the substance.

The backend stamps its own identity and refuses a request naming a different
generator or an unknown version of itself, so a take can never be mislabelled for
later invalidation or cache keys. Request validation is now shared with the
reference backend, so both enforce one contract. The authoring-runtime capture owner
supplies the product path: it captures the performance job, the region and its
revision, then runs generation from the captured project -- safe to call off the UI
thread -- and adopts the result through the shared proposal command. A session that
moved on, a take that belongs to another capture, and a second adoption through the
same capture are each refused.

The action is reachable: `ProposeAutomaticPerformance` is a dispatcher command with
a File-menu item, and the controller resolves the selected region, the singer
identity the proposal is recorded against (the track's saved neural selection, or
the resolved voicebank), a fixed seed and a per-run take identity before running it.

Verification. `tests/test_automatic_performance.cpp` adds four backend cases:
identity and proposal state; the arch, scoop and resolution invariants; articulation
and partial-range behaviour with every point inside the requested range; determinism
per seed; and the refusals for a foreign generator, an unknown version, a stale
revision, an unsupported channel and cancellation. It also adds a capture case that
adopts a proposal into a real session, leaves accepted selections untouched,
survives undo and redo, and refuses both a reused capture and a moved session.
`seam_u2_tests` adds the controller case: dispatching the menu command leaves one
Proposed take with the backend's identity and no accepted selection, a second
dispatch is a distinct proposal, and undo removes it.

Not claimed. The proposal defaults are named constants, not measured thresholds, and
nothing here is a listening or quality judgement: whether this saves a creator work
is the M6.P2 counterbalanced study. Alternate-take audition and comparison, partial
range and per-channel regeneration from the native surface, and the comparison UI
that M3.P3 items 3 and 5 require are still missing; today one command proposes over
the whole selected region.

## The neural singer chooser is reachable from the application menu

Selection existed on the controller but nothing in the running application called
it, so the capability was still unreachable for a user. The command dispatcher now
carries the chooser: `neuralResources()` returns the installed singers a surface
can run and `selectNeuralResource(id, version, contentHash)` records one, with
`clearNeuralResource()` for the empty choice. The defaults are deliberately honest —
an empty list and an `Unsupported` refusal — so a platform that implements none of
this shows nothing to choose instead of offering bundles it cannot execute, and no
existing dispatcher had to change to keep compiling.

The macOS menu adds a `Neural Singer` submenu beside `Voicebank`, built the same way:
the dispatcher's list becomes items carrying their identity in `representedObject`,
the selected singer is ticked, and the first item clears the selection. It is
rebuilt from `IApplicationMenu::refresh()`, which the native application already
calls from its state-changed hook, and the controller notifies state changes after
selecting or clearing, so the tick follows the project. The Windows menu is an empty
stub today — it installs a dispatcher and renders no dynamic menus at all — so the
voicebank list has no Windows equivalent to mirror yet and the new defaults are what
that platform gets.

Verification. `tests/test_file_dialog_contract.cpp` adds the dispatcher contract
case: a dispatcher that implements nothing answers with an empty list and refuses
both selection and clearing. The controller side is already covered by the fixture
case in `tests/test_neural_selection.cpp`, which lists, refuses, selects, undoes and
clears against a real installed bundle. The AppKit submenu itself is
compile-verified only.

Not claimed. This checkout has no GUI test harness enabled
(`SEAM_RUN_NATIVE_GUI_TESTS` is off), so the menu's rendering and its click path were
not exercised; no screenshot, accessibility observation or AppKit run backs it. No
surface yet supplies a neural deployment, so a real run shows the empty list until a
signed deployment exists. No model, no listening result.

## A neural singer can be chosen, listed and cleared

The project schema has stored `neuralResource` for a while, the encoder and
decoder round-trip it, and the renderer compares a prepared bundle against it —
but nothing could *set* it. The controller had no way to choose an installed
neural singer, so the selection the previous change made renderable could only
appear in a project a test wrote by hand.

`application::SetTrackNeuralResourceCommand` now mirrors the procedural-recipe
command: it records only an identity a surface already resolved, refuses to apply
or undo over a changed selection, validates a replacement before swapping it, and
reports project-audio impact scoped to its track. Resolution and bundle admission
stay where they belong, in the surface's own deployment and the installed-resource
index.

The standalone controller exposes the chooser the native surface needs:
`neuralResources()` lists what this installation can actually run — in registry
order, from the same verified index the renderer resolves through, and empty when
the surface ships no verified deployment rather than listing bundles nothing could
execute. `selectNeuralResource(id, version, contentHash)` resolves the identity
through that index *before* editing, so a selection this installation cannot admit
is never saved into a project, then executes the command and refreshes the
document and browser. `clearNeuralResource()` records the same edit with no
replacement. `platform::NeuralResourceMenuItem` carries the identity, a display
name and the selected flag, matching the shape of the existing voicebank menu
item.

Verification. `tests/test_performance_commands.cpp` adds a command case: a stale
chooser result is refused without touching the revision, a reference whose kind is
not neural is rejected, clearing and undo restore exactly the previous project, and
an unknown track is a clean failure. `tests/test_neural_selection.cpp` adds a
controller case that runs against a real installed bundle fixture: the list shows
the installed singer as unselected, an identity the index cannot resolve is
refused *before* the project changes, selection records the exact identity, undo
restores the cleared state, and clearing works through the public surface.

Not claimed. There is still no menu entry, so a user cannot yet reach selection
from the running application: the dispatcher and native menu list are the next
step, and the Windows menu implementation cannot be compiled or verified on this
machine. No real deployment is signed, no model exists, and none of this is a
listening result.

## The application selects its own neural helper

`TrackNeuralSource` was already consumed by the renderer, the render cache and the
export owner, but nothing outside tests ever constructed one: the deployment
descriptor was read only by tests and the packaging writer, and no product surface
consulted its own signed deployment. A project could therefore save a neural
selection that the product could not run, with no surface even looking for the
helper it ships.

`libs/seam-authoring-runtime` now owns `NeuralSelectionService`. One service is
created per surface. It validates the surface's declared budgets and provenance,
reads the descriptor with the same 16 KiB bound the verifier enforces, verifies the
signature against the surface's release key under the bundle launch contract
(schema 2 only), and loads the helper through the loaded-module anchor, so a bank
cannot redirect execution even if it could name a path. The package supplies the
helper path and its expected digest; the surface supplies the process budgets,
because the package format deliberately carries none; the surface also declares
the worker/runtime/provider provenance that is hashed into the render cache
identity and published. Nothing comes from a project or a voicebank.

`select()` then resolves one saved `NeuralResourceReference` through the installed
resource index, freezes and admits the bundle, cross-checks the admitted execution
identity against the saved identity, and creates the runner. It refuses an unknown
or relabelled identity, a duplicated resource root, a bundle whose configuration is
not executable (schema 1 declares no steps layout and no vocoder output name), a
cancelled request, and a non-zero anchor or descriptor that fails verification. A
saved selection this installation cannot resolve is a failure; another voice is
never substituted for it.

The standalone surface is wired to it. `StandaloneApplicationControllerConfig`
gains the optional deployment surface and an installed-resource root, so a build
that ships a signed helper selects it and a build that ships none behaves exactly
as before. Construction verifies the deployment and indexes the resource root,
which means a surface whose declared helper cannot be verified refuses to start
instead of failing later on the first note. Both export paths (`makeExportRequest`
and `exportAudio`) resolve a neural track before any bank source for that track and
fail the export with the selection error rather than exporting a different singer.

Verification: `tests/test_neural_selection.cpp` adds three cases to the new
`seam_neural_selection_tests` target — the full guarded selection path with the
refusals for a stranger's signature, a schema-1 deployment, a mismatched target or
platform, unmeasured budgets, a missing anchor and a cancelled token; the
non-executable bundle refusal; and a resource root whose duplicate identity is
ambiguous. `seam_u2_tests` adds one controller case: a surface whose deployment is
signed by nobody is refused at construction, while a controller without a neural
deployment still starts. The executing helper in these tests is the transport
fixture probe, which performs no inference.

Not claimed. No real deployment is signed and none can be: the repository's trust
roots are still marked `testOnly`, so a production surface has no release key to
verify with yet. No surface can yet *choose* a neural resource — the project schema
stores the reference, but no command or editor action sets it, and no preview or
stop/retry surface exists. No model was trained or executed, and no listening
result follows from any of this. M6 still owns the audible exit.

## The shipped helper no longer links the build machine's Protobuf

`build/release/seam_neural_worker` recorded 81 absolute `/opt/homebrew` load
paths (78 Abseil, 2 Protobuf, 1 OpenSSL), so the packaging owner refused the only
helper SEAM can actually ship. The development SDK publishes Protobuf and Abseil
as shared libraries only, and a payload that links them can never resolve on a
user's machine.

`tools/neural_runtime/build_static_protobuf.py` now builds the pinned Protobuf
33.4 release with its vendored Abseil 20250512.1 as static archives, verifies that
the install tree contains no shared library at all, and links a probe
(`static_protobuf_probe.cpp`) that reads and re-parses a `FileDescriptorProto`.
The repository's own closure reader then proves the probe resolves every
non-system reference from beside itself. A distribution build points at that
prefix with `-DSEAM_STATIC_PROTOBUF_ROOT` and at the pinned code generator with
`-DSEAM_PROTOC_EXECUTABLE`, because the runtime-only archive deliberately ships no
`protoc`; `tools/phase13a/static_protobuf.py` is the payload-facing entry point.

Three defects were found and fixed while making this work, and the first one is
worth recording because it fails in a way that looks like a broken archive:

* Abseil installs an ABI-pinned `absl/base/options.h` derived from the C++ standard
  known at configure time, while its own sources compile from the unpinned header.
  Leaving the standard to the compiler default pinned `ABSL_OPTION_USE_STD_STRING_VIEW`
  to 0 (Abseil's own `string_view` class) while the archives were built against
  `std::string_view`, so every Protobuf reference to `absl::string_view`, `CEscape`,
  `ByChar::Find` and friends was unresolved even though the archive defined those
  exact symbols. Linker flags cannot repair an ABI disagreement; both builds now fix
  `CMAKE_CXX_STANDARD=17`, and `verify_static_install()` re-reads the installed
  header and refuses a pin that disagrees with the archives.
* The probe's flattened Abseil link list was previously diagnosed as mis-ordered.
  It was not: the same undefines survived `-Wl,-all_load`, which is what pointed at
  a symbol-level ABI mismatch rather than a link-order problem.
* The builder is importable rather than only a CLI, and `--probe-only` is hermetic:
  it re-verifies an installed prefix and the probe without the pinned tarballs, the
  network or a code generator, so a configured build can re-check the SDK cheaply.

Evidence. Configuring a distribution build with the static Protobuf/Abseil SDK, the
pinned static OpenSSL 3.5.7 (`libcrypto.a`, commit
`8cf17aaeb4599f8af87fefd810b5b5fee90fe69e`) and the 1.30.0 ONNX Runtime takes
`seam_neural_worker` from **81 host load paths to zero**: `otool -L` lists only the
ONNX runtime (`@rpath`) and operating-system libraries/frameworks, and
`derive_runtime_closure()` reports **1 entry, 0 unresolved** — the one entry being
the ONNX runtime the payload ships beside the helper. The staging invariant that
previously exercised only its refusal branch now stages the real worker, which is
the end-to-end statement that matters: the helper a payload would ship is one the
packaging owner accepts.

Verification. `tests/phase13a/test_static_protobuf.py` adds 13 hermetic cases
(exact pins and digests, the pin parser, the refusals for a shared library, an ABI
pin mismatch, a missing archive, a missing CMake package and a missing prefix, and
the payload entry point's placement). CTest gains
`seam_static_protobuf_contract_tests` unconditionally and `seam_static_protobuf_closure`
whenever `SEAM_STATIC_PROTOBUF_ROOT` is set; both pass in this checkout, and the
closure test passes in a build configured with the static SDK. `THIRD_PARTY_NOTICES.md`
now records the statically linked Protobuf/Abseil pins.

Not claimed. No payload was assembled, signed or installed, the payload driver does
not yet build the neural worker at all, and no Windows closure was produced. This
removes a hard blocker for the installed payload; it is not installed-execution
evidence, and M5 still owns the nine host tuples.

## The Windows payload's runtime closure is derived from its own imports

Packaging could derive a macOS helper's runtime from its load commands but refused
the same for Windows, so a Windows payload could only be staged with a
hand-written dependency list. That asymmetry is gone.

Added `tools/phase13a/pe_linkage.py`, which reads a PE image's import directory:
the DOS header, the optional header's data directory, the real section table for
RVA-to-offset translation, and the import descriptor array up to its declared
size. Import names are returned in descriptor order, and a descriptor that names
a path instead of a module is distinguishable.

`derive_runtime_closure()` now lives in `tools/phase13a/runtime_closure.py` and
dispatches on the image container, with `macho_linkage` reduced to Mach-O parsing
and resolution. The two platforms need different rules and the module states
both: macOS resolves `@rpath` through LC_RPATH entries, so an absolute
build-directory rpath is reported unresolved; Windows records only module names
and its loader searches the running image's directory first, so a bare module name
resolves beside the helper while a path-naming descriptor does not. Resolution is
case-insensitive on Windows and follows symbolic links, so a plain-name copy of a
versioned runtime is staged under the name the import asks for.

One deliberate rule: the VC++ runtime is not treated as host-provided. Assuming it
exists is exactly the failure mode this work exists to catch, so `vcruntime140.dll`
and friends are reported unless the release owner ships them or supplies them in a
search path. Only the operating system's own modules (including `api-ms-win-*` API
sets) are skipped.

Staging and the assembly CLI use the same owner for both platforms:
`--runtime-search-path` now derives a Windows closure too, and the previous
"requires explicit dependencies" refusal for Windows is removed.

Verification: `tests/phase13a/test_neural_helper_staging.py` grows to 17 cases with
three new ones — reading a synthetic PE import directory (including the truncated
and non-PE refusals), deriving a Windows closure that stages the package's own
modules while reporting a path-referencing descriptor and a missing module, and
staging a Windows payload end-to-end from a derived closure so
`neural_package_inventory()` reports `VERIFIED_FILES`. The staging group runs 46
tests. The PE fixtures are synthetic: the Windows worker has still never been
built or executed on Windows, so this proves the analysis and the refusals, not a
running Windows helper.

## The Studio controller can plan, run, cancel and resume a generation campaign

Closed the M1.P3 item that was still open in this document: the generation
campaign existed as authoring services and CLI commands, but the native Studio
controller had no plan/run/cancel/resume path, so a singer-scale campaign could
not be driven from the product surface.

`libs/seam-native-ui/src/voicebank_studio_campaign.cpp` adds
`beginGenerationCampaignPlan()`, `beginGenerationCampaignAdvance()`,
`beginGenerationCampaignResume()`, `cancelGenerationCampaign()` and
`generationCampaignProgress()` to `VoicebankStudioController`, implemented
entirely on the same services the CLI calls (`planGenerationCampaign`,
`VerifiedGenerationCampaign::admit`, `advanceGenerationCampaign`, the production
repository). The controller never edits campaign JSON and never writes producer
state itself: planning publishes one immutable definition into a new directory,
and advancement is a loop of one-bounded-batch service calls. A second plan into
an existing directory is refused, so a stored campaign is resumed or advanced,
never replaced. Planning also refuses when the durable producer no longer matches
the `initialProducerSha256` the definition binds, which is what made the CLI's
stale-state check necessary in the first place.

Cancellation keeps the contract the plan requires. The stop is honoured before
every batch that would render or commit, committed batches stay in the
repository, the recorded campaign identity is unchanged, and the controller
reports `Cancelled` with the real campaign size so `beginGenerationCampaignResume()`
continues from the campaign's own receipts. The batch count is read from the
admitted definition before the first batch, so a cancel that lands while the
worker is starting still reports "0 of 2" rather than "0 of 0" — the first
version of this code reported the latter and the new test caught it.

`tests/test_voicebank_studio_campaign.cpp` with CTest `seam_studio_campaign_tests`
covers the three outcomes against a real producer workspace and a real recipe:
planning writes the definition and refuses to replace it (including the empty
recipe and empty take-id refusals); advancement commits two one-job batches,
adopts the recovered producer into the controller, leaves every take in
`MarkerReview` with the durable generation advanced by exactly two, and is
idempotent when repeated; and a cancelled run keeps its retained batches and
completes on resume, while a wrong campaign digest fails with `Conflict` and
`Failed` progress instead of advancing a different definition. The fixture is a
synthetic producer, recipe and audio: it proves the orchestration and its
refusals, not a useful singer or a qualified resource.

The native surface now exposes it. `studioGenerationControls()` adds a "Plan
campaign" control and a "Run campaign" / "Resume campaign" control below the
single-job controls: planning is offered whenever the producer has planned take
ids, running only once this controller recorded a campaign identity, and either
recording or a busy worker disables both. `apps/seam-voicebank-studio-native`
wires them to the platform dialogs and to `Cmd/Ctrl-Shift-C` and
`Cmd/Ctrl-Shift-Y`, with a new `FileDialogPurpose::PlanGenerationCampaign` that
names a *new* folder (save mode with directory creation on AppKit and Win32, since
the definition must live in its own directory and an existing one is never
reused). ESC already cancelled a run because campaign work shares the controller's
stop source, and the status line already reports batch progress, so no separate
progress or cancel path was needed.

Verification for the surface: `seam_studio_campaign_tests` checks the control set
(present only with a producer workspace, planning enabled for planned take ids,
running disabled until an identity exists and relabelled to "Resume campaign"
afterwards, every control disabled while recording, and non-colliding bounds down
to the 720px minimum width), and `seam_export_tests` now finds the batch and cancel
controls by id instead of by position, because the panel legitimately grew a row.

## Neural tracks can render, cache and publish through the authoring coordinator

Added the plan's `tests/test_neural_render_workflow.cpp` with CTest
`seam_neural_render_workflow_tests`, and it immediately found that the
coordinator could not render a neural track at all.

`AuthoringRenderCoordinator::preflight()` resolved a track's source and then fell
through to the sample-bank requirement for every source that was neither a recipe
file nor a procedural resource. A neural source has no voicebank reference, so an
audible neural track was refused with "Voicebank ID and version are missing"
before any render started. Preflight now handles `TrackNeuralSource` explicitly:
it requires an admitted, valid bundle and a selected runner, and it does not ask a
neural track for a sample bank. Whether the project's saved selection agrees with
the admitted bundle stays with the project renderer, which sees both. The new
failure is a typed `RenderFailureKind::NeuralSourceMissing`, mapped to the CLAP
editor's failed preview status and the `RENDER_FAILED` diagnostic code so a neural
problem is never reported as a missing voicebank.

The same workflow test then found that the neural branch of
`ProductionProjectRenderer::renderWithSources()` never consulted the PCM cache:
every neural phrase re-ran the worker and `cacheHits` stayed zero. The neural
branch now loads and stores cache entries under the prepared content identity,
which already binds the admitted bundle digest, the feature and control identity,
the provider, runtime and worker versions, the quality setting and the owned
window. A second identical submission is a cache hit, and changing the worker or
runtime is a miss, so one execution's audio can never be served for another. The
renderer identity recorded in the cache entry is `seam.neural-worker.v1`, now a
shared constant (`rendering::kNeuralRendererIdentity`) so a cold render and a
cache hit disclose the same renderer.

Publication now names the execution instead of guessing it. `PublishedProjectAudio`
carries `neuralIdentities`, one entry per audible neural track with the track id,
model id, model version, bundle content hash, configuration version, inference
step count and the worker/runtime/provider provenance — for the failure path too,
so a failed neural render still says which model was attempted. `activeRenderer`
reports `seam.neural-worker.v1` for an active neural track instead of the
source-filter label that a unit-plan-derived name produced for every non-voicebank
source.

The three workflow cases are: two simultaneous neural tracks whose published
identities and phrase content hashes stay distinct; a cancelled neural preview that
publishes nothing (idle retained slot, no identity) and then completes on retry;
and cache provenance that hits for an identical submission and misses when only
the worker or runtime changed. None of them proves musical output: the executing
runner is the transport fixture probe, which returns silence and performs no
inference.

Two more cases close the rest of M2.P2 item 7. A neural project now exports a
committed master and stems through `ExportService::exportSetWithSources()`, with
the exported WAV frame count equal to the neural render and a reproducible master
digest across two exports of the same execution. A source with no runner is a
typed `NeuralSourceMissing` failure with the diagnostic text naming the missing
admitted bundle: the previously published audio keeps its revision and samples,
and a repaired request publishes again at a newer revision. So an older
successful phrase cannot be published over a failed current neural request.

Verification: the workflow target now passes 5 cases.

Verification: the new target passes 3/3, the neural label group 7/7 and the
neighbouring `seam_neural_phrase_runner_tests`, `seam_neural_render_tests` and
`seam_authoring_render_coordinator_tests` all pass unchanged.

## The packaged helper's runtime closure is read from its own load commands

Staging could copy a library, but it could not tell whether the helper would find
it after installation. Added `tools/phase13a/macho_linkage.py`, which reads the
Mach-O load commands (`LC_LOAD_DYLIB` and its weak, re-export, upward and lazy
forms, `LC_ID_DYLIB`, `LC_RPATH`) and answers the question that matters: which
images the host provides, and which references resolve from the directory the
helper is launched out of. It parses thin 32/64-bit and universal images, selects
the slice the payload will actually run, and refuses a truncated or oversized load
command table instead of guessing.

`derive_runtime_closure()` walks that linkage recursively through supplied search
paths. A reference that the host provides is skipped. A reference that exists on
the build machine but is reached through an absolute path or an rpath that will
not exist in the package is reported as unresolved rather than copied, because
copying the file would not repair the load command. Runtime SDKs publish a
versioned file behind the load-name symlink, so search paths resolve symlinks and
the real file is staged under the name the load command asks for.

Staging now accepts `runtime_search_paths` and refuses the whole payload when any
entry is unresolved, and `scripts/assemble_release_payload.py` exposes it as
`--runtime-search-path`. Deriving a closure is implemented for macOS arm64; a
Windows payload must pass explicit dependencies until PE import analysis lands.

The first run of this analysis against the real development worker produced a
concrete defect: the worker linked `@rpath/libonnxruntime.1.dylib` through the
absolute build-directory rpath, so the packaged helper would have failed to load
its runtime on any user machine while working here. `seam_neural_worker` now
carries `@executable_path` and `@loader_path` ahead of the development SDK
directory, so the ONNX Runtime library resolves beside the staged helper and the
development run still finds it. After the rebuild the analysis reports
`libonnxruntime.1.30.0.dylib` staged as `libonnxruntime.1.dylib`, which is exactly
the name the load command asks for.

What the same analysis still reports as unresolvable is real remaining work, not
a packaging bug: 81 absolute Homebrew references (78 abseil, 2 protobuf, 1
OpenSSL). The shipped worker must link the vendored static OpenSSL and a static
or staged protobuf/abseil closure before a payload can be assembled with a
derived closure. Until then the staging path refuses that worker by design, and a
hand-written `--neural-dependency` list would have to ship 81 libraries whose
reference paths do not exist outside this machine, which is why the check refuses
rather than papers over it.

Verification: `tests/phase13a/test_neural_helper_staging.py` grows to 14 cases
covering load-command parsing, absolute-rpath refusal, symlinked SDK publication,
derived staging and closure refusal; `seam_neural_package_materialization_tests`
passes, and the new `seam_neural_worker_relocatability` target checks the invariant
against the real built worker on every run of the neural label group (7/7 passing).

## The neural helper is staged from finalized bytes into every payload surface

Completed the packaging half of M2.P2 item 9 for the two declared targets. No
packaging path could previously place the neural worker and its inference runtime
into a payload, so `neuralPackages` could only ever report `MISSING` and a
released payload had nothing to launch.

Added `tools/phase13a/neural_helper_staging.py`. `stage_neural_helper()` takes the
finalized worker, its runtime dependencies, the payload platform and the build
identity, then stages one helper package beside every declared surface and seals
it through the existing `build_neural_package_manifest()`. Staging is two-phase:
every surface is validated before the first byte is written, so a payload can
never be left half-staged by a later failure. An artifact that is already staged
with identical bytes is left untouched, and one staged with different bytes is
refused instead of silently replaced.

The platform check reads the image itself, not the file name. `image_identity()`
parses thin and universal Mach-O headers and PE headers, and
`require_platform_image()` refuses an artifact whose container or machine does not
match the target: a Mach-O cannot be sealed into a Windows payload, a PE cannot be
sealed into a macOS payload, an arm64 target refuses an x86_64-only Mach-O, and a
universal image is accepted only when it declares the required machine. A file
that is not a Mach-O or PE image is refused rather than treated as a dependency.
The helper name (`neural-helper`/`neural-helper.exe`), the surface layout, the
manifest location and the module path all come from one place:
`neural_package_layout()` now owns the per-surface layout that
`neural_package_inventory()` used to restate.

The assembly entry point can now stage before sealing:
`scripts/assemble_release_payload.py --neural-worker <path> [--neural-dependency
<path>]... [--neural-surface <id>] [--neural-protocol-version 1|2]` stages the
helper, binds it to the payload's own `RELEASE_IDENTITY.json` build id, and then
assembles and seals the payload. Without those flags the previous behavior is
unchanged and `neuralPackages` still records explicit absence.

Verification: `tests/phase13a/test_neural_helper_staging.py` adds 8 cases over
container parsing, macOS and Windows staging, cross-platform refusal, universal
binaries, name conflicts, replaced bytes and the CLI; the payload assembly suite
adds an end-to-end case where the CLI stages a helper and the sealed manifest
reports `VERIFIED_FILES` for every Windows surface, plus a refusal case for a
macOS image. The 40 tests of `seam_neural_package_materialization_tests` pass
(3 probe-dependent cases skip when the group is run without the CTest-provided
native probe). The fixture images used by these
tests are labelled fixtures: they prove the packaging path and its refusals, not
that an inference-qualified worker was produced.

Still open in this package: an actual built worker plus a verified ONNX Runtime
dependency closure per platform (the macOS closure must be derived from the
image's own load commands rather than a hand-written list), the Windows process
backend execution, and M2.P2 item 7's native preview/stop/retry, multi-voice
scheduling and cache provenance on the neural path.

## Platform identity is one explicit table, not string coincidence

Advanced the naming half of M2.P2 item 9. The product names its two supported
targets in three namespaces whose Windows spelling differs on purpose: host and
install evidence carry a platform name plus a separate architecture
(`macos`/`arm64`, `windows`/`x86_64`), payload, update and neural-deployment
descriptors carry one identifier (`macos-arm64`, `windows-x64`), and the
full-product contract carries `macos-arm64`/`windows-x86_64`. Nothing previously
owned the translation between them, so a caller that compared
`windows-x64` with `windows-x86_64` would either pass by coincidence or fail by
accident.

Added `tools/platform_identity.py` as that owner. One frozen table holds a row
per target with its host name, architecture, deployment identifier and optional
contract identifier; `linux-x64` is deployment-only and therefore has no
contract identity. The accessors are `host_platforms()`, `deployment_platforms()`,
`product_contract_platforms()`, `product_contract_platform()`,
`deployment_platform()`, `deployment_platform_for_host()` and
`identity_for_deployment()`. Every accessor refuses an unknown value, a wrong
namespace (the contract spelling is not a deployment platform and the reverse is
also true) and a malformed or empty string with `PlatformIdentityError`, so an
unlisted platform cannot pass through unexamined.

Four duplicated tables now read from that owner instead of restating the strings:
`tools/phase13a/update_contract.py`, `tools/external_beta/host_evidence.py`,
`tools/external_beta/install_evidence.py` and
`tools/external_beta/product_soak.py` take the host or deployment view, and
`tools/external_beta/full_product_contract_registry.py` takes the contract view.
`host_platforms()` deliberately returns only the two certifiable host targets, so
the install and soak evidence contracts keep exactly the platform pairs their
matrix documents declare.

The mapping also became load-bearing rather than descriptive. The sealed release
payload manifest now records `productContractPlatform` through
`product_contract_platform()`, and `verify_release_payload_manifest()` re-derives
that value, so a payload whose contract identity was rewritten after assembly is
refused as a manifest identity failure instead of being trusted. On the native
side, `tests/test_neural_worker_protocol.cpp` now proves the two namespaces are
distinct: a correctly signed descriptor declaring `windows-x64` loads, and an
equally signed descriptor declaring the contract spelling `windows-x86_64` is
refused. Translation may only happen through the mapping owner.

Verification: `seam_platform_identity_tests` is registered (9 tests),
`seam_neural_package_materialization_tests` passes 30 tests including the new
payload-manifest binding case, the external-beta Python contract suite passes
179 tests, and `seam_neural_worker_protocol_tests` passes with the new native
guard. Packaging the actual worker and runtime into a payload is still open: no
packaging script stages the neural worker or its ONNX Runtime dependencies yet,
and the Windows process backend still has no executed evidence. (The packaging
half of that sentence is superseded by the entry above: staging now exists.)

## Production neural worker executes admitted bundles

Added the prepared admission handle `AdmittedNeuralBundle`
(`libs/seam-neural-synthesis/{include/seam/neural_synthesis/model_bundle.hpp,src/model_bundle.cpp}`).
`admit()` verifies the frozen manifest, both graph payloads, the vocabulary and
the declared execution identity once, then retains those immutable payloads as
shared pointers. Copying a prepared handle therefore shares one model instead of
duplicating graph bytes per phrase snapshot, which is what the render path needs
before it can prepare work outside the audio callback. The handle carries
`ModelContract`, `NeuralBundleVocabulary`, `MelFeatureSpec`, steps layout,
vocoder output name and a `NeuralExecutionIdentity` that includes the bundle
content digest and the inference-step count, because steps change the result for
identical input.

Admission refuses four cases the metadata inspection alone would allow: a
configuration schema v1 bundle, which declares neither a steps layout nor a
vocoder output name and would otherwise execute assumed defaults; a bundle whose
declared frame bound exceeds the caller's prepared budget; an out-of-range or
zero step count and frame budget; and a vocabulary that does not decode or does
not match the declared model vocabulary hash. A moved-from handle is refused as
well. `tests/test_neural_model_bundle.cpp` covers acceptance, identity binding,
asset sharing across handle copies, a distinct identity for a different step
count, and every refusal above. This handle is not operator-level graph admission
and not an OS sandbox: the child still parses the exact bytes it executes.

Verification note: the complete Release suite passed 132/132 serially in 275.42
seconds. An earlier `ctest -j 4` run on the loaded machine reported nine failures
(demo smokes, two neural runtime checks, dependency direction, production staging
and import outcome); every one passed in `--rerun-failed` and in the serial run.
Those parallel failures were contention, not regressions. Prefer a serial run or a
quiet machine when interpreting this suite.

### Next unit: the neural render path, with two inspected constraints

Implemented `RenderSnapshotFactory::createNeural()`
(`libs/seam-rendering/src/render_snapshot.cpp`). It binds one admitted model
bundle to one vocal region and refuses to invent any part of the identity: the
bundle owns model identity, the project owns music and pronunciation, and the
caller supplies `NeuralRenderProvenance` (worker build, runtime version, selected
provider) from the signed deployment descriptor. A `RenderSnapshot` now carries
`neuralExecution`, the authoritative prepared bundle, while `resource` holds an
inert `NeuralSingerResource` tag; nothing may read resource bytes as a model.

`buildNeuralIdentity` hashes the bundle digest, configuration version, inference
steps, declared frame bound, mel geometry, amplitude scale, vocabulary hash, steps
layout, vocoder output name, `kDiffSingerInputRevision`, the execution provenance,
the frozen project JSON, pronunciation identity, style, ABI, quality and owned
window. Two runs that differ in any of those cannot share cached audio. The
factory refuses a bundle that cannot be prepared, unbounded or non-printable
provenance, a region with no notes, an unknown track or region, a snapshot rate
that differs from the admitted model rate, an owned window outside the score
context, and a track already bound to a procedural recipe.

`renderResourceFamily()` now reports Sample, Procedural or Neural from one place,
and `findSample()`/`findProcedural()` give checked carriers. This was not cosmetic:
the first draft of the render test called `sample()` on a neural snapshot and
trapped with `bad_variant_access`, which is exactly the failure mode the plan
warned about for sample-only accesses. Neural subdivision and neural pipeline
execution remain explicit `Unsupported` refusals with named messages, so no path
can silently treat a prepared bundle as a bank.

Verified: `seam_neural_render_tests` passes, and the complete Release suite passed
132 of 133 with the only failure being source closure for these then-untracked
files.

Added the pipeline half of that package. `PhraseRenderPipeline` now takes an
optional `std::shared_ptr<const NeuralPhraseRunner>`; the abstract
`NeuralPhraseRunner` is what the application implements, so helper selection,
expected digest and process budgets stay out of the bank and out of the audio
callback. A pipeline without a selected runner refuses a neural snapshot with a
named `Unsupported` error instead of falling back to sample material. With a
runner, the branch verifies the snapshot is complete, that its rate matches the
admitted model, and that the returned audio covers the declared owned window with
exactly the declared frame count. It rejects a runner that returns sample
placement metadata as an invariant violation, forwards the compiled phonemes,
reports `SingerResourceKind::Neural`, and leaves the unit plan and procedural
markers empty. `PhraseAudio` carries no sample rate, so rate agreement remains the
runner's contract and is stated in the interface.

`seam_neural_render_tests` covers both halves with a test-only runner: refusal
without a selected runner, successful execution, exact owned-window enforcement, a
one-sample overlap rejected as a conflict, fabricated placements rejected as an
invariant violation, and cancellation reaching the runner. The production runner
itself is still to be written in the authoring/application layer, where the
deployment descriptor and `runNeuralBundleWorker` are available.

Implemented that production runner as
`libs/seam-authoring-runtime/{include/seam/authoring/neural_phrase_runner.hpp,src/neural_phrase_runner.cpp}`.
`NeuralPhraseRunnerOptions` carries the canonical bundle directory, its payload
budget, the already-resolved helper identity and process budgets, and the
explicit silence symbol; `validate()` refuses a relative path, an unbounded
bundle, a non-bundle launch contract, an unbudgeted process, a relative helper or
a missing helper digest. `create()` also reads the selected directory's manifest
and binds its digest, so a snapshot admitted from one bundle cannot be rendered
through a different one. `render()` builds the request with
`prepareNeuralScoreRequest`, sets `bundleContentHash` so the launch satisfies
metadata v3, re-validates against the admitted model contract, runs
`runNeuralBundleWorker`, and returns window-exact mono audio with no placements.
A vocabulary without the declared silence symbol is refused instead of guessed.

Two real constraints surfaced while implementing this. First, the neural path has
no bank source, so consonant timing uses the existing source-independent in-note
policy; `createNeural` now selects it explicitly and the timing-policy revision
participates in the render identity. Second, request preparation refuses any
phoneme span outside the requested output window rather than clipping it, so a
partial neural window that excludes part of a syllable is refused with
`InvalidArgument` today. Windowed neural rendering therefore needs a package that
defines how out-of-window phonemes are represented before it can ship; the test
records this refusal instead of hiding it.

`seam_neural_phrase_runner_tests` drives the real parent transport with the
existing bundle transport fixture as the selected helper and covers: a bound
request and window-exact result over the full phrase, refusal of every unsafe
execution option, a foreign bundle rejected as a conflict, cancellation, and a
vocabulary without silence refused as unsupported. The helper returns silence by
contract, so this proves admission, request binding and response validation, not
synthesis. The real inference path is covered separately by
`seam_neural_production_worker`, which executes actual ONNX graphs.

Closing the model chain, `tools/voice_model_training/prepare_bundle.py` composes an
admitted bundle directly from real acoustic and vocoder export directories. The
declaration is read from the receipts rather than typed by hand: it re-verifies
each graph against its recorded digest and byte count, requires both exports to
declare the same supported 48 kHz/80-bin logarithmic-mel profile with a matching
`profileSha256`, requires the acoustic runtime smoke result, takes the ordered
vocabulary from the acoustic receipt and converts it through the native-compatible
converter, and writes the configuration and vocabulary with canonical bytes. The
manifest is published last, so a directory without it is not a bundle.

Two representation details were settled by inspection rather than assumption. The
stored target matrix is `[T,F]` while the admitted graph consumes `[1,T,F]`, so the
declaration maps the profile layout to `BTF`. Native `JsonValue` objects are
`std::map`, and the frozen manifest is published pretty-printed with sorted keys
and a trailing newline; the tool reproduces those exact bytes, which
`check_bundle_preparation.py` verifies by preparing the same assets through the
native CLI and comparing digests and manifest bytes.

`seam_neural_bundle_preparation` then runs the composed bundle through the
production worker with a real SNW1 v3 request and asserts the v3 response, bundle
binding, request hash and expected PCM. Refusals cover tampered graph bytes, a
profile mismatch between exports, an unsupported profile, a vocabulary whose
padding token repeats, a missing runtime smoke result, an existing output
directory and a nonpositive frame bound. The graphs are deterministic arithmetic
fixtures, so the chain is proven while the singer remains unqualified.

Added the persisted product selection. `domain::NeuralResourceReference` stores
only a singer resource identity, and `VocalTrack` now holds an optional
`neuralResource` beside `proceduralRecipe`; `Project::validate()` refuses a track
that selects both families and refuses a reference whose kind is not Neural.
Project JSON is schema 10: the track member is written explicitly (including
`null`), and it is required when the schema is 10 or newer so an older build
refuses a newer project instead of silently dropping the selected voice. A schema
9 file without the member still migrates to "no neural selection", while a file
that claims schema 9 and carries a neural reference is refused rather than
half-understood. No path, helper command or resolved execution state is stored in
a project file.

The render path now honours that selection in both directions. A track that saved
a neural selection cannot produce a sample-bank snapshot, mirroring the existing
saved-procedural-recipe refusal, and `createNeural` refuses a bundle whose model
id, version or bundle digest differs from the saved selection. An unbound track
still previews, so trying a voice before selecting it stays possible.

Verified: `seam_tests` (including the new schema round-trip, migration, smuggling
and family-conflict cases) and `seam_neural_render_tests` (including the
match/mismatch selection cases) pass.

Closed the remaining link between a saved selection and installed bytes.
`prepare_bundle.py` now accepts `--resource-id` and `--resource-version` and, when
both are given, publishes a `resource.json` record after the manifest with
exactly that identity and the manifest digest as its content hash. The record is
never guessed from the directory and the two values must be supplied together.

`NeuralResourceRegistry` (`libs/seam-authoring-runtime/{include,src}/seam/authoring/neural_resource_registry.*`)
scans an installation root under explicit resource, per-asset and total byte
budgets, and verifies each candidate before indexing it: the record schema and
printable identity, that the manifest digest equals the recorded content hash,
that every declared asset exists as a regular file with exactly its recorded
length and SHA-256, and that the four required roles each appear once. A
directory without a record, a tampered asset, a duplicate identity, a relative
root, an out-of-budget asset or a cancelled scan is refused; an empty root is a
valid empty catalog. `resolve()` matches id, version and digest exactly, so a
saved selection can never be satisfied by a different voice, and a reference
whose kind is not Neural is refused before lookup.

Verified: `seam_neural_phrase_runner_tests` now also covers registry scanning and
resolution, and `seam_neural_bundle_preparation` asserts the record's identity
against the manifest digest and refuses a half-specified identity.

Reached the product render path. `rendering::TrackSingerSource` gained a
`TrackNeuralSource` carrying the admitted bundle, the execution provenance and the
selected runner; the source carries no helper path because the runner already
holds the resolved first-party selection and its process budgets.
`ProductionProjectRenderer::renderWithSources` dispatches a neural track to
`createNeural` plus `PhraseRenderPipeline{runner}`, produces mono model audio at
the phrase's absolute start frame, routes it through the same track route and
gain as other families, and publishes the snapshot content hash. The previous
fall-through to the sample branch is now unreachable for a neural source, so a
prepared model can no longer trap or be treated as a bank.

The saved-selection rule is enforced in two places: the coordinator refuses a
resolved singer whose model id, version or bundle digest differs from the saved
selection, and `createNeural` refuses the same mismatch at snapshot level. A
missing runner is an explicit invalid-argument refusal rather than a silent
substitution. `seam_neural_phrase_runner_tests` covers a full neural project
render (one track, one region, one phrase, stereo routing with silence on the
right channel, empty unit plan and no invented diagnostics) plus the mismatch and
missing-runner refusals.

Measured the worker invocation cost that decides whether the pilot needs a
bounded session owner. `seam_neural_production_worker` now times its first
accepted invocation and three identical repeats on the development Mac and prints
them as JSON. Final recorded run: cold 0.0521 s, warm 0.0484/0.0498/0.0493 s. The
fixture bundle is about one kilobyte of arithmetic graph, so these numbers bound
process creation plus admission and deliberately exclude real model-load time; a
production-sized bundle will be larger and slower to admit. The decision stays
`pending real-model measurement`: with roughly 50 ms of fixed per-invocation cost
on this machine, a per-phrase process remains acceptable for a pilot, and the
question only becomes pressing once a real bundle's admission and reload time are
measured against the agreed budget.

Advanced the primary lane's installation handoff (M1.P3 item 6). The installed-song
regression in `tests/test_standalone_voicebank_workflow.cpp` now goes past its
one-unit fixture: it derives the phone symbols the engine will actually request
for the phrase from the phonemizer instead of guessing unit names, packs a
multi-unit bank for those symbols, installs it through the trusted installer, and
selects it. The new song then reports complete coverage, the producer's generation
inputs are removed from disk, and the song must still export master plus stems
from the installed bank. It saves through `ApplicationCommand::SaveProjectAs` and
reopens through `ApplicationCommand::OpenProject` on the same controller, verifies
the reopened track still binds the same bank id, version and content hash with
complete coverage, and re-exports to prove the master hash is identical. That
closes the "producer workspace unavailable to the new song" requirement with
evidence rather than intent.

M1.P3 remains open: the full source → generation campaign → edit → review →
candidate → package chain still needs its own connected regression, the plan's new
`tests/test_original_singer_workflow.cpp` and `seam_original_singer_workflow_tests`
do not exist yet, and the human/reviewer and real-input journeys remain external
evidence that this work does not claim.

Added that regression. `tests/test_original_singer_workflow.cpp` now runs as CTest
`seam_original_singer_workflow_tests` and covers the connected lifecycle the plan
names: a synthetic source is imported into a producer workspace, edited by a
committed normalize operation, prepared for review, accepted by a supplied
reviewer decision, published as a candidate bank, packaged as a signed
`.seambank`, and installed through the standalone controller. The installation
first attempts a byte-tampered package and requires that attempt to fail with no
installed card, then retries the same action successfully, so a genuine refusal
and retry is part of the flow rather than a separate unit fixture.

After installing, the new song must report complete coverage, the producer
workspace is renamed away, and the song still exports master plus stems from the
installed bank. It saves through `SaveProjectAs` and reopens through `OpenProject`
on the same controller, re-exports with an identical master hash, and verifies the
installed bank's content hash against the card. Finally it mutates the producer
draft (unit queue state and last durable generation) and proves the saved song
bytes and the installed bank manifest are unchanged, which is the plan's
immutable-old-song requirement.

Still open under M1.P3: the generation-campaign orchestration (plan/run/cancel/
resume with collected takes) is exercised by its own unit suites rather than this
connected regression, the native Studio actions for review and packaging are not
yet the path this test drives, and the reviewed-by-a-real-person and real-input
journeys remain external evidence. This test uses a synthetic source and test
identities only; it does not qualify an original singer.

Storage note: the machine reached 124 MiB free, which caused eleven unrelated
suite failures (demo smokes, contract tests, neural runtime checks). Those were
not regressions; the same tests pass with storage restored. Four regenerable
ONNX Runtime build artifacts were removed to recover space: `onnxruntime-source`
(954 MiB), `onnxruntime-telemetry-free-pinned-build` (686 MiB),
`onnxruntime-telemetry-free-build` (260 MiB) and the redundant
`onnxruntime-osx-arm64-1.30.0.tgz` (40 MiB). The telemetry-free SDK, the release
SDK, every Python environment, both source checkouts and every evidence directory
were retained. Rebuild the removed trees with `tools/neural_runtime/build_telemetry_free.py`.

`RenderSnapshotFactory::createNeural()` and the phrase-pipeline neural branch are
the next package. Two constraints were inspected rather than assumed.

`cmake/NeuralDependencyDirection.cmake` walks the transitive dependencies of
`seam_neural_synthesis` and fails only if that closure reaches
`seam_authoring_runtime` or `seam_rendering`. It does not forbid
`seam_rendering` from linking `seam_neural_synthesis`, so the factory and the
snapshot carrier may live in `seam-rendering` as the plan intends. The prepared
handle must be carried in a new snapshot member; the legacy opaque
`NeuralSingerResource::model` payload must not be reinterpreted as an admitted
bundle.

Worker run options carry the helper path, expected digest and process budgets,
and those belong to the deployment descriptor owned above rendering. The pipeline
therefore cannot build them itself: the neural branch needs an injected runner or
launch contract supplied by the authoring/application layer, keeping helper
selection out of a bank and out of the audio callback. Today the scheduler and
pipeline already reject a neural resource with `Unsupported`
(`libs/seam-rendering/src/render_scheduler.cpp`, `.../render_pipeline.cpp`), so no
existing path silently treats a neural resource as sample material.

Added `apps/seam-neural-worker/main.cpp`, the first executable that performs real
acoustic-then-vocoder inference for an admitted model bundle. The application
selects it through the existing launch contract,
`--seam-neural-worker-v2 BUNDLE_DIR MODEL_ID MODEL_VERSION BUNDLE_CONTENT_HASH
MAXIMUM_BUNDLE_BYTES`, with one SNW1 request frame on stdin and exactly one SNW1
response frame on stdout. The child never receives a command, library path or
script from a bank.

Admission order is deliberate. The worker reads the bounded request frame, then
re-loads the bundle directory itself through `loadNeuralBundleDirectory`, so a
file changed after parent inspection fails its manifest or asset digest. It then
inspects the bundle metadata, requires the request's bundle, model and vocabulary
identity to match the bytes it loaded, and only then parses both graphs with the
native ONNX inspector and the frozen pair contract. Session creation happens after
that admission; loaded interfaces are cross-checked against the admitted
declaration before any tensor is executed. A build without native graph admission
cannot execute a bundle, and the CMake target now exists only where the pinned
schema is available.

Inference reuses the shared DiffSinger path: `prepareDiffSingerAcousticInputs`
produces tokens, durations, padded F0 and the step tensor; the acoustic graph
produces mel; the admitted vocoder produces audio; `finalizeDiffSingerResponse`
validates the complete padded buffer, trims the final partial hop to the exact
requested sample range and applies sample-domain dynamics once. Rejections cover
nonfinite mel or PCM, wrong mel or audio geometry, an out-of-range gained sample
and any request/response identity mismatch.

`tools/neural_runtime/check_production_worker.py` builds a real ONNX bundle with
the production CLI, drives the worker through that contract, and asserts exact
response binding, sample count and arithmetic output. It also asserts that the
worker refuses the v1 transport contract, an invalid byte budget, a wrong manifest
digest, a wrong launch identity, bytes changed after preparation, a bundle whose
graphs fail admission, wrong model/vocabulary/sample-rate/frame identities, malformed
frames and an out-of-range dynamics result, always with empty stdout. The v1
transport fixture remains a separate executable that accepts an uninspected-graph
bundle the production worker rejects.

Verified in two builds: `build/release` with the local ONNX Runtime 1.30.0 SDK and
`build/neural-runtime/seam-telemetry-free` with the telemetry-free local SDK. All
five `neural-native-experiment` tests pass in both, including the new worker test.
These are deterministic arithmetic fixture graphs executed with real runtime and
real admission; no learned singer, voice identity, listening result or Beta claim
follows. The worker's diffusion step count is a pinned diagnostic constant because
the admitted configuration schema does not yet carry it, and the production
bundle still needs its model-configuration and packaging revision.

Authority: `SEAM_IMPLEMENTATION_PLAN_2026-09-13.md`, preserving the original
R1–R20 and Full-Scope U1–U48 obligations. This is current execution status,
not a replacement product contract or a release approval.

## Actual DiffSinger architecture checkpoint

Recovery verification: the complete Release CTest suite passed 130/130 in 86.02
seconds after the native export-path changes. This includes native paired/bundle
checks and source closure, but does not erase the separately reproduced Python ORT
teardown SIGABRT. Telemetry-free source retrieval remains active; replacement build
and qualification have not yet occurred. Existing runtime installations are intact.

Inspected ONNX Runtime v1.30.0 source at
`f2c39fe2f838cf35ce7da92824f5a5e3ee6e88a7`: runtime telemetry disabling only flips an
atomic flag; constructor initialization and SDK shutdown remain active. The source
supports `onnxruntime_USE_TELEMETRY=OFF` at compile time. Added a pinned CPU
wheel/shared-library build helper and `tools/neural_runtime/TELEMETRY_FREE_RUNTIME.md`
with the required replacement qualification. Source expansion/download is underway;
no replacement build or runtime installation has yet been claimed or performed.

Connected actual checkpoint exports to the native C++ ORT probe through
`--acoustic-export GRAPH SHA256`. Native hash binding, structural inspection,
dynamic BTF output checks and finite-value checks execute before reporting three
successful learned-weight cases (3/16/23 frames); wrong hashes reject. Existing
paired-fixture regression passes with `--native-inspection`. Native telemetry is
explicitly disabled. This remains a probe, not the production worker/render path.

The first real graph revealed native rejection code 14: the exported encoder
stores a negative-infinity attention mask, disallowed by native finite-tensor
intake. Export now rewrites only recognized scalar float32 negative-infinity
constants consumed as Where data operands to the finite float32 floor. Other
nonfinite constants reject. Valid token IDs are positive; all-padding input is
outside this contract. The real native subprocess subsequently completed all
cases and the wrong-hash rejection test.

IMPORTANT: the complete parent Python diagnostic again aborted at shutdown with
recursive_mutex failure (SIGABRT) despite disabling ORT telemetry, after native
subprocess success. Disabling telemetry has NOT proven a root-cause repair. Native
inference evidence is valid for that subprocess, but the overall export/runtime
environment remains unqualified. Do not rerun until green and discard this finding;
shipping runtime lifecycle/telemetry implementation needs further investigation.

Added the persistent acoustic export command from a captured trusted local
checkpoint. It verifies architecture settings, vocabulary and acoustic profile,
strictly restores weights, exports/inspects the merged graph, executes an ORT smoke
case and publishes acoustic.onnx followed by export.json. The actual upstream
diagnostic now invokes this command in a separate process and verifies published
graph hashes and checkpoint receipt provenance; the complete run exited zero with
checkpointExportVerified=true. These are temporary engineering fixtures in the
diagnostic; no qualified singer asset or redistribution authorization is created.
The command itself preserves its selected output for later native integration.

Added real-weight denoiser ONNX/PyTorch parity using identical supplied tensors:
eight cases over 3/16/23/257 frames and timesteps 0/7 met 1e-5 absolute/relative
tolerance; maximum observed error 7.450581e-8. This isolates learned-model arithmetic
from cross-runtime RNG differences and does not close stochastic sampler parity.
One diagnostic run exited 134 after emitting passing results. The macOS report
`Python-2026-09-13-195108.ips` identifies an ORT Microsoft telemetry worker in
`DebugEventSource::DispatchEvent` / HTTP response handling, with recursive_mutex
failure during teardown. An unchanged repeat exited zero. ORT telemetry is now
disabled before diagnostic session creation; broader shutdown reliability remains
unqualified and JSON `passed` alone must never substitute for a zero process exit.
The first complete rerun after telemetry disabling exited zero and retained all
eight denoiser parity passes; this is a smoke result, not a stability soak.

Actual encoder+diffusion ONNX merge now passes SEAM's data-only graph inspector
and ONNX Runtime 1.30.0 with scalar runtime steps (1,4,8) and dynamic frame lengths
(16,3,23). All outputs have finite [1,T,80] geometry. Merged graph is 845063 bytes,
SHA-256 `f9e0862d596fddfb88f2e4f7c17db2c962224541681a190e660dc5052f6282fb`.
Root-cause fixes: a typed non-shallow entry avoids upstream Optional-Tensor JIT
inference failure while reusing upstream samplers and requiring seeded exact Torch
parity; seeded outer initializer name mappings preserve If/Loop captures during
graph prefixing/merge. This is real trained-weight graph execution, not arithmetic
fixture substitution. Stochastic numerical parity across runtimes, vocoder/model
bundle publication and native production rendering remain outstanding.

Serialized the actual trained duration encoder to ONNX opset 17 and executed its
owned bytes with ONNX Runtime 1.30.0. Five shape cases passed (including zero-duration
phone and 1025-token sequence), maximum absolute error 9.536743e-7. Materializing a
4096-token positional table before tracing prevents the upstream lazy-growth branch
from freezing the initial 1024-entry capacity. Intermediate graph: 644485 bytes,
SHA-256 `cbb7c6ac9069526cb7b70697cb6ca97461516ac0495024a8e50d70890a2b61de`.
Verified with NumPy 1.26.4, ONNX 1.19.1, ml-dtypes 0.5.3 after repairing an unintended
NumPy upgrade from unconstrained dependency resolution; explicit export-check pins
are now provided. This encoder is not the complete SEAM acoustic graph. Diffusion
serialization, graph merge, vocoder and native production rendering remain open.

Inspected the actual upstream deployment modules and SEAM acoustic/vocoder tensor
profile. Added a strict training-weight → deployment-model bridge with explicit
natural-log scale binding: upstream deployment defaults to converting log10 mel,
whereas SEAM targets already use ln amplitude. Setting `mel_base=e` avoids that
incorrect extra scaling. The actual model passed strict state loading and exact
conditioning parity at 3, 16 and 23 frames (including a zero-duration phone), and
deployment diffusion returned finite [1,23,80] mel. This is Torch-side deployment
verification, not serialized ONNX parity, acoustic quality or native rendering.

Added bounded multi-epoch command execution, retaining one parent-linked checkpoint
per completed epoch and publishing a run completion record only after all requested
epochs finish. Source/label/shard admission refreshes each epoch with stable dataset
identity; live optimizer and CPU RNG state continue without reinitialization. Run
time and cumulative binary checkpoint bytes are bounded cooperatively. Earlier
complete checkpoints remain recoverable if a later epoch fails.
Actual upstream-model verification passed: two continuous epochs match separate
first-epoch/resumed-second-epoch execution in loss, model tensors and CPU RNG.

Implemented strict local-checkpoint continuation in the training command. Resume
requires captured receipt identity, matching configuration/environment/profile and
fresh matching dataset admission before optimization. It restores model, optimizer
and CPU RNG state and records completed epochs plus parent receipt hash. Review
renewal, dataset migration and quality-based scheduling remain open.
Verified with two separate upstream-model subprocess continuations from the same
first-epoch checkpoint: identical second-epoch loss, model tensors and CPU RNG,
with completedEpochs=2 and matching parent receipt identities in both outputs.

Added the runnable `tools.voice_model_training.train` CPU entry point: exact
configuration hashes, shared dataset input capture, bounded model settings, flat
target inventory, trusted pinned upstream checkout and reviewed-epoch publication.
The real architecture diagnostic invokes it in a fresh subprocess and reloads the
result: one complete 16-frame training phrase, loss 0.9917272, matching checkpoint
receipt and coverage. Configurable continuous epochs are now supported; quality-based scheduling,
real corpus training, export and native singer rendering remain open. See the
training tool's `TRAINING_COMMAND.md` for the runnable contract.

The next connected diagnostic has now passed against clean pinned upstream
DiffSinger revision `336cf01b57f2ad44c6b37a79cf33993043291759` (Torch 2.8.0,
NumPy 1.26.4, CPU). Three distinct temporary oscillator sources traverse actual
file capture, fixture-only dual signatures, sharded admission and the reviewed
training service. One train phrase covers all 16 frames, changes 43 model tensors,
and restores checkpoint parameters exactly. A separate validation phrase executes
the evaluation path; the third fixture remains in the test partition. No mock
replaces admission, target loading or optimization in this diagnostic. Reported
training loss 0.9709514 and validation loss 1.0373794 describe synthetic mechanics,
not lyric intelligibility or held-out singer quality. Real corpus production,
multi-epoch training, export and native rendering remain open.

Connected `train_reviewed_epoch` now orchestrates fresh admission, paired targets,
whole-phrase optimization, complete train-frame coverage and revalidated checkpoint
publication. Preflight rejects preparation issues and phrases above 4096 frames.
Cancellation/expiry interrupt between updates; source/label/shard admission is
repeated after the epoch and before the final receipt. Contract tests cover early
rejection, late identity changes and failed training without publication. These
mocked boundary tests do not establish real-corpus training or singer quality.
The signed-fixture integration is now exercised as described above; production
multi-epoch execution and actual model export/render integration remain next.

Added read-only conditioning reuse during fresh dataset assembly. Existing shards
must match features reconstructed from newly admitted source/label inputs; they
are never rewritten. CLI tests preserve dataset identity and shard bytes on a
successful refresh, reject missing reuse directories, altered shard content and
changed source audio, and publish no snapshot on rejection. This enables fresh
admission checks at training boundaries without regenerating feature caches.

Connected checkpoint restoration to captured receipt identity and verified owned
bytes, replacing direct path loading. The local-producer loader rejects altered
binary content before Torch deserialization and verifies embedded metadata against
the completion record. The actual model still restores inference and continuation
exactly. This does not extend trust to arbitrary downloaded checkpoints or bypass
fresh source/review admission for a resumed training run.

Added reusable checkpoint publication with new-directory/no-overwrite semantics,
bounded binary writes, file fsync, metadata/binary hashes and a final authority
recheck callback before receipt publication. Tests cover byte limits, absent
coverage and late expiration without a completion record. The actual architecture
round trip now exercises this owner. It remains a local CPU checkpoint writer,
not an untrusted importer or completed authorized production-training workflow.

Recovery checkpoint verification: all 39 training-tool tests passed in the
isolated real-model environment (13.995 seconds), including optional Torch tests
executed rather than skipped. Native pitch CLI and tracked-source checks were
also rerun separately. The preceding actual upstream architecture run passed
audio-backed optimization, isolated evaluation and exact checkpoint continuation.
This checkpoint preserves connected training mechanics; it does not complete
production training, lawful corpus acquisition, model export or Beta qualification.

Added isolated CPU objective evaluation without optimizer updates, preserving
Torch RNG, existing gradients and mixed module modes. Tests cover repeatable
seeded loss and state restoration on rejected input. The real architecture check
also exercises evaluation, explicitly reusing its synthetic training fixture only
for mechanics; genuine held-out corpus evaluation remains outstanding.

Replaced constant mel targets with actual 80-bin extraction from a byte-verified
original oscillator WAV. The real upstream model now has 58,704 parameters and
its fixed-noise loss fell 0.9959463 → 0.9717840. Finite [1,16,80] inference, exact
checkpoint restoration and the next resumed update all passed. Dataset/checkpoint
engineering identity binds actual source/target/profile records and synthetic
conditioning. This supersedes prior constant-target measurements; oscillator
token labels are not linguistic supervision or a qualified singing dataset.

Added explicit epoch source/core coverage accounting with halo-mask verification.
Duplicate, missing, out-of-order and unknown-source cores reject. The architecture
experiment now treats its eight repeated updates as eight single-phrase epochs,
each with verified coverage, preserving the synthetic nature of the experiment.
Generic iterator-only runs explicitly report coverageVerified=false.

Connected the real architecture experiment to a bounded epoch runner. Eight
synthetic updates completed with sample-weighted mean loss 0.9427672, followed by
the same exact restore/resume checks. Unit tests cover weighted aggregation,
empty input, budget exhaustion, cancellation, changed identities and late shard
failure. No incomplete epoch returns a success record or writes a checkpoint;
actual sampler/coverage policy and source admission remain caller responsibilities.

Extended the actual model check with temporary checkpoint serialization of model,
optimizer and CPU RNG plus configuration/revision/step identity. Strict restoration
reproduced inference bit-for-bit, then the original and restored optimizers produced
identical next loss (0.9727515) and model state. The self-produced 712,539-byte
checkpoint was not retained. This proves the tested CPU round trip only; durable
production checkpoint handling, other-device resume and real-data training remain open.

Ran the SEAM optimization/DDPM adapter against the clean pinned upstream
DiffSingerAcoustic class, not an interface stand-in. A deliberately small random
54,024-parameter WaveNet DDPM changed 43 parameter tensors across eight synthetic
fixed-noise updates; loss decreased 0.9528179 → 0.9277189. Upstream DDIM inference
returned finite [1,16,8] mel. No learned singer checkpoint or audio was published.
The isolated model environment needed setuptools 75.8.0 for the upstream legacy
librosa pkg_resources import; no upstream code patch or runtime bypass was used.
This proves executable architecture integration, not lawful data training,
vocoder compatibility, production-size behavior or musical qualification.

## Acoustic optimization primitive

Implemented the inspected non-shallow DDPM training-call adapter and noise-loss
layout conversion. Its interface fixture reaches a real optimizer update while
checking original tokens/mel2ph and rejecting incomplete phrases or unsupported
speaker conditioning before the model call. Source inspection exposed duration
derivation from mel2ph; whole-phrase-only enforcement prevents falsely treating
halo chunks as equivalent upstream training. Actual model construction and full
conditioning/objective support remain unfinished.

Added full source token sequences and one-based mel2ph to batches and the Torch
adapter interface. Sequence construction uses original labeled phonemes rather
than collapsing frame token runs, preserving repeated and unsampled short phones.
Tests retain three identical tokens with frame ownership [1,3], exercise contextual
batch mapping and reject out-of-range alignment. Actual upstream task wiring remains open.

Cross-checked the pinned upstream acoustic task: DDPM noise and reflow velocity
objectives are distinct from direct mel regression. Extended the update primitive
with a named, model-owned per-element objective callback while retaining SEAM's
mask/weight reduction and gradient checks. Tests distinguish custom squared-noise
fixture loss from mel L1 and reject unnamed or already reduced objectives. Actual
upstream architecture, noise scheduler and training-task adaptation remain open.

Added source-local context halos to conditioning and paired target batches, with
separate core offsets and an explicit loss mask. Optimization counts only core
frames while retaining their partial-hop sample weights. Tests reconstruct all
eight owned target frames exactly once from overlapping 4/5/3-frame inputs and
verify exclusion of halo loss. Model-specific receptive-field sizing and temporal
chunk-equivalence qualification are still required.

Added a real Torch update step for supplied acoustic adapters, using paired
conditioning/targets, sample-weighted L1 and finite gradient clipping. It refuses
held-out batches and mismatched optimizer parameter ownership. Optional Torch
tests exercise decreasing loss and changed parameters in a clearly labeled
constant-output fixture, partial-tail weighting, invalid targets and a finite
forward/nonfinite backward that must not call the optimizer. This is optimizer
mechanics, not an original singer or completed M2.P3 training workflow.

## Integrated training preparation regression checkpoint

Full Release CTest run: 129/130 passed in 112.91 seconds. The source-closure test
caught two newly created comparison files not yet indexed during the run; after
staging them, its focused rerun passed in 0.26 seconds. Thus every registered
test has current passing evidence across the full run and that corrected rerun;
this was not a single clean 130/130 invocation. No runtime regression failed.

An isolated Python 3.11 / Torch 2.8.0 / librosa 0.10.2 / NumPy 2.2.6 environment
passed dependency checking and all 24 frontend numerical comparison cases.
Maximum absolute errors: 4.7401e-7 (float64 reference), 0.0016051 (float32).
The comparison explicitly prepads to SEAM's full-hop policy. Trained-model
compatibility, lawful production data, optimization, learned checkpoints,
production worker integration and musical qualification remain unfinished.

## Frame conditioning implementation

Connected acoustic target binaries to source-local conditioning batches through
`iter_supervised_batches`, requiring an explicit profile hash and matching source,
PCM, sample rate, hop and frame geometry. It verifies exact binary content and
finite float32 values, then supplies owned target slices. End-to-end tests use
the actual extraction CLI and compare batches of 3/3/2 frames to the full API
output, with source-identity and single-byte corruption rejection. Training
optimization, upstream numerical parity and qualified data remain outstanding.

Exposed acoustic extraction through `acoustic-targets CONFIG HASH WAV NEW_DIR`.
It captures bounded regular source bytes, verifies the expected digest, computes
targets, writes little-endian float32 data, and publishes hash-bound metadata
last. Subprocess tests verify binary equivalence to the API, hashes, no overwrite
and altered-source rejection without output. This closes standalone extraction,
not dataset/optimizer integration, upstream parity or real singer qualification.

Connected mel extraction to owned, digest-verified WAV bytes through the existing
PCM inspector. Added signed 16/24/32-bit decoding and target/profile/source hashes
without resampling or source writes. Cross-width exact-signal tests passed,
including negative full scale and 24-bit sign extension; wrong digest/rate reject.
This yields acoustic data, but does not yet publish a target cache or train a model.

Added actual log-mel target computation as an optional NumPy API after inspecting
pinned DiffSinger and librosa source. Its explicit full-hop tail policy differs
from unpadded upstream extraction; no learned-model compatibility is claimed.
Local NumPy 2.4.4 tests ran (not skipped), covering silence, partial-hop geometry,
amplitude scaling and invalid input. Upstream numerical comparison, authenticated
audio intake, acoustic shard integration and optimizer consumption remain open.

Implemented source-local, partition-specific column batch consumption of schema-3
shards. The reader recomputes split geometry, checks binding/reference identities,
and compares each shard with conditioning reconstructed from captured labels
before yielding it. Integration tests cover held-out exclusion, altered bytes,
invalid batch limits, and a four-frame phrase split into batches of three and one.
No optimizer or acoustic target loading is implied; callers still own fresh
source/review admission and must discard a training attempt if later I/O fails.

Added optional phrase-sharded dataset assembly through the public CLI. Schema 3
retains only one expanded phrase while writing bounded, hash-referenced feature
files, and publishes the dataset manifest last. Total limits are 1M analysis
frames and 256 MiB per attempt, with 65,536 frames per phrase. Existing output
directories reject; incomplete attempts are retained without a final manifest.
Subprocess checks compare sharded features exactly with inline schema-2 output,
verify hashes/sizes, and ensure retries and snapshot/directory collisions reject.
Trainer-side shard consumption and checkpoint production remain open.

Connected the transform to actual `assemble-dataset` execution. Schema-2
snapshots carry source-sorted conditioning, its canonical digest in dataset
bindings, and the aggregate analysis frame count. API and subprocess coverage
inspect the generated phone/note/pitch/tail values and digest. Expansion is
preflight-bounded to 65,536 frames per compact snapshot; full-corpus sharding and
trainer consumption are explicitly unfinished, not bypassed by raising limits.

Added a bounded pure training feature transform mapping validated phoneme and
score intervals onto the existing F0 analysis clock. Preserved independent
phone/note timing, explicit rest versus MIDI-zero distinction, slur ownership,
expressive F0, and partial-tail sample counts. Unit coverage checks exact
boundaries, unchanged inputs, budget rejection and unresolved confidence issues.
No training run or learned singer is claimed; trainer consumption remains open.

## Reviewed dataset assembly CLI checkpoint

Connected `assemble-dataset` to fresh source-rights and schema-3 label admission,
captured configuration/review references, independently pinned policy anchors,
and deterministic leakage-aware splitting. The output retains configuration
identity and the earliest review expiry. Missing partitions or duplicate-selection
work publish an issue-bearing snapshot with exit 3; invalid inputs reject, and
existing snapshots cannot be overwritten. No training or release approval follows.

Verified the actual subprocess command against currently valid fixture-only
signatures: issue publication, exact configuration binding, no overwrite, changed
review digest rejection, and parent-path rejection. Focused CTest groups
`seam_voice_model_training_tests` and `seam_training_pitch_cli` both passed
(12.89 seconds total). These checks use test material, not a lawful production
corpus or independent musical approval. Real data acquisition, derived-clip
admission joining, training, export, and held-out singer qualification remain open.

## Neural bundle metadata compatibility follow-up

CLI-to-native child handoff: added `check_bundle_runtime.py` and trusted native
`--paired-bundle` experiment mode. The check converts exporter-style vocabulary
with the real CLI, prepares its manifest, inspects the byte-bound bundle offline,
then launches a separate native process that reloads/freeze-verifies the actual
directory before paired inference. Rebuilt runtime/CLI targets and the end-to-end
check passed. A same-length mutation of the vocoder after parent inspection is
rejected by child loading. This advances disk-byte revalidation but is not the
production worker protocol or production pre-session ONNX admission; all graphs
remain application-generated arithmetic fixtures, not learned singer weights.

CLI bundle preparation: added `prepare-neural-bundle DIRECTORY MODEL_ID
MODEL_VERSION MAX_PAYLOAD_BYTES` for four named regular assets. It reads bounded
bytes, builds/freeze-verifies a canonical manifest, validates metadata, creates
the manifest without overwriting, and reloads it before reporting
`DATA_BUNDLE_PREPARED_UNAPPROVED`. CLI integration CTest passed (1/1, 2.47 seconds),
including invalid-metadata/no-publication, exact manifest identity, prepare→
inspect and repeat/no-overwrite. Graph validity remains unclaimed. Assets must
remain stable during preparation; a failed post-publication reload retains the
manifest for diagnosis, not an automatic rollback or multi-file transaction.

CLI directory inspection: exposed `inspect-neural-bundle DIRECTORY MODEL_ID
MODEL_VERSION MANIFEST_SHA256 MAX_PAYLOAD_BYTES`. It uses native directory
loading and metadata inspection, reports verified identity/clock/vocabulary,
and explicitly returns `METADATA_INSPECTED_ONLY` with execution/release flags
false. Rebuilt CLI integration CTest passed (1/1, 3.65 seconds). Tests cover
invalid limits, changed asset rejection and successful metadata-only inspection
of deliberately non-ONNX graph placeholders, preventing a metadata PASS from
being described as executable admission. It performs no installation or writes.

Native directory intake: added `loadNeuralBundleDirectory`, reading bounded
`manifest.json`, validating all declared role/name/size/hash fields and aggregate
payload limits before asset reads, and freezing the exact loaded asset bytes.
Canonical manifest identity and payload hashes are rechecked by the freeze
factory. Initial rebuilt native protocol CTest passed (1/1, 1.12 seconds),
covering successful metadata inspection, wrong identity, payload limits,
changed-file rejection, cancellation and retained immutable bytes after disk
changes. Regular-file/non-symlink checks are point-in-time checks, not race-free
filesystem isolation; byte hashes remain authoritative and graph execution is
not admitted. Peak memory includes read buffers plus frozen copies, so payload
limits are not process-RSS limits. General importer UI and child-side invocation
are still open.

Native CLI preparation integration: added `convert-neural-vocabulary SOURCE_JSON
SOURCE_SHA256 NEW_OUTPUT_JSON` to the existing voicebank CLI. It reads bounded
source bytes, verifies the caller's expected digest, uses the native converter
and durably creates a new output without overwriting. Its report binds both
hashes and remains `CONVERTED_UNAPPROVED`/releaseEligible false. The rebuilt CLI
integration CTest passed (1/1, 2.90 seconds), including wrong-hash/no-output,
alias preservation, output hash, source preservation and no-overwrite cases.
This is a usable preparation action, not a full neural bank importer or release
approval. No Python or ONNX Runtime dependency was added to the CLI.

Converter differential verification: added a bounded native test-helper entry
and `check_vocabulary_parity.py`. The first run exposed differing canonical
bytes: native JSON is two-space-indented with a final newline; Python had emitted
compact JSON. Aligned Python output to the native writer. Sixteen byte-for-byte
comparisons now pass across shuffled maps, aliases, Unicode/escaped Unicode and
larger inventories; twelve invalid-source cases are rejected by both paths.
All 40 offline unit tests also passed. Regenerated converted vocabularies have
new content hashes compared with the earlier compact Python output; existing
serialized vocabulary decoding is unchanged, and bundle manifests must always
bind the actual bytes. This is converter parity, not complete bank admission.

Native vocabulary conversion: added `convertDiffSingerVocabulary` to the neural
library using bounded native JSON parsing. It preserves positive source IDs,
canonicalizes merged alias groups deterministically and rejects gaps, padding
collisions, malformed IDs/names and duplicate keys. Native protocol CTest passed
(1/1, 1.37 seconds). The paired native runtime fixture now uses this converter
before freezing its vocabulary, replacing its hand-authored SEAM vocabulary.
This provides the native conversion service; general bank-import UI, persisted
source provenance and actual learned-bank qualification remain unfinished.

Exporter vocabulary conversion: added `convert_vocabulary.py`, consuming bounded
phone-to-ID JSON and emitting canonical SEAM vocabulary v2 without reassigning
any positive trained ID. Merged aliases are retained, padding is reserved at
zero, sparse IDs are rejected rather than compacted, and output is rechecked
against the metadata parser envelope. Tests reconstruct the complete original
mapping, verify order-independent output, reject malformed mappings, and feed
converted bytes into hash-bound bundle inspection. This closes the offline
mapping conversion gap, not native importer wiring or learned model execution.

Exporter vocabulary compatibility: pinned DiffSinger `PhonemeDictionary.dump`
exports phone-to-ID JSON and merged groups can assign multiple phone names to
one positive ID (`utils/phoneme_utils.py:107-137,187-189`). Added SEAM vocabulary
v2 with canonical `tokens` plus `aliases` mapping names to existing nonpadding
IDs. Native vocabulary size now counts embedding tokens, not alias names.
Native protocol CTest passed (1/1, 1.12 seconds); all 36 offline tests passed,
including shared cross-language IDs and invalid alias collisions/targets.
Legacy vocabulary v1 remains unchanged. Exported-map conversion, language-ID
conditioning and actual learned-bank inference are still open.

Configuration v3 output binding: native/offline readers now require explicit
`vocoderOutput` (`audio` or `waveform`) in v3, retaining v1/v2 legacy semantics.
Offline bundle inspection passes this declaration to actual pair inspection;
native paired fixtures author v3 and reject a graph output name that differs
before inference. Native protocol CTest passed (1/1, 2.44 seconds), dynamic
paired runtime passed and all 35 offline tests passed. This supersedes the
unbound output-name limitation below. The fixture CLI deliberately selects two
fixed profiles (scalar/audio and vector1/waveform); it is not a general bank
importer or production admission interface, and does not execute learned vocals.

Exporter-source intake: cloned `openvpi/DiffSinger` into ignored
`build/neural-runtime/DiffSinger-source` and pinned inspection to
`336cf01b57f2ad44c6b37a79cf33993043291759`. No exporter/dependency/training code
or pretrained weights were executed. Its NSF-HiFiGAN exporter declares output
`waveform`, not our initial fixture's `audio`. Pair inspection now takes an
explicit allowed output name and binds it in the digest; native trusted-fixture
execution handles either declared output. Dynamic paired tests now exercise
`waveform` with vector1 steps. Bundle configuration still has no output-name
field, so its offline wrapper retains the legacy audio default pending a
versioned binding; no arbitrary bank execution is authorized.

Source: `deployment/exporters/nsf_hifigan_exporter.py` at the pinned revision,
input/output names and opset 17 in `_torch_export_model`. The acoustic exporter
also has optional language, speaker, variance, gender, velocity and depth paths.
These are not automatically covered by the minimal paired profile. The source
checkout includes an Apache-2.0 license; this is not evidence of permissions
for separately obtained voice weights, recordings or training datasets.

Native v2 execution binding: the paired runtime fixture now freezes explicit v2
spectral declarations and application-selected scalar/vector1 steps layout.
The actual graph steps rank must match that metadata before inference. Release
runtime target rebuilt and paired check passed: both layouts execute at both
sequence lengths, opposite declared layouts fail before Run, and wrong-hop
output fails afterward. This supersedes the v1 fixture limitation below.
It does not prove FFT/window/mel semantics from actual learned model behavior
or establish production graph admission and worker execution.

Configuration v2 follow-up: native and offline metadata readers now require
`fftSize`, `windowSize`, `melFrequencyScale` on both acoustic/vocoder feature
objects and root `stepsLayout`. FFT bounds are 2..32768; window must be positive,
no larger than FFT, and at least the hop. Frequency scale is explicitly Slaney
or HTK; steps layout is scalar or vector1. Pair declarations must match exactly.
Version 1 remains readable with unspecified spectral fields and a legacy scalar
layout; no missing values are inferred. Native protocol CTest passed (1/1,
2.96 seconds); all 33 offline tests passed, including actual vector1 graph
inspection through v2 bundle configuration. This supersedes the missing-field
and scalar-only bundle limitations below, but not graph/runtime compatibility
or execution admission. The native arithmetic fixture still authors v1 metadata.

Source-backed exporter correction: inspected the existing OpenUtau checkout at
`8c0dc4007e6e8c8181f3a12c10205671800eeb8b`. Its
`OpenUtau.Core/DiffSinger/DiffSingerRenderer.cs:275` constructs continuous
acceleration `steps` as int64 `[1]`, not the scalar used in our initial fixture.
Offline pair inspection now accepts an explicit `steps_layout` choice, binds
it in the contract digest and rejects mismatched ranks. Native fixture execution
inspects the actual steps input and supports exactly scalar or `[1]`. Both
layouts passed dynamic paired execution; wrong-hop output still fails. All 31
offline tests passed. Bundle configuration currently has no steps-layout field,
so its existing inspector retains scalar behavior; production schema binding
must be completed rather than inferring rank silently.

The same source review found missing metadata dimensions in our configuration:
FFT size, analysis-window size and mel-frequency scale (Slaney versus HTK).
OpenUtau checks these in addition to sample rate, hop, mel bins and frequency
range. Our current metadata cannot establish their compatibility. This is an
open admission requirement, not justification to accept an arbitrary exported
pair. No learned model assets were found in the checked project paths.

Offline JSON-bound follow-up: added a quote/escape-aware container-depth check
before recursive decoding and post-decode node, collection, finite-number and
128-byte UTF-8 string checks. Configuration uses native 128-node/16-entry
limits; vocabulary uses its separate larger limits. All 30 offline inspection
tests passed, including 4000-level nesting rejection, braces within strings,
numeric overflow and collection/node limits. Post-decode limits do not bound
peak allocation, and no hard parser-process memory ceiling is claimed.

Neural process-budget propagation: source inspection found the neural runner
did not forward the platform helper's resident-memory/CPU limits. Added
application-owned run options and forwarded both fields. Actual child probes
verify resident-memory and CPU-time termination with their specific diagnostics;
negative CPU limits are rejected. Rebuilt neural protocol CTest passed (1/1,
2.63 seconds). Zero defaults retain legacy v1 behavior; production admission
still must choose measured nonzero budgets. Sampling remains best-effort and
is not an OS sandbox or Windows/installed-host qualification.

Frozen-identity runtime integration: the paired native experiment now freezes
its two loaded graphs plus fixture configuration/vocabulary through the actual
`FrozenNeuralBundle` factory, inspects native metadata, and constructs ORT
sessions from the frozen graph spans. Requests use the real manifest-derived
model digest and frozen vocabulary, replacing the placeholder model hash.
Release runtime target rebuilt and dynamic paired execution passed, including
the wrong-hop rejection case. Fixture configuration remains application-authored
and fixed to this experiment; this does not implement arbitrary bank import,
production pre-session graph admission, signed worker launch or learned vocals.

Vocabulary policy reconciliation: source comparison found offline intake had
allowed 256-byte/control-character tokens while native decoding limits tokens
to 128 UTF-8 bytes and excludes C0/DEL. Offline intake also incorrectly capped
vocabulary entries at 4096 rather than native decoding's 65536 (phone-span limits
are separate). Corrected those policies. All 26 offline tests passed, including
UTF-8 byte boundaries and a 4097-entry vocabulary. Mirrored native boundary and
control-character regression cases passed in the rebuilt neural protocol CTest
(1/1, 7.54 seconds). This is targeted vocabulary-policy reconciliation, not a
claim of complete differential parity for all metadata or graph admission.

Offline bundle-binding follow-up: `inspect_bundle.py` accepts only immutable
manifest/asset bytes and an expected manifest digest, verifies asset hash/size
closure and required roles, then derives graph-pair parameters from the actual
configuration bytes and checks the vocabulary. Six new tests cover success,
changed bytes, wrong manifest identity, unlisted assets, duplicate JSON and a
rehashed configuration that contradicts graph mel bins. All 23 offline
inspection tests passed. This closes the caller-supplied-parameter gap in the
offline tool only; runtime enforcement, cross-language parity and executable
admission remain open. Optional variance/tensor roles are explicitly unsupported
by this initial paired execution profile, not removed from the full plan.

Response-path follow-up after the full-suite checkpoint below: shared
`finalizeDiffSingerResponse` performs worker-side trim/gain finalization,
constructs the normalized response and binds it to the canonical request hash.
The native paired experiment now round-trips the actual response codec too,
checking request binding and exactly-once gain. Tests verify changed dynamics
change both returned PCM and the request digest, and reject an empty backend
identity. Release neural/runtime targets built; neural protocol CTest passed
(1/1, 0.94 seconds), followed by the dynamic native paired check. The existing
receiving backend was inspected and does not apply another dynamics pass.
No fresh full-suite claim for this follow-up, and no actual production helper
launch, admitted learned model or normal song integration is established.

Integrated verification checkpoint: complete Release build passed, followed by
a fresh full CTest run: 124/124 passed in 293.51 seconds. Separately, all 17
offline graph/pair inspection tests and both native runtime experiments passed
again against the rebuilt binary. This supersedes the earlier focused-only
regression boundary for this accumulated change set. It does not qualify a
learned singer, production graph admission, Windows execution, actual installed
host matrix, human listening or full Beta GO. Linker duplicate-library warnings
were present; the build completed successfully.

Request-conditioning integration follow-up: the native paired probe now links
the real neural library, serializes/deserializes a sample-domain request and
uses `prepareDiffSingerAcousticInputs` rather than handwritten tensors. Both
runtime cases exercise a 37-sample partial-hop tail. New shared
`finalizeDiffSingerAudio` validates the entire padded mono buffer, trims to
the request count and applies sample-domain dynamics once, rejecting invalid
raw/tail PCM or gain overflow instead of clipping. Release targets built;
neural protocol CTest passed (1/1, 0.89 seconds), including finalizer shape,
tail, gain, identity and cancellation cases; dynamic native paired check
passed. This supersedes the earlier manual-tensor limitation, not the open
production-worker, real model, graph admission or normal song integration.

Native paired execution follow-up: the optional runtime probe now executes
tokens/durations/f0/scalar-steps acoustic inputs, checks finite `[1,T,80]` mel,
passes mel plus f0 into a second native session, and checks finite `[1,T*256]`
audio with expected values. The generated dynamic arithmetic pair passed at
T=3 and T=5 using the same sessions. A pair with identical declared interfaces
but an actual 128-sample hop passed offline inspection and was correctly
rejected at runtime for output shape. `check_paired_runtime.py` passed after
the Release probe rebuild. This remains a controlled integration experiment:
no learned singer, normal song request bridge or production worker is claimed.

Paired-interface follow-up: `inspect_pair.py` directly inspects both graph byte
strings against a proposed SEAM export profile matching the existing prepared
acoustic inputs (tokens/durations/f0/steps) and a pitch-conditioned vocoder.
It checks names, dtypes, ranks, intra-graph axis relationships, mel layout/bins
and bounded maximum mel-buffer sizing; its contract digest binds graph hashes
and supplied parameters. Eight structural tests passed. This is not universal
DiffSinger compatibility or production execution admission. Constant graph
fixtures do not prove actual output shapes, hop timing, learned conditioning,
musical quality, or that the supplied parameters match frozen configuration.

Offline pre-runtime intake now exists in `tools/neural_runtime/inspect_graph.py`.
It parses bounded bytes without external-data resolution, recursively rejects
external tensors/custom operators (including nested graphs and attributes),
checks standard ONNX structure, and reports hash-bound actual interfaces.
Nine focused Python tests passed, covering the trusted graph report, nested
rejections, oversized dimension product, custom imports, empty input and
truthful reporting of unresolved dynamic dimensions. This is not production
admission: model-family bounds, execution budgets and child-side enforcement
remain open. The positive arithmetic fixture runner now invokes intake before
native inference; direct native invocation remains trusted-fixture-only.

Native-runtime follow-up: the optional arithmetic probe now reads bounded owned
graph bytes once (16 MiB each), creates sessions from memory and checks actual
session tensor names, float32 dtype and exact fixture rank/dimensions before
inference. The manual Python/native check passed with repeated correct output,
wrong-scale rejection, swapped graph rejection, wrong shape/rank/dtype and
dynamic-dimension rejection, plus empty/oversized input rejection. This is
post-parse runtime introspection of trusted generated fixtures, not pre-session
operator admission, external-tensor safety, DiffSinger execution or singing.
The production pre-session graph inspection requirement remains open.

Added `inspectNeuralBundleMetadata` over immutable frozen bundle bytes. It
binds the vocabulary digest and model identity, parses bounded configuration,
and requires acoustic/vocoder agreement on sample rate, hop, mel bins, layout,
amplitude encoding, multiplier, offset and frequency range. Unknown fields and
invalid numeric domains are rejected even when both declarations match.

The configuration schema is `com.project-seam.neural-bundle-configuration`
version 1. Its exact root fields are `formatId`, `schemaVersion`,
`maximumFrames`, `acousticFeatures` and `vocoderFeatures`. Both feature objects
contain `sampleRate`, `hopSize`, `bins`, `layout`, `amplitudeScale`,
`multiplier`, `offset`, `minimumHz` and `maximumHz`. This internal declaration
schema is not yet the complete production model/export contract.

Verification: Release neural protocol target built; its CTest passed (1/1,
3.39 seconds), including independent feature mismatches, matching invalid
declarations, frame bounds, unknown executable field, invalid vocabulary and
cancellation. No full-suite or graph admission result is claimed.

This API deliberately returns metadata, not executable admission. The test
uses non-ONNX graph placeholders: passing it proves no graph compatibility.
Next: bounded actual graph inspection, immutable executable admission,
application-selected bundle transport and child-side byte re-admission, then
production acoustic/vocoder execution. Windows supervision, model training,
rights and musical qualification remain open under the original plan.

## Active outcome: M1 original voice → bank → unfamiliar song

| Package | Implementation | Demonstrated workflow | Qualification remaining | Next action |
|---|---|---|---|---|
| M1.P1 | Implemented: inventory v2→producer v4; language-bound generation; multi-style draft/review/publication; legacy readers retained; durable evidence-backed legacy migration with a retained plan and parity against the planner | Two-style initialization, generation collection, native-controller draft creation, review and candidate reopen regressions, plus the migration suite and the planner-to-CLI parity case | None outstanding for this package's required changes | Campaign and articulation work continues in M1.P2/P3 |
| M1.P2/P3 | Not completed by this increment | Existing procedural/producer foundation retained | Connected articulation, campaign, actual bank and unfamiliar-song evidence | Continue after the necessary M1.P1 producer bindings |
| M2–M6 | Remaining full scope retained | No new milestone qualification | As specified by the implementation plan | Independent neural process/data work remains available |

## Verification checkpoint — September 13, 2026

- Nine draft tests plus eight legacy inventory tests passed (17 total), including
  real CLI generation, exact numeric types, bounded IDs and legacy-writer rejection.
- CMake configure succeeded and registered `seam_draft_inventory_tests` passed.
- The focused CTest target was rerun after the bounded-ID case was added.
- The producer now defines `ProductionUnitIdentity` with exact language/style/
  coverage/layer equality and a canonical inventory-v2 SHA256. Python-generated
  rows and C++ agree on ASCII and quoted Japanese-label golden vectors; distinct
  style slugs cannot merge assignments.
- Producer schema 4 now persists a workspace language and per-assignment/take
  style, checks four-axis duplicate/retake ownership, and retains legacy 1–3
  serialization. Raw and generated import matching use style; source assessment
  retains v4 and includes language/style in its material identity. Review and
  single-style manifest paths reject relabeling. The later publication checkpoint
  below admits complete schema-4 style matrices through the canonical publisher.
- Tests import identical PCM into two distinct styles, recover the durable
  workspace, and reject cross-style retakes, missing styles, language changes,
  and relabeling existing takes without mutating saved state. Generic save cannot
  masquerade as legacy migration; the explicit migration operation is pending.
- At checkpoint `5291e652`, rebuilt the complete configured Release tree and ran all **122 CTest targets:
  122 passed, zero failed** (86.13 seconds). The producer target now has 49 cases.
- Following that checkpoint, score-job preparation and CLI collection now carry
  assignment style. Expectation v2 carries explicit language; legacy expectation
  v1 retains its format. Tests prepare/load/render a schema-4 job, collect it via
  the actual CLI, and repeat collection without creating another generation.
  Wrong-style preparation and wrong-language collection fail without changing
  producer state; malformed v2 language/version fields are rejected.
- Rebuilt the complete Release tree after the generation integration. The three
  focused CTest targets (export workflow, draft inventory, producer) passed.
  The 122-target run above predates this latest integration, not a fresh claim.
- Version-aware Python definition preparation now maps inventory v2 to producer
  v4 without repeating workspace language in assignment rows. C++ init-production
  admits empty v4 drafts and still rejects preapproved/imported material. Python
  verification checks style keys, retake binding, immutable language/take identity
  and schema-4 source-quality material hashes. Legacy generic inventory readers
  remain unchanged.
- New parity tests execute both preparation and initialization CLIs, then verify
  the resulting two-style workspace in Python. Missing language/style, duplicate
  assignments and schema relabeling are rejected by both readers. All 33 focused
  Python tests passed; registered draft inventory, external-beta Python contract
  and sample-review CLI tests also passed. This is not exhaustive full-product
  or populated multi-style publication qualification.
- No generated voice, musical review, qualified range, installed bank or Beta GO
  is claimed. The complete implementation goal remains active.

## Multi-style review integration

- Schema-4 workspaces can now prepare and apply a multi-style review packet
  through the shared review service. Legacy style-free workspaces remain rejected.
- A synthetic regression uses two takes with identical PCM, phone coverage and
  pitch but different styles. Reviewing the first does not review the second;
  the second requires a fresh packet and its own explicit decision. Stale packets
  and relabeling into the other style fail, and durable recovery retains both
  separate review records.
- Producer, native Studio review and sample-review CLI targets rebuilt and all
  three focused CTest targets passed. The producer target has 50 cases.
- This review checkpoint alone did not admit publication. See the subsequent
  publication integration below. No musical approval outside the explicitly
  synthetic tests was created.

## Multi-style candidate publication integration

- The canonical publisher now admits schema-4 multi-style workspaces only when
  declared styles exactly match assignment ownership, all styles have the same
  required phone/pitch matrix, and every current assignment has its own valid
  take and retained independent review. Legacy workspaces keep the single-style
  restriction. No approval is inferred from shared PCM.
- Regression coverage publishes and reopens a two-style candidate, verifies its
  manifest and content hash, and rejects omitted styles, reused review evidence,
  extra declared styles and asymmetric phone/pitch requirements.
- Schema-4 source qualification now requires an explicit current source-quality
  assessment. C++ and Python no longer allow the historical no-assessment fallback
  for these workspaces. Source execution remains separate and does not need a
  musical PASS. Test evidence is expressly synthetic, not a real evaluation.
- Scope clarification from code inspection: Python `_production_candidate.py`
  validates a separate legacy `READY`/`unitBindings` export contract, not the
  canonical C++ `com.project-seam.resource-candidate` descriptor. Its pair-based
  legacy contract is not being reinterpreted as authority for these candidates.
- This remains an engineering candidate with `releaseEligible: false`. Native
  multi-style draft authoring, explicit legacy migration, generation campaigns,
  real singer quality and the remaining M1–M6 obligations are still unfinished.
- Verification after this integration: complete configured Release build passed;
  **122/122 registered CTest targets passed**, zero failures (82.22 seconds).

## Native multi-style draft creation

- The shared draft builder now derives all schema-4 styles from assignments and
  generates style-distinct unit IDs. Missing takes retain style-qualified labels.
  The selected identity style must belong to the workspace; it cannot relabel or
  filter its other assignments. Legacy workspaces retain explicit single-style
  behavior. CLI help and Studio's progress status explain the all-styles behavior.
- Tests create a partial and complete two-style draft, prepare it for review,
  reject an unknown style, and prove that selecting either existing style retains
  identical manifest content. A native-controller test asynchronously creates and
  opens both styles without changing producer state or creating reviews.
- Shared manifest, Studio manifest and sample-review CLI targets passed after
  rebuilding affected targets. The prior 122-target run predates this increment;
  no fresh desktop visual QA or musical qualification is claimed.

## Legacy migration preparation

- Added `python3 -m tools.external_beta.voicebank_production prepare-style-migration
  --workspace WORKSPACE --inventory LEGACY_INVENTORY --output NEW_PLAN_JSON`.
  It verifies durable history and matching inventory, captures the source bytes'
  SHA256/generation and inventory evidence, and writes only a new plan outside
  the workspace. Existing output paths are not overwritten.
- A singleton style in the validated legacy inventory can resolve ownership.
  Multi-style legacy inventories produce `UNRESOLVED` with per-assignment reasons;
  the planner does not guess a selected style. A resolved proposal retains old
  reviews/source bindings but clears active marker/pitch approval and requires
  source-quality reassessment. Its generation is not advanced by the planner.
- All 18 production-draft parity tests passed, including actual CLI invocation,
  deterministic proposal content, ambiguous styles, unchanged workspace bytes,
  preservation of historical evidence and rejection of in-workspace/overwrite
  destinations. The proposed state is validated against the target schema.
- **Not yet applied:** the C++ durable migration operation, retained migration
  receipt and history-transition verification remain to implement. Generic save
  continues to reject a schema upgrade; this plan cannot bypass that boundary.

## M1.P2 audible pilot started

- Added a reproducible `seam_singer_pilot` executable using the existing production
  export path, not a separate DSP implementation. It creates saved scores, editable
  recipes, master WAVs and unapproved baked candidates for a six-note Japanese
  vowel/fricative ladder and three phonation/formant variants.
- Retained complete local outputs in `build/release/seam-pilot-listening-02/`.
  Master peaks are about 0.0645; RMS spans 0.0126–0.0221. Distinct variant hashes
  establish different PCM, not perceived improvement or female identity.
- The first run exposed a harness metadata-as-WAV measurement error; fixed it
  and preserved the partial directory. Registered a real-CLI test for repeated
  identical audio hashes, finite/nonzero unclipped diagnostic output, distinct
  recipe identities and refusal to overwrite an existing destination. It passed.
- No listening verdict, complete articulation coverage, installed pilot bank or
  unfamiliar-song acceptance is claimed. Next: analyze the retained phrase timing,
  pitch and transitions, expand consonant/context probes, and connect inventory
  campaign generation. Explicit legacy migration remains unfinished but does not
  prevent the new-workspace pilot work.

### Pilot steady-pitch measurement

- The pilot now emits per-variant pitch diagnostics tied to the dry candidate's
  SHA256. It uses the existing broad-range FFT pitch analyzer, with fixed central
  half-note windows derived from the project tempo map. Full analysis windows
  must fit inside those intervals. Unvoiced frames remain in the denominator;
  missing voiced estimates produce null medians, not zero error.
- Retained run `build/release/seam-pilot-listening-03/`: the baseline has 92
  analyzed windows, all voiced and within 50 cents; per-note median absolute
  errors range from 0.052 to 0.350 cents. This supports steady-pitch behavior for
  this six-note fixture only, not transitions, timing-edit accuracy, language
  intelligibility, singer identity or Beta qualification.
- Rebuilt the pilot and passed its real-CLI regression (1/1), now checking
  diagnostic hash binding, denominators and baseline pitch. No full-suite rerun
  is claimed. Next synthesis investigation should prioritize consonant/context
  transitions and articulation coverage over steady-pitch changes.

### Expanded articulation listening fixture

- Added an explicit `articulation` pilot mode: fourteen Japanese CV notes,
  `ma mi mu me mo na ni nu ne no pa ta ka sa`, rendered through ordinary export
  in the same three variants. Recipes explicitly bind nasal resonance and
  antiresonance for m/n, separate released-stop bursts for p/t/k, and s noise.
  This extends the diagnostic recipe, not the renderer's supported source types.
- Complete local audio/scores/recipes/markers/pitch diagnostics are retained at
  `build/release/seam-pilot-articulation-01/`. No auditory verdict is asserted.
- The real CLI regression now repeats both fixtures and verifies exact 28-phone
  coverage, four gesture classes, ordered contiguous planned boundaries, complete
  candidate span, actual SHA256 binding and unapproved state. Rebuilt executable
  and focused CTest passed (1/1, 2.85 seconds); no full-suite run claimed.
- M1.P2 remains open: context/transition semantics, remaining consonant families,
  held-out linguistic phrases and resumable inventory generation are not supplied
  by this diagnostic. Prioritize those gaps rather than treating marker coverage
  or steady vowel pitch as proof of an intelligible singer.

### Inventory assignment to real generation job

- Added shared `inventory_generation.hpp/.cpp`: deterministic template-v1 score
  construction from a unique schema-4 Japanese producer assignment. The score
  retains canonical coverage phones as an explicit phonetic hint, assignment
  pitch/style, stable IDs and a template hash. It uses one 960-tick note at the
  default 120 BPM; this is an initial timing template, not complete context design.
- `prepareInventoryGenerationJob` writes a create-new score and delegates to
  `prepareGenerationJobFromScore` with its exact hash and selected recipe. Job IDs
  include take identity; expectations capture producer state at preparation time.
  No producer mutation, automatic approval or alternate renderer is introduced.
- The integration test uses a durably initialized synthetic producer, prepares
  `cv:s:a`, renders the actual job, verifies ownership and unchanged producer
  bytes, and rejects duplicate output, wrong style, duplicate assignment identity,
  unsupported language and `release:a:R` (unsupported adapter phone). The initial
  test accidentally used generation zero; its rejection was retained as a fixture
  correction, not bypassed in production. Export CTest passed, 1/1 (4.45 seconds).
- Still required: inventory-file admission/CLI, coverage-wide unsupported-context
  reporting, additional timing templates and resumable campaign prepare/render/
  collect receipts. Do not prepare an entire campaign's expectations up front.
- Inspection also found `generation_batch.cpp` still deduplicates assignments by
  coverage/pitch without language/style. Repair and test this before admitting
  a multi-style campaign; this increment does not claim that path complete.

### Multi-style generation batch repair

- Repaired the preceding batch-admission gap: version-2 expectations now use
  `ProductionUnitIdentity` (language/style/coverage/pitch). Legacy expectations
  still use their original style-free identity; recipe style cannot create a
  second legacy assignment. Duplicate job/take and frame-budget checks remain.
- A real integration fixture initializes two same-phone/same-pitch assignments,
  prepares each through the inventory score builder, admits/renders the batch,
  saves its manifest, collects both atomically and reopens the durable producer.
  Both styles survive; neither assignment gains marker/pitch approval. Repeated
  job references and a budget one frame below the required total are rejected.
- Focused export CTest passed (1/1, 3.96 seconds). Complete configured Release
  build also passed. This closes the batch identity mismatch, not the campaign
  scheduler, restart receipts or real singer qualification.
- Full configured regression after this repair: **123/123 CTest targets passed**,
  zero failures, 87.81 seconds. This also covers the intervening inventory-score
  and listening-pilot increments; it does not stand in for installed-host or
  independent musical acceptance.

### Bounded immutable campaign planning

- Added shared `generation_campaign.hpp/.cpp`. `planGenerationCampaign` accepts
  explicit planned take IDs, validates the producer and frozen recipe, constructs
  each inventory template and preflights it through the normal procedural snapshot
  compiler. It keeps only one temporary compiled snapshot at a time. Unsupported
  takes fail with the take ID; there is no truncation or substitute silence.
- Definitions capture exact initial producer JSON/hash (including inventory and
  source-policy evidence), recipe JSON/hash, score JSON/template identity, ordered
  take/style/coverage/pitch rows, frame totals and deterministic batch membership.
  Input order does not affect the canonical definition. Every job is UNPREPARED;
  no generation expectation is captured or producer/filesystem state changed.
- Admission retains the existing 64-job/32M-frame batch ceiling and applies
  aggregate job/frame/estimated-byte limits. Disk numbers are conservative planning
  allowances, not measured filesystem quotas. Definition serialization is bounded
  to 32 MiB. Cancellation is checked before and between template compilation.
- `verifyGenerationCampaign` requires the supplied digest, decodes frozen inputs,
  reconstructs the canonical plan and compares exact bytes. Changed totals,
  batch layouts, hidden expectation fields and numeric type spoofing fail even
  with a recomputed outer digest. This proves internal consistency against the
  selected digest, not authority to replace that digest or source approvals.
- Focused Release build and export CTest passed (1/1, 4.44 seconds), covering
  deterministic two-batch planning, resource mismatch, budgets, cancellation and
  adversarial definitions alongside real two-style generation/collection. The
  prior 123-target run predates this increment; no new full-suite run claimed.
- Still open: create/inspect CLI publication, filesystem quota enforcement during
  execution, just-in-time batch preparation, durable advancement/commit receipts,
  external-edit detection and crash/restart integration. A valid plan is not a
  completed or resumable campaign yet.

### Campaign CLI publication and inspection

- Added separate CLI command module, using shared planning/verification services:
  `draft-generation-campaign WORKSPACE RECIPE NEW_PLAN TAKE...`,
  `plan-generation-campaign WORKSPACE PLAN HASH NEW_DIRECTORY`, and
  `inspect-generation-campaign CAMPAIGN HASH`. Drafting uses default limits and
  explicit selected take IDs; publication retains the exact caller-selected
  plan bytes/hash, not regenerated expectations or a changed recipe.
- Plan publication recovers the current producer and rejects a changed initial
  state before creating output. The new directory's atomic `campaign.json` is the
  publication boundary; partial directories are retained and never overwritten.
  This is a point-in-time state check, not a workspace lock or authorization to
  run later without rechecking. Inspection proves frozen-plan consistency only.
- Real CLI tests draft, inspect and publish, reject repeated destinations and a
  wrong hash, verify unchanged producer bytes, then collect real two-style output
  and confirm stale publication fails before directory creation.
- Advancement remains unimplemented: just-in-time preparation and durable
  completion/recovery receipts must precede any resumable-execution claim.
- Verification: focused Release build and export CTest passed (1/1, 3.92 seconds).
  No fresh full-suite result or musical acceptance is claimed.

### Recoverable batch collection checkpoint

- Added `collectGenerationBatchWithReceipt`, a shared transaction building block
  for campaign advancement. It requires the original producer snapshot and frozen
  job references, uses a persistent exclusive receipt lock, validates ownership
  and budgets, then delegates atomic collection to the existing repository.
- Retry recognizes every original expectation through retained producer lineage.
  Partial recognition, changed initial state, an extra producer generation, or a
  conflicting receipt fails. Exact receipt retries neither collect nor generate
  again. Receipt bytes bind original/committed producer hashes, generation, take
  audio hashes and expectation hashes; they are create-new and never overwritten.
- Tests inject an interruption after the real two-style commit but before receipt
  publication, hide both temporary output directories, then recover the receipt
  from producer-owned assets without regenerating audio or advancing generation.
  Repeated recovery preserves receipt bytes; false receipts and an unrelated
  later producer save are rejected.
- Important remaining durability boundary: repository read-only recovery does not
  re-fsync its mutable current pointer. Recovered results therefore deliberately
  return `durabilityConfirmed=false` with a diagnostic, even though the receipt
  file itself is durably written. Add locked exact-generation pointer
  reconciliation before permitting the next campaign batch. Do not interpret
  successful recognition as permission to skip this boundary.
- This is not yet the complete campaign runner: batch preparation/resume, pointer
  reconciliation, advancement CLI and broader process-crash tests remain open.
- Verification: affected Release targets rebuilt and export CTest passed (1/1,
  4.30 seconds). No new complete-suite run claimed.

### Exact current-pointer reconciliation

- Added repository `reconcileCurrentPointer(expectedGeneration, expectedHash)`.
  It takes the existing workspace writer lock, refuses newer occupied generation
  or journal records, verifies recovered state against both supplied identities,
  and durably republishes `project.json`. It does not append generations, alter
  immutable record contents or change reviews/source policy. Cancellation is
  checked before locking and at the last pre-publication boundary.
- Batch receipt recovery now invokes this operation after recognizing the exact
  committed requests. This supersedes the preceding temporary unconfirmed-pointer
  result: recovery returns confirmed only after exact locked pointer publication
  succeeds. An error still leaves the committed take recoverable, never reimports.
- The integration fixture damages the pointer after the injected post-commit
  interruption. Wrong hash, cancellation and a competing writer are rejected;
  normal retry restores the exact pointer hash without adding a generation or
  regenerating hidden output. The existing later-external-change rejection stays.
- This verifies the pointer repair path, not arbitrary power-loss behavior across
  filesystems/platforms. Campaign batch preparation and advancement are still open.
- Affected Release targets rebuilt and focused export CTest passed (1/1, 3.89
  seconds). No fresh complete-suite or installed-host result claimed.

### Explicit generation preparation recovery

- Campaign integration exposed a prerequisite: existing job preparation required
  a brand-new directory and could not resume partially written inputs. New job
  preparation now publishes `preparation.json` first, containing the exact future
  manifest and its score/recipe/expectation digests. It then publishes inputs,
  reference and final `job.json` in that order under a preparation lock.
- Added explicit `resumeGenerationJobPreparation` using the original snapshot,
  producer and take. Recomputed intent must match exactly. All existing named
  files are checked for regular-file status and exact bytes before any missing
  file is written. Changed producer expectations, conflicting content, symlinks
  and directories without intent are not adopted. No files are overwritten.
- Existing prepare APIs retain create-new behavior. Existing complete jobs remain
  readable without the new intent. An old partial job or interruption between
  directory creation and intent publication remains unowned and is not silently
  repaired; campaign-level recovery must preserve that artifact explicitly.
- Tests retain a complete job, hide score/expectation/final manifest, reject a
  changed producer and tampered recipe before filling any gaps, then restore the
  exact inputs and recover the original manifest/expectation hashes. Repeating
  explicit resume succeeds while ordinary prepare and unowned-directory resume
  remain rejected. These are interruption-state fixtures, not OS process-kill tests.
- This is the job-preparation building block, not yet campaign batch advancement.
- Verification: affected Release build and focused export CTest passed (1/1,
  4.36 seconds). No fresh complete-suite run claimed.

### Just-in-time campaign batch preparation integration

- Added shared `prepareGenerationCampaignBatch`. It verifies the immutable plan,
  bounds the batch index, matches batch 0 to the exact initial producer, and
  requires a confirmed predecessor receipt/current-producer hash and expected
  generation offset for every later batch. This is an internal service: callers
  must supply a verified collection result, not trust arbitrary receipt JSON.
- Each batch retains its original producer JSON and campaign/index identity under
  a preparation lock. Selected score templates must still exactly match frozen
  plan bytes. Job creation/resume uses the retained-intent preparation service;
  batch manifests are create-new or verified against the exact prepared job list.
  No rendering, collection or approval happens during batch preparation.
- A real two-batch integration now plans two styles, prepares/retries batch 0,
  renders and collects it, recovers the producer, prepares batch 1 using the first
  confirmed receipt, renders and collects batch 1, and reopens both takes. The
  second job's expectation binds the first commit's hash, explicitly not the
  initial hash. Premature batch 1 and stale batch 0 are rejected before creating
  their directories. This demonstrates sequential preparation, not only planning.
- Focused Release build and export CTest passed (1/1, 3.87 seconds). No new full
  suite run is claimed. The persisted advancement controller/CLI, authoritative
  receipt-chain loading, runtime disk quotas, crash-window handling before intent
  publication, and OS process-kill tests remain open. Do not label this a complete
  resumable campaign runner yet.

### Repository-backed historical receipt verification

- Added exact-hash `recoverGeneration` and optional historical generation/hash
  arguments to `findCollectedGeneration`. Historical reads validate the requested
  immutable generation and its normal repository evidence; they never move the
  current pointer or silently substitute a newer/older recoverable snapshot.
- Added `loadVerifiedGenerationBatchReceipt`. It verifies the original producer
  against stored history, checks frozen job expectations, resolves the exact next
  generation, validates take lineage/audio identities in that historical state,
  and reconstructs canonical receipt bytes. Unknown fields, altered values or
  a valid hash belonging to the wrong generation are rejected.
- The two-batch test verifies batch 0's saved receipt after batch 1 has committed:
  the returned historical state contains one take, while the current pointer and
  two-take workspace remain untouched. Negative cases replace the committed state
  hash with the latest generation hash, forge an audio hash, insert an unknown
  field, or request a missing/wrong-hash historical generation.
- This supplies authoritative persisted-receipt loading for the forthcoming
  advancement loop. Historical verification alone does not assert currentness or
  restore a pointer; the loop must compare its final state with the current
  producer and use exact reconciliation before further mutations.
- Verification: affected Release build and focused export CTest passed (1/1,
  4.22 seconds). No fresh complete-suite run claimed.

### Persisted campaign advancement loop and CLI

- Added `advanceGenerationCampaign` and the planned CLI form:
  `advance-generation-campaign WORKSPACE CAMPAIGN_JSON CAMPAIGN_SHA256 OPERATOR UTC`.
  The immutable campaign and repository history are authoritative; there is no
  mutable unchecked progress counter. A campaign lock serializes invocations.
- The loop reconstructs completed batch state using verified historical receipts
  and exact frozen templates. For the first incomplete batch it requires the
  expected producer state, prepares/resumes original jobs, renders only if the
  requests have not already been collected, and atomically collects with a durable
  receipt. Each invocation advances at most one incomplete batch.
- A producer one generation ahead is considered only when retained batch inputs
  exist, and original-request recognition must prove every take before skipping
  rendering. This handles commit-before-receipt interruption without rebinding
  expectations. Partial collection and unrelated changes fail. A completed retry
  verifies the whole chain and requires current producer equality.
- CLI reports `BATCH_COLLECTED` or `COLLECTED_UNREVIEWED`, always with
  `releaseEligible:false`. It does not manufacture source rights, independent
  reviews, qualified singer resources or release approval.
- Integration tests inject a post-commit/pre-receipt interruption in batch 0,
  recover it with one take still present, advance batch 1 through the real CLI,
  verify two takes, repeat without a generation change, then reject an external
  producer save. Focused export CTest passed (1/1, 4.20 seconds).
- Remaining hardening: directory-created/intent-not-yet-published recovery,
  hard runtime disk quota accounting, process-kill/cancellation coverage and
  large-campaign performance. Current completed-batch traversal revalidates plans
  repeatedly; measure and eliminate redundant compilation before singer-scale
  campaigns. This pilot-scale working loop does not close all M1.P2 obligations.
- Full configured Release build passed. Full CTest run: 122 passed, one failed
  (84.18 seconds); source closure correctly reported the new advancement source
  was not yet indexed. After staging that exact file, the source-closure target
  passed (1/1, 0.22 seconds). No code changed between those runs; all 123 targets
  now have passing evidence, but no second all-green full invocation is claimed.

### Reuse immutable campaign admission during traversal

- Added `VerifiedGenerationCampaign`: callers cannot construct it from unchecked
  JSON. Admission performs the existing digest/canonical reconstruction checks and
  owns an immutable parsed plan. Copies share that plan; changing the source text
  after admission cannot change its content or digest. Moved-from handles are
  rejected by preparation before filesystem writes.
- Advancement now admits once and passes the handle through each visited batch.
  Previously it performed a full-plan verification initially and again for every
  batch; each verification compiled every campaign template. This change removes
  those repeated whole-plan preflights without removing per-batch producer,
  predecessor, frozen-score, prepared-job or repository-history checks. Existing
  string-based preparation remains a wrapper that fully admits its input.
- Tests exercise shared immutable ownership, source-buffer replacement, wrong
  digest and moved-from rejection, then run the existing two-batch/CLI recovery
  flow through the admitted-handle path. This is a structural reduction in repeated
  work; no singer-scale wall-clock speedup is claimed without a benchmark.
- Remaining scaling work includes indexing selected rows and reducing repeated
  historical job loads. Runtime disk bounds and OS process-crash qualification
  also remain open; this change does not complete M1.P2.
- Verification: affected Release build and export CTest passed (1/1, 4.55 seconds).
  No new complete-suite run claimed.

### Real process termination at campaign commit boundary

- Extended the existing generation test helper with a campaign mode that raises
  SIGKILL after the producer commit and before collection-receipt publication.
  The parent uses actual process wait status and requires termination by SIGKILL,
  rather than accepting a generic failure or an injected Result error. The signal
  path is enabled on macOS/Linux; this run was on macOS.
- The fixture confirms one committed take and no receipt, moves the temporary
  output directory aside, then retries through the normal advancement CLI. Retry
  succeeds with exactly unchanged producer bytes and batch manifest hash and does
  not recreate output. It then advances the second batch and retains the existing
  completed-retry and external-edit rejection checks. This also exercises release
  of campaign/receipt OS locks when destructors cannot run.
- Focused Release build and export CTest passed (1/1, 4.40 seconds). This is real
  process-death evidence for the post-commit/pre-receipt window only, not a machine
  power-loss test or complete campaign crash matrix. Preparation-intent windows,
  cancellation during rendering, storage bounds and Windows qualification remain.

### Bounded retained-storage inspection and phase guards

- Added shared `inspectCampaignStorage`: counts logical file bytes (including
  sparse files and duplicate/hard-linked paths conservatively), bounds entries
  and nesting depth, checks cancellation, and rejects symbolic links, special
  files or enumeration errors. It does not follow links, delete artifacts or
  treat unreadable paths as empty. Only the campaign directory is counted.
- Advancement applies the admitted byte allowance before its lock/write work and
  checks retained storage after preparation, rendering and collection. Exceeding
  a boundary returns an error and preserves evidence. The producer-owned asset
  repository outside the campaign directory is not included by this scan.
- Tests count a five-byte/two-file fixture exactly, reject four-byte and one-entry
  allowances, cancellation and a symlink, then place an over-limit sparse canary
  in a real campaign directory. Advancement rejects it before preparing batch 0
  or collecting any take. Moving the canary aside allows the existing SIGKILL/
  CLI-recovery scenario to proceed.
- This is phase-boundary enforcement, not a hard per-write disk quota: a phase
  can overshoot before its post-check, and external writers can race a scan.
  Reservation/accounted writers and combined campaign/producer growth remain
  necessary before declaring aggregate storage control complete.
- Verification: affected Release build, export CTest (1/1, 4.41 seconds), and
  staged-source closure (1/1, 0.25 seconds) passed. No new full-suite run claimed.

Next concrete implementation owners: explicit evidence-backed legacy migration,
then complete populated-workspace parity and candidate
review/publication parity and the resumable inventory campaign. The generation
test uses synthetic diagnostic material, not a qualified singer. No M1 completion
is claimed.

## Voiced reattack boundary repair

### M2.P1 process-ownership extraction

Native inference experiment: added an optional `seam_onnx_runtime_probe` C++
target selected by an application-owned SDK root, and an isolated ONNX fixture
generator/check. The official macOS-arm64 ONNX Runtime 1.30.0 archive was
downloaded and its SHA-256 matched the release digest
`6ebb5062a934537c352937821f9fe9718e7de1a2db1122a93dd363ffd53a7012` before
extraction. The probe compiled and ran two real CPU inference sessions against
generated arithmetic graphs. Repeated outputs passed; changed scale returned
the exact output-mismatch exit code and swapped graph schemas returned the
runtime-error code. SDK and fixture environment remain ignored under
`build/neural-runtime`; requirements and reproduction steps are tracked under
`tools/neural_runtime`. No global Python packages were installed.

This is an actual runtime experiment, not neural singing: graphs contain no
learned voice weights, model admission is not implemented, and the executable
is not the first-party framed worker or an untrusted-bank sandbox. Signed
deployment, bounded model loading, feature matching, worker integration and
trained acoustic/vocoder assets remain required. No full-suite rerun claimed.

Data-bundle follow-up: added a separate `FrozenNeuralBundle` in synthesis, not a
reinterpretation of legacy NeuralSingerResource. It requires exactly one each
of acoustic/vocoder/vocabulary/configuration, permits bounded variance/tensor
assets, and owns deep-frozen hash-verified bytes behind shared immutable backing.
The deterministic name-sorted manifest binds roles, names, lengths and digests.
There are no executable/path/runtime-library fields or ONNX Runtime dependencies.
Asset count is 4–32, payload total at most 512 MiB, each asset at most 256 MiB,
vocabulary/configuration at most 4 MiB each, and manifest at most 32 KiB.
Tests cover reorder-stable identity, shared backing, caller-buffer mutation,
digest mismatch, aggregate overflow, invalid/duplicate names and roles, and
cancellation. Targeted synthesis build and performance-snapshot CTest passed
(1/1, 2.13 seconds). These arbitrary-byte fixtures do not prove graph validity:
manifest import, acoustic/vocoder feature compatibility, vocabulary admission,
render-resource integration and real inference remain required.

Follow-up: moved the dependency check into a reusable CMake module and added
six configure fixtures: allowed, harmless cycle, forbidden direct/transitive,
LINK_ONLY and alias links. The guard resolves ALIASED_TARGET before checking
forbidden owners so aliases cannot bypass the rule. Negative fixtures require
the specific dependency diagnostic, not merely any configure failure. The new
CTest passed (1/1, 0.37 seconds); total registered tests are now 124. This does
not assert support for arbitrary nested generator expressions.

Moved the bounded helper request/output API and sole process implementation
to `libs/seam-platform`. Japanese reading and neural execution now call the
platform API directly. The old authoring header contains only using-declaration
aliases for source compatibility. A normalized source comparison confirmed
that process implementation logic is unchanged apart from include/namespace;
timeouts, bounded I/O, cancellation, process-group cleanup and existing POSIX
limitations are preserved. Helper tests now link platform without authoring.

Removed neural -> authoring-runtime and explicitly declared neural -> synthesis.
A configure-time transitive target-link check rejects paths from neural to
authoring or rendering (including LINK_ONLY-wrapped dependencies). Fresh CMake
Graphviz output in `build/release/seam-dependencies.dot` shows neural's direct
dependencies as core, distribution, formats, platform and synthesis. The fresh
Ninja graph assigns helper_process.cpp to seam_platform only. Targeted helper,
neural-worker and Japanese-reading CTests passed (3/3, 2.69 seconds).
This completes the source-owner extraction, not M2.P1: Windows supervision,
host qualification, data-only neural bundle admission and real inference remain
open. The POSIX runner remains best-effort supervision, not a security sandbox.
Full verification after extraction: complete Release build passed; fresh CTest
passed 123/123 in 85.20 seconds, including source closure and compatibility-header
callers. No Windows execution or installed-host supervision result is implied.

### Rhythmic custom phrase authoring

Custom pilot input now accepts `LYRIC:MIDI[:TICKS]`, defaults to 480 ticks and
preserves explicit note durations in the normal saved score. Per-note durations
are bounded to 1–3840 ticks and total duration to 61440 ticks (32 seconds at
120 BPM). Syntax/count/range failures occur before output-directory creation;
phonetic timing still rejects a gesture that cannot fit its note. The custom
recipe now includes explicit b/d/g models, with unsupported liquids still
rejected rather than substituted. A regression exports ba/melisma/N/a with
960/240/720/480 ticks and verifies all five marker spans and the 60000-frame
candidate. Invalid, empty, extra-field and aggregate-overflow duration inputs
are rejected. Targeted pilot build and CTest passed (1/1, 6.35 seconds).
No full-suite rerun or naturalness qualification is claimed for this CLI change.
The maximum-duration diagnostic also rendered successfully in all three variants
to `build/release/seam-pilot-rhythmic-16bar-01`: 64 quarter notes, 16 bars at
120 BPM. Baseline candidate metadata confirms 1536000 frames at 48 kHz, 104
markers and unapproved status. The repeated kana phrase with varied melody is
an engineering render exercise, not independent creator/new-song acceptance.

### Normal voiced-stop rendering and candidate integration

Schema-six recipes are now admitted through normal resource decoding, compiled
articulation and rendering. Candidate schema six adds `voicedPlosiveRevision`
and `voiced-plosive` markers. Loading binds the marker to the exact recipe's
closure model and rejects unvoiced relabeling, missing/incorrect revision,
insufficient closure space, schema downgrades and approval claims. Closure
parameters remain in hash-bound recipe bytes rather than duplicated editable
metadata. The voiced source revision participates in schema-six snapshot hashes.
Older candidate schemas retain their shapes; mixed schema-six recipes may
export older gesture subsets without claiming a voiced stop they did not render.

Added the ordinary `stops` pilot: pa/ba/ta/da/ka/ga with matched pair pitches and
three variants. The exported Float32 regression checks exactly silent unvoiced
closures, nonzero voiced closures, precise marker identities and repeated hashes.
The repository-import/Studio-reopen fixture now includes ba so collection must
retain the new marker kind and unreviewed state. Listening WAVs are retained in
`build/release/seam-pilot-voiced-stops-01`. The fixture isolates closure voicing
using paired release spectra; this is not proof of natural b/d/g pronunciation.
This section supersedes the earlier default-admission gates, not their remaining
acoustic-quality limitations. Advanced timing/coarticulation and other phone
classes remain open under M1.P2.
Verification: full Release build passed; fresh full CTest passed 123/123 in
92.61 seconds, including voiced-stop export/import/Studio reopen. No listening
qualification, installed-host qualification or M1 completion is claimed.

### Opt-in articulated voiced-stop rendering

ArticulatedStream revision ten renders admitted VoicedPlosive gestures using
the continuous score-driven PhonationSource as closure excitation. Its stateful
VoicedPlosiveSource owns closure filtering and the release burst; the separate
noise lane explicitly delegates that gesture instead of rendering the burst
twice. Ordinary vowel-tract excitation is muted during the stop, and the vowel
re-entry uses a bounded taper. Compiled dynamics/articulation gain applies after
mixing, as for other sources. Stream copies/reset include the voiced-stop state.
Preparation compares closure gain/cutoff and release configuration with the
frozen recipe. Default schema-six admission remains disabled pending candidate
metadata/export integration.

The real Japanese ba timing fixture now exercises opt-in audio rendering,
nonzero closure, exact release/vowel boundary silence, whole/chunk equality,
checkpoint replay, cancellation rollback, reset replay and changed-recipe
rejection. Targeted Release build and voice-design CTest passed (1/1, 8.01
seconds). This is not yet normal song/export support or acoustic qualification.

### Voiced-stop compiled timing integration

ArticulationPlan revision nine carries an explicit VoicedPlosive gesture with
the closure source configuration, preserved PhonemeKey and ordered interval.
An explicit experimental admission flag permits schema-six planning only;
default resource decoding and song rendering remain closed. Recipe-selected
stop bindings now include voiced phones and validate closure parameters and
token voicing. A real Japanese `ば` score compiles a 2,880-frame onset into
2,400 closure frames plus 480 burst frames at 48 kHz, with no timing invention
outside the owning note. The following vowel retains its compiled nucleus.
Noise-only rendering rejects the new gesture rather than omitting its voicing.
Tests cover the actual resolved score, source parameters, default rejection,
wrong-style rejection and downstream renderer gates. The source/filter mixer
and candidate ABI remain required before normal rendering admission.
Verification: targeted Release build and voice-design CTest passed (1/1,
7.40 seconds). No full-suite or musical-quality result claimed.

### Explicit voiced-closure recipe contract

Schema six adds optional `plosives[].voicedClosure` with explicit `gain` in
(0, 0.5] and `lowpassHz` in [40, 2000]. Its presence admits b/d/g design-time
bindings; its absence preserves p/t/k. Existing same-style source requirements
and duplicate frication/plosive rejection remain. A schema-six mixed recipe
encodes null closure fields for unvoiced entries. Strict decoding rejects
missing/extra fields, invalid models and semantic downgrades; schemas 1–5 retain
their prior canonical representation. Tests verify round trip, schema identity,
downgrade/missing-field rejection, invalid gain/cutoff/phone combinations and
byte/hash-identical legacy stop encoding after removing the new opt-in pose.

Targeted Release build and voice-design CTest passed (1/1, 7.56 seconds).
`decodeVoiceRecipeResource` deliberately still rejects schema six in rendering:
normal song rendering must not silently ignore closure voicing. Timing, mixed
source rendering and candidate marker integration are the next required work.
No production voiced-stop support or acoustic quality qualification is claimed.

### Experimental voiced-closure source primitive

Added `VoicedPlosiveSource` beside the existing unvoiced primitive. It accepts
caller-supplied excitation (no independent pitch oscillator), applies bounded
gain and a stateful one-pole low-pass during the closure, tapers the closure
edges, and retains the existing release burst exactly. Configuration rejects
nonfinite/out-of-range gain and cutoff and closures too short to voice. Invalid
excitation and cancellation roll back both the filter and release-source state.
Tests at 22.05/48/96 kHz verify nonzero closure, exact unvoiced-burst equivalence,
whole/chunk identity, reset and failed-render rollback. Targeted Release build
and voice-design CTest passed (1/1, 9.31 seconds).

This is a source primitive only, not shipped voiced-stop support. Next required
integration: explicit versioned recipe fields and same-phone model binding,
compiled closure/release timing and score-derived excitation in the articulated
renderer, marker/candidate ABI propagation, and voiced-versus-unvoiced exported
phrase tests. Production still rejects unsupported voiced stops. No recipe
schema or existing renderer behavior changed in this increment, and no acoustic
quality claim is made for the experimental closure parameters.

### Actionable recipe coverage errors

Recipe articulation failures now retain the original error code and identify
the recipe ID and selected style alongside the existing phone/note diagnostic.
Missing VocalTract poses identify phone, style and recipe rather than only
reporting a missing pose. Tests verify missing phone/style and the stderr from
an actual unsupported `ば:60` production pilot export. No source substitution,
DSP change, approval or expanded phonetic support is implied. Targeted Release
build and voice-design/pilot CTests passed (2/2, 7.78 seconds).

### Bounded user-authored pilot phrases

The pilot CLI now accepts `phrase LYRIC:MIDI ...` rather than only fixed
fixtures. It validates 1–64 UTF-8 lyric/pitch pairs, MIDI 24–96, and retains
ordinary editable `.seam` projects plus recipes, WAVs and unapproved candidates.
Notes currently use 480 ticks at 120 BPM; richer editing belongs to the saved
score/native editor, not a second CLI score engine. The combined explicit pilot
recipe supports the existing phone models and leaves unsupported phones as
render errors. A custom m/a/t/a/continuation-a/N/a phrase exported successfully
to `build/release/seam-pilot-custom-01`. Tests cover actual marker output,
invalid syntax/pitch/count rejection before directory creation and unsupported
voiced-stop rejection without a successful report. This is not complete phone
coverage, a populated bank or the full new-song acceptance journey.
Verification: Release pilot target built; updated pilot CTest passed (1/1,
4.92 seconds). No full-suite rerun or listening acceptance claimed.

### Standalone nasal production fixture

Added `seam_singer_pilot NEW_DIRECTORY nasals`: six alternating standalone
Japanese `N` and oral vowel notes through normal ExportService. Inspection
confirmed the existing syllabic-N fallback timing path; no new timing or DSP
semantics were needed. A dedicated explicit nasal recipe pose supplies full
nasal coupling with resonance/antiresonance instead of inserting an oral vowel.
The CLI regression checks exact N/a/N/i/N/u markers, full-note spans, unapproved
metadata, voiced analysis windows in every note, audio hashes and deterministic
repeat exports across three variants. Release pilot build and updated pilot
CTest passed (1/1, 4.67 seconds); no fresh full-suite run claimed. Listening
artifacts are retained in `build/release/seam-pilot-syllabic-nasal-01`.
This exercises the required standalone-N path but does not qualify its sound,
close all phonetic classes, or complete M1.P2.

### Vowel-only production-path follow-up

The new `seam_singer_pilot NEW_DIRECTORY boundaries` fixture renders the same
four-note melody as separate vowels and as a melisma, across three recipe
variants. Its real exported dry-PCM assertion initially failed: the baseline
first reattack boundary had summed adjacent absolute amplitude 0.01975246.
Pure-vowel phrases select SustainedPoseStream, so the mixed renderer repair
alone did not fix this production path. Sustained vowel scheduling now applies
the existing 5 ms taper at compiled reattacks without tapering continuations.
Its renderer revision increased from 12 to 13 to invalidate prior identities.

The CLI regression checks eight vowel markers, hash-bound Float32 mono audio,
zero boundary samples for separate attacks, nonzero continuation boundaries,
and exact repeat hashes for all three variants. Targeted Release build and
pilot/voice-design/export CTests passed (3/3, 8.38 seconds). No fresh full-suite
run is claimed for this follow-up. Listening artifacts are retained in
`build/release/seam-pilot-boundaries-02`; `-01` retains the pre-fix comparison.
Neither fixture is a listening-quality acceptance result.

- Source inspection found that PhonationSource restarts phase on compiled score
  reattacks, but ArticulatedStream previously joined all adjacent voiced gestures
  without an envelope taper. Optional authored attack/release controls do not
  supply a default taper. A new adjacent-vowel regression failed before repair
  at the final sample preceding the reattack.
- The mixed renderer now applies its existing bounded 5 ms smoothstep taper at
  gesture boundaries that coincide with a compiled note reattack. Intra-note
  phone transitions and compiled melisma continuations remain connected. Filter
  and source state are retained; this is not a filter reset or score mutation.
- ArticulatedStream revision increased from 8 to 9. Existing snapshot/cache and
  candidate metadata paths consume that constant. Recipe schemas are unchanged;
  affected old render identities must not be treated as newly rendered evidence.
- Regression covers separate Japanese vowel notes, a continuation vowel,
  exact boundary silence only for reattack, and sample-identical whole/chunked
  rendering with an owned-window split one frame before the boundary. The
  expanded matrix exercises 22.05/44.1/48/96 kHz, starting MIDI pitches 36/61/84,
  both reattack and continuation (24 combinations), cancellation rollback,
  checkpoint replay and reset replay.
- Verification: affected Release targets built; voice-design and export CTest
  targets passed (2/2, 7.57 seconds). The new regression was observed failing
  before the implementation change. No full-suite or listening-quality pass is
  claimed. Voiced stops, expanded phonetic context, qualified singer assets and
  the remaining six-milestone plan are still open.
- Regenerated three articulation variants through the production ExportService
  in `build/release/seam-pilot-articulation-reattack-01`. These are retained
  unqualified listening artifacts, not phonetic or identity acceptance evidence.
- Follow-up verification: complete Release build passed, followed by a fresh
  full CTest invocation: 123/123 passed in 84.76 seconds, including the expanded
  boundary matrix and production pilot CLI. This supersedes the earlier narrow
  test boundary for this repair, but does not establish musical qualification.
# Training label consistency checkpoint

Added phoneme/F0/voicing label validation and a correction queue. Checks cover
phrase bounds, complete contiguous phone spans, vocabulary membership, explicitly
configured confidence threshold and analysis-frame geometry. Low-confidence,
unknown or unreviewed labels are not silently accepted. Supplied review revision
text is not authenticated; all outputs retain trainingAdmitted=false.

Verification: fourteen training-tool tests passed, including valid consistency
without approval, combined correction reasons and invalid alignment/feature
geometry. Source digest, note/slur/lyric binding, reviewed revisions and the
label-report CLI remain open, as do permission admission and actual training.

# Captured source-preparation CLI checkpoint

Added the `prepare CONFIG SHA256 SOURCE_ROOT NEW_REPORT` command, sharing bounded
configuration loading and no-replace publication with `split`. It binds the exact
configuration identity to file-inspection results. Exit 3 preserves per-source
rejection diagnostics without a split-ready inventory; exit 0 means inspected
only, not rights/training approval. Invalid config/publication returns exit 2.

Verification: eleven training-tool tests passed, including subprocess preparation
success, rejected-source diagnostics and existing-output preservation. Audio
transforms, segmentation, permission admission, labels and training remain open.

# Source-file preparation integration checkpoint

Added read-only `prepare_sources`: explicit root, contained source paths,
captured digests, per-file/aggregate byte budgets, regular-file checks and
per-item inspection diagnostics. Successful sources produce actual PCM-derived
split identities; any failure suppresses the complete split-ready inventory
instead of silently omitting failed records. Original files remain unchanged.
Path checks do not claim race-free filesystem isolation.

Verification: ten training-tool tests passed, including actual source reads,
missing/escaping/linked paths, changed digest, no partial split publication and
source preservation. Captured-config prepare CLI, permissions and transforms
remain unfinished; these results do not qualify training data or a singer.

# Training source PCM inspection checkpoint

Added bounded inspection of actual captured mono integer PCM WAV bytes: expected
source digest, requested sample rate, supported widths, duration/frame limits and
exact payload length. It returns both source-container identity and a separate
geometry-plus-PCM identity, without converting or modifying audio. Unsupported
formats require a future explicit preparation transform. No rights are admitted.

Verification: nine training-tool tests passed. Metadata-only WAV variations have
different source hashes but the same PCM identity and therefore stay in one split
group; wrong digests, stereo/unsupported width, wrong clock and truncated payloads
reject. Permission admission, reviewed labels and training remain unfinished.

# Exact-audio duplicate dossier checkpoint

Split output schema 2 now reports duplicate-audio source groups, unique-audio
counts by partition and redundant-source totals, while retaining every source
reference. This prevents raw record counts from masquerading as independent
recording counts. Selection/removal is explicitly review-required; no label or
permission conflict is resolved automatically. Input configuration stays v1.

Verification: six training-tool tests passed, including three duplicate records
counted as one unique recording in the held-out partition, stable output order
and preservation of all source records. Hashes remain caller-supplied pending
the separate source-audio admission stage.

# Captured dataset split command checkpoint

Added `python3 -m tools.voice_model_training split CONFIG SHA256 NEW_OUTPUT`.
It reads bounded captured configuration, rejects duplicate keys/schema changes,
binds configuration and source-inventory hashes, and publishes canonical output
without replacing an existing destination. Opened input must be a regular file.
Temporary output is fsynced before same-directory no-replace link publication;
directory durability and Windows qualification are not claimed.

Verification: five training-tool tests passed, including subprocess command
execution, bad digest/no output, deterministic repeated output, source preservation,
overwrite refusal and temporary cleanup. No source permissions or audio are
admitted by this split command; the rest of the training pipeline remains open.

# Original-model dataset split implementation checkpoint

Started the planned `tools/voice_model_training` owner with deterministic source
splitting. Song/session/lineage/exact-audio relationships are unioned transitively;
groups touching explicitly held-out songs are assigned wholly to test. Remaining
groups use seeded hashing; missing partitions are reported rather than repaired
by leaking related recordings. A canonical source-inventory digest is retained.

Verification: four tests passed, covering order independence, transitive leakage,
explicit holdout propagation, invalid identities and input preservation. This is
the split algorithm only, not the full command or source-admission pipeline. No
audio, permissions, labels, learned checkpoint or singing qualification was created.

# Native graph text-budget checkpoint

Native inspection bounds each protobuf string field to 4096 bytes and total
model-tree text to 8 MiB before upstream checking. Raw tensor storage is excluded
from this text policy and retains its own bounds. The policy includes descriptive
metadata; actual export compatibility is still to be qualified. Checks are
post-parse and do not prevent all parser allocation.

Verification: all four native integration tests passed in 8.36 s. Tests accept
the per-field boundary, reject an oversized name and reject aggregate metadata
overflow. The diagnostic privacy test now uses a bounded forged-log name so it
still reaches the upstream checker rather than failing the new text budget first.

# Native checker exception diagnostic checkpoint

The reusable native inspection API no longer writes upstream checker exception
text to stderr. The CLI emits a stable numeric failure message; the runtime
caller retains its existing bounded generic failure message. This avoids echoing
model-controlled tensor names or graph fragments from caught checker exceptions.
It does not intercept every third-party library's internal logging mechanism.

Verification: all four native integration tests passed in 6.34 s. A regression
uses a 64 KiB input name containing a forged log line; rejection emits only the
fixed code-15 diagnostic and no stdout.

# Cooperative native-inspection cancellation checkpoint

`inspectBytes` now accepts a stop token, checks cancellation before/after parsing
and upstream checking, per traversed message and periodically during numeric
tensor scans. Observed cancellation returns code 18 without replacing outputs.
The ownership test covers pre-cancelled inspection and preservation of prior
results. Third-party parser/checker calls remain noninterruptible internally;
process supervision is still required for hard deadlines. CLI behavior is unchanged.

Verification: native build-and-test target passed all four integration tests in
4.87 s. In-flight cooperative cancellation latency is not measured by these tests.

# Full native-enabled regression checkpoint

The complete enabled Release build passed, followed by all 128 CTest targets
(`ctest --test-dir build/release --output-on-failure -j 4`, 113.11 s).
All 43 offline Python neural tests also passed. This includes the four optional
native inspection/runtime tests and the original product regression suite.
It supersedes the focused-only test boundary for the accumulated native changes.
The parser/runtime remain development dependencies; full production execution
policy, trained original singer, installed-host evidence and Beta GO are not
established by this regression result.

# Optional native integration test target checkpoint

Registered four native experiment CTests and a build-and-test target,
`seam_neural_native_checks`, with explicit binary dependencies. Enabling the
Python-driven checks requires an application-configured existing ONNX 1.19.1
environment; configuration performs no installation or download. Default builds
do not acquire these dependencies. The enabled checkout now has 128 CTest entries,
not 128 newly verified tests in this checkpoint.

Verification: the new target built its dependencies and passed all four tests
in 9.25 s. The last complete 124-test run predates this optional registration;
no new full-suite claim is made. Tests remain fixture/structural evidence, not
production singer qualification or Beta GO approval.

# Frozen native inspection-to-runtime checkpoint

The root build can optionally link native structural inspection into the existing
arithmetic runtime probe with `SEAM_NATIVE_ONNX_SCHEMA`. Bundle metadata and both
frozen graphs are checked, including native pair compatibility, before either
ORT session is constructed. Parsed validation models are discarded; ORT receives
the same immutable bytes that were inspected. No path is reopened in between.

Verification: rebuilt the enabled probe; CLI-prepared bundle/request inference
and both pair profiles passed. A newly prepared, correctly hashed bundle with
an unknown operator rejects with the native inspection error before session
construction. Mismatched pair declarations now reject earlier in this mode.
Default builds do not gain a parser dependency. This integration is still a
development experiment: production worker packaging, execution-family resource
policy and an admitted handle remain open; no learned singing is claimed.

# Reusable in-memory native inspection checkpoint

Extracted the native checker into a reusable object-library target with an
`inspectBytes` API. The pathname command is now an adapter. Parsing operates
only on supplied bytes; model/report outputs are replaced transactionally after
all checks pass. Failed inspection preserves previous outputs. This enables
future frozen-bundle/worker integration without reopening graph paths.

Verification: native build and owned-byte test passed; results survive source
buffer changes and malformed/empty input leaves prior outputs unchanged. The
tensor and pair regression script also passed. No production worker currently
consumes this API, and a prepared admission handle remains required.

# Native acoustic/vocoder pair checkpoint

Added a native pair contract operating directly on parsed/checked ModelProto
objects. It requires exact tensor names/types/ranks, batch one, declared mel
layout/bins, steps scalar or vector-one, vocabulary-duration axis agreement,
per-graph frame-axis consistency and a distinct output-sample axis. Hop/frame
declarations bound mel element count; actual computed shapes remain runtime checks.

Verification: native experiment rebuilt; scalar/audio and vector1/waveform
profiles passed; 12 mismatched configurations rejected, alongside the existing
tensor regressions. This is structural pair validation, not a prepared execution
admission handle or proof of acoustic feature semantics/learned singing.

# Native graph interface reporting checkpoint

Native inspection now returns verified top-level input/output names, element
types, static/symbolic dimensions, IR and opset versions using proper JSON
serialization. Differential checks compare both acoustic/vocoder interfaces
against Python and cover a name containing quotes, backslash, newline and Unicode.
The rebuilt checker passed the existing 16 representation/28 rejection cases.
These interfaces support the next native pair-contract step; reports still do
not authorize execution or establish dynamic runtime bounds.

# Upstream native graph-checker integration checkpoint

The isolated native inspection build now includes the locally installed ONNX
1.19.1 checker and standard operator schemas, plus their source dependencies.
It calls the in-memory checker only after external-reference and local bounds
checks. Unknown standard operators, undeclared inputs and unsupported attributes
are rejected. Full shape inference is disabled; an execution-family allowlist,
resource-cost policy, pair validation and production packaging remain open.

Verification: native target built successfully; 16 valid tensor representations
and 28 rejection cases passed, including three upstream checker regressions.
Status is `NATIVE_STRUCTURE_CHECKED`, not execution admission. Dependencies remain
local development inputs and are not shipping/release-qualified artifacts.

# Native tensor numeric-value checkpoint

Native tensor inspection rejects nonfinite float32/64/16, noncanonical Boolean
values and out-of-range typed int8/uint8/float16 storage values. Raw storage uses
explicit little-endian order. This policy also rejects intentional infinite mask
constants; real-model compatibility is not established. Operator attributes and
runtime outputs need separate validation.

Verification: rebuilt parser; 16 valid raw/typed representations and 25 rejection
cases passed, including eleven invalid-value cases. Operator semantics and
production inference admission remain unfinished.

# Native tensor payload consistency checkpoint

Native tensor inspection now enforces exact raw byte lengths or exact typed
element counts according to the declared shape/type. Mixed raw/typed storage,
wrong typed fields and segmented tensors are rejected. Numeric ranges and
finiteness remain separate unfinished checks, as do operator/schema validation
and production graph admission.

Verification: rebuilt parser; both graph inventories still match Python;
14 invalid cases reject; all eight supported types pass in both raw and typed
form with matching declared byte counts (16 representation cases). The earlier
large missing-data fixture now rejects missing payload before reaching the
second tensor's aggregate-storage check, as intended.

# Native tensor bounds checkpoint

The native parser experiment now checks numeric/bool element types, rank,
overflow-safe dimension products, aggregate declared tensor storage and all
encountered ValueInfo shapes. It rejects unnamed dynamic dimensions but does
not yet bound named dynamic dimensions at runtime. Reports include declared
tensor bytes. Generated parser payloads are not execution-admitted; tensor
payload consistency and operator/schema validation remain missing.

Verification: rebuilt native experiment; two graph storage inventories matched
Python; ten invalid cases were rejected, including excessive tensor/interface
products, negative dimensions, unsupported tensor types and aggregate storage.

# Native schema parser experiment checkpoint

Added an isolated native ONNX parser target using locally available Protobuf
33.4.0 and hash-checked ONNX 1.19.1 schema bytes. Reflection code is generated
only in the build directory. No shipping target links this experiment.
Native input-byte and recursion limits precede parsing; a bounded reflective
walk rejects external tensors, unknown fields, custom domains, training graphs
and local functions. Message-count checks occur after parsing, not before
allocation. This is parser groundwork, not a completed graph admission factory.

Verification: configured/built on macOS arm64; two arithmetic graph node/tensor
inventories matched the Python inspector; empty/truncated bytes, unknown fields,
custom domains and nested external tensor metadata were rejected. Operator,
shape and pair validation and production worker integration remain open.

# Parent bundle-loading lifetime checkpoint

Bundle launch now rejects process budgets beyond the platform runner's limits
before filesystem lookup/model loading. Regression cases use a missing directory
to prove invalid-budget rejection precedes path admission. Parent graph payload
ownership is scoped to metadata inspection and released before launching the
child, which independently loads its own verified bytes. Metadata/vocabulary
remain owned values. This removes parent graph retention during inference; it
does not establish a measured peak-memory ceiling or eliminate loading copies.

Verification: rebuilt native worker protocol target and passed its CTest in
3.58 s, including bundle transport, cancellation and deadline coverage.

# In-flight bundle-worker termination checkpoint

The dedicated transport fixture now supplies a test-only readiness/PID marker
after child-side bundle and request validation. Native tests wait for this
handshake before cancellation, verify the terminated PID no longer exists, and
exercise a separate deadline-terminated child. A fresh request must succeed
afterward. This closes the earlier pre-launch-only cancellation evidence gap for
the macOS bundle transport fixture, not installed hosts or Windows supervision.

Verification: rebuilt fixture/protocol test; initial protocol run passed (2.19 s)
and three consecutive repeat-until-fail runs passed (4.23 s total). No production
graph runtime or learned audio is involved in these lifecycle tests.

# Graph intake storage and interface checkpoint

The offline ONNX inspector now limits aggregate declared tensor storage to
512 MiB, including nested tensor attributes, rather than applying only per-tensor
element limits. Numeric/bool interface types and static dimension products are
checked explicitly. Reports expose sorted operator counts and declared storage
for subsequent graph-family policy work; neither field grants admission.
Runtime intermediates, dynamic dimensions, parser memory and production child
admission remain outside this check.

Verification: all 43 offline neural tests passed, including combined-storage,
interface-product and unsupported-interface regressions. Paired arithmetic and
CLI-prepared bundle runtime checks passed. No new full CTest run was needed for
this Python-only inspector change; the preceding native checkpoint remains below.

# Integrated regression checkpoint

The accumulated bundle metadata/vocabulary conversion, CLI preparation, request
v3, launch v2, deployment materialization and transport changes passed a complete
Release build and all 124 CTest targets (`ctest --test-dir build/release
--output-on-failure -j 4`, 102.83 s). Separately, 40 offline neural-runtime tests,
16 native/Python vocabulary byte comparisons plus 12 rejection cases, paired
arithmetic runtime inference and CLI-prepared bundle request inference passed.
This supersedes the earlier focused-only verification boundaries for these code
changes. It does not satisfy learned singing, independent listening, production
graph admission, Windows/installed-host qualification or full Beta GO acceptance.

# Deployment descriptor materialization checkpoint

Added `build_neural_deployment_descriptor` in the release-package tooling. It
reconstructs the canonical helper manifest from finalized files, checks the
application-specified build/module/protocol target, and emits unsigned canonical
descriptor bytes plus their digest for the release signing owner. Schema 1
remains eight fields; schema 2 includes explicit launch protocol 2. Unsupported
platform/surface combinations, unsafe manifest paths, changed payloads and
cross-version packages are rejected. It neither supplies keys nor signs releases.

The native package materialization suite passed in 6.12 s. New tests generate
descriptors in Python, sign them with ephemeral test keys in a copied native
module, and exercise signature verification plus module-anchored package loading
for both versions. A correctly signed descriptor targeting the wrong loaded
package version is rejected. Test helper bytes are not execution-qualified;
these results establish deployment plumbing only, not production inference.

# Bundle-aware process transport checkpoint

`runNeuralBundleWorker` now requires launch protocol 2, request metadata v3,
an application-selected absolute canonical directory, a bounded aggregate bundle
size, and nonzero CPU/resident-memory budgets. It reloads and verifies metadata,
uses the bundle's vocabulary, and launches the selected helper with the directory,
model ID/version, manifest hash and payload budget as separate arguments.
Shared transport code retains helper hashing, deadlines, bounded pipes, request
hash checks and exact response identity checks, including the bundle hash.

The separate `seam_neural_bundle_transport_probe` is a zero-PCM transport fixture,
not the v1 probe and not a production graph worker. It reloads actual bundle bytes
in the child; no ONNX session is constructed. Tests cover successful binding,
missing response bundle identity, missing budgets, payload limits, wrong launch
version, the legacy helper, cancellation before launch and changed bundle bytes.
Production child graph admission remains mandatory and unimplemented in this
transport layer. Windows and installed-host execution evidence remain pending.

# Versioned neural deployment checkpoint

Helper manifests now admit only the explicit schema/protocol pairs 1/1 and 2/2.
The resolved options retain that launch version; the legacy runner rejects 2.
Signed deployment schema 2 requires an explicit protocolVersion 2, an application
target expecting 2, and a loaded package with the same launch version. Schema 1
retains its original eight-field representation and expected legacy protocol.
Python package materialization can seal either version without changing its
default; payload inventory reconstructs the declared version rather than silently
rewriting it to 1. File integrity is not proof of a helper's implementation.

Verification: native protocol test passed (1.05 s), including signed version
selection and cross-version rejection. Python/native package materialization
exercises generated manifests for both versions. Production bundle launch and
child graph admission are still pending; no new singing capability is claimed.

# Bundle-conditioned protocol v3 checkpoint

Implemented the M2.P1 metadata version boundary: request/response v3 carry an
explicit `bundleContentHash`, matching the current frozen bundle's model content
identity. V3 requests require phonetic conditioning; v3 responses require the
canonical request hash. Binary framing remains SNW1/version 1, and metadata v1/v2
retain their existing representations. Unknown fields prevent silent downgrades.
The legacy `runNeuralWorker` launcher refuses bundle-conditioned requests rather
than letting its v1 helper satisfy a production contract accidentally.

Focused verification: native protocol CTest passed (1.11 s); native arithmetic
bundle runtime test passed for both v2 and v3, including wrong bundle identity
and attempted downgrade. This implements the wire contract, not the remaining
production v2 launcher, admitted graph handle, packaging, or real singer model.

# Request-driven runtime experiment checkpoint

The native optional ORT probe now accepts an external framed request through
`--paired-request`, validates it against child-loaded bundle metadata and vocabulary
before session creation, and returns binary request-bound PCM. The end-to-end
CLI preparation/reload test verifies two pitch/dynamics combinations, 731-sample
output with padded-tail removal, exact canonical request hashes, and rejection of
truncation, trailing bytes, wrong model identity, and changed graph bytes.

Verification: optional native probe rebuilt successfully; bundle runtime test
passed; all 40 offline neural-runtime unit tests passed. No full Release-suite
rerun is claimed for this checkpoint. Fixtures are arithmetic graphs, not learned
voices. Production worker packaging, model admission, cancellation, lawful learned
assets, singing qualification, and the remaining full implementation plan stay open.
# Source-bound training label command

Reviewed dataset assembly API: joined fresh source-rights admission, annotation
admission and deterministic splitting with exact source/hash/clock/lineage checks.
Snapshot binds both configurations/reviews, labels, vocabulary and split evidence;
missing partitions/duplicate selection remain explicit issues and training stays
unadmitted. Fixture integration passed for deterministic assembly, missing train/
validation partitions and signed conflicting lineage rejection. No real dataset
was admitted or trained. CLI and derived-source dataset assembly remain open.

Annotation admission CLI: added `admit-labels` with captured label/review/policy
inputs, independent trust anchor, fresh source inspection and current-clock
expiry checks before publication. Existing output and signed unresolved label
errors reject without overwriting anything. Fixture subprocess tests cover valid
admission, immutable prior output and invalid-label nonpublication. Source rights
and full training readiness remain explicitly separate.

Source-bound annotation admission: refactored label inspection for reuse and
joined it with label-specific signed review. Admission requires schema 3 score/
silence ownership and rejects unresolved phone/confidence/voicing corrections.
The absent legacy review string may be superseded by the verified signature;
source permissions and training execution remain separate. All 30 training
tests passed, including fixture admission and signed low-confidence rejection.
No real label approval or whole-dataset training admission was issued.

Annotation authority separation: refactored shared review checks behind explicit
rights/label verification entry points. Label reviews require a distinct policy,
role, format and decision; tests reject rights-as-label and label-as-rights replay
while valid fixture signatures still verify. All 30 training tests passed. No
real annotation review was created; source-bound label admission remains open.

Extractor lifecycle repair: timeout cleanup previously called poll(), which could
reap an exited leader and skip killing descendants retaining output pipes. It
now checks unreaped returncode state and terminates the process group before
waiting, preserving PID ownership through that operation. A forking fixture
exits the leader while its descendant holds pipes; regression verifies targeted
group termination on timeout. The first test draft had unsafe redundant cleanup
after PID reaping and an overly short startup allowance; removed that redundant
kill and increased the test deadline. Focused lifecycle tests pass.

Correction publication: `correct-labels` now re-inspects the source, checks label
identity/geometry, applies stale-checked corrections and publishes new label/edit
evidence with parent/child label hashes. Review remains invalidated; consistency
diagnostics remain visible. All 29 training tests passed, including subprocess
corrected publication, hash lineage, no-overwrite and stale-edit rejection.
Native correction UI and authenticated musical review remain unfinished.

Label correction API: added transactional F0/voicing and phoneme-object edits
with expected-old-value checks. Shared boundaries can be changed in one batch;
stale targets, duplicate targets, invalid timing and inconsistent voicing reject
without changing the original. Derived labels clear review revision. All 28
training tests passed. CLI/editor integration and authenticated label approval
remain unfinished; a corrected label is not automatically training truth.

Pitch correction queue: fresh extraction now publishes low-confidence voiced
frame and zero-padded-window diagnostics, with source frame indices and explicit
reviewRequired. Refreshed reports use schema 2; fresh-pitch segments use schema 5.
Old artifacts are not overwritten/migrated during resume. Training and native CLI
integration tests passed, including uncertainty/tail diagnostics. This exposes
known estimate uncertainty; it does not establish a validated acoustic threshold
or independent label approval.

Fresh-feature workflow integration: propagated the extractor option through
batch preparation and reviewed derived-source admission. Native tests execute
off-grid batch extraction, exact resumed extraction and fixture-signed parent
admission followed by real native feature extraction. Training and native feature
groups passed. This closes option plumbing, not real dataset permission, musical
label review, Windows supervision or actual model training.

Off-grid crop integration: `segment --fresh-pitch-extractor` now extracts fresh
native pitch on the exact child WAV and publishes schema 4 evidence-bound labels.
It supports starts between parent hops without shifting/reusing parent F0 frames;
phonemes are rebased and review stays invalidated. The native integration test
executes a one-sample-offset crop and verifies child feature hash and geometry.
Training and native feature CTest groups passed. Batch/admission option propagation
and real-voice boundary quality remain unfinished.

Automatic refresh CLI: `refresh-pitch` now joins captured configuration, bounded
source inspection, supervised native extraction and feature-to-label conversion,
then publishes a new report containing full feature evidence and cleared-review
labels. Integration exercises the actual native binary through the Python CLI
and verifies no-overwrite. Training and native feature CTest groups passed. This
does not create phoneme alignments, approve labels or train a model.

Native extraction supervisor: added POSIX first-party extractor invocation with
deadline, bounded stdout/stderr, concurrent pipe draining, failure termination and
strict JSON parsing. Integration compares actual native output to supervised
output; fixture tests cover timeout, duplicate fields, stderr overflow and a
subsequent successful invocation. All 27 training tests passed. This does not
establish OS sandboxing, Windows support or executable authenticity; refreshed
label publication is still unfinished.

Voiced feature verification: native CLI tests now extract deterministic PCM tones
at 8 kHz/110 Hz, 48 kHz/220 and 880 Hz, and 192 kHz/440 Hz, then consume their
features in the training adapter. Complete-window estimates pass a 10-cent test
tolerance; no tail-accuracy or real-voice claim follows. Adapter regressions reject
wrong settings/grid, nonfinite values and inconsistent voicing without modifying
parent labels. All 26 training tests and the native feature CLI integration passed.

Native-to-training feature adapter: `apply_pitch_features` now validates exact
native feature fields, source hash, clock, window/hop settings and full frame grid,
then copies fresh F0/voicing into labels and clears review revision. The native
CLI test consumes actual generated feature JSON and rejects a wrong source hash;
it passed. This does not yet orchestrate extraction automatically or establish
real-voice pitch accuracy. Existing phonemes remain supplied annotations.

Native feature command: `seam_voicebank_cli extract-pitch WAV` now emits full-hop
FFT autocorrelation features as JSON stdout, binding the exact captured WAV hash
and declaring window/hop/range/threshold/algorithm/coverage. Input is bounded to
64 MiB, mono decoded samples to 16 million, output frames to 65536 and FFT work
to 512 million butterflies. Rate-scaled power-of-two windows preserve the existing
60..1200 Hz diagnostic range. No automatic training approval. Added native CLI
integration cases for full/partial-hop silence, hash binding, source preservation
and malformed input. Python training adapter and real-voice quality remain open.

Fresh-feature preparation: inspected native `analyzePitch` and found its default
complete-window output does not match training's ceil(samples/hop) label geometry.
Added explicit `PitchFrameCoverage::FullHopGrid` with zero-padded tail windows,
preserving default CompleteWindows behavior. Work/frame budgets include every
added tail frame. Regression cases cover single-sample, exact-hop, partial-hop
and longer tails, silence, invalid coverage and frame-budget rejection. This is
the native prerequisite for fresh crop feature extraction; CLI integration and
acoustic quality qualification are not yet claimed.

Derived admission CLI: `admit` now accepts a captured crop configuration/source
and separate clip destination, checks all four options together, and publishes
the derived-source admission outside the clip directory. Exact clip resume is
explicit and always creates a new time-bound admission report after parent
review verification. All 25 training tests passed, including CLI derived hash
binding and resumed re-admission. No real approvals or training were performed.

Checkpoint `4e05891b` preserves signed source admission and the shared signature
repair locally (not pushed). Follow-up `admit_segment` now revalidates parent
review/source/evidence, checks parent hash and lineage, then derives an exact clip
and binds its identities to the reviewed singer. Tests cover derived admission
and unreviewed lineage rejection before output. All 25 training tests passed.
Only fixture approvals were exercised; no real source authorization or training
was performed. Derived admission CLI/dataset assembly remain open.

Signature repair interoperability checks: fetched RFC 8032 section 7.1 from
https://www.rfc-editor.org/rfc/rfc8032.txt and added its first two public known-answer
vectors. Public-key derivation, exact deterministic signatures and verification
match; S+L malleability rejects. Four update-CLI tests passed. Added a training
review regression showing that an identity-key forged review rejects even under
an explicitly supplied policy containing that weak key; training CTest passed.
These vectors and regression tests are targeted evidence, not a full independent
cryptographic implementation audit.

Shared signature repair: reproduced identity-key universal forgery acceptance in
the Python Ed25519 verifier used by release and training review verification.
Added on-curve/canonical point decoding and rejection of torsion-only public keys
and nonce points. Tests cover the forged identity signature, noncanonical zero-x
sign encoding, out-of-field encoding, valid signatures and altered messages.
Three update-CLI tests and all 25 training tests passed. This is a targeted
security repair, not a claim of complete cryptographic audit or subgroup-policy
qualification. No real signed approvals were issued.

Admission CLI follow-up: connected `admit` to captured configuration/review/policy
files, independent canonical policy hash and fresh audio/evidence inspection.
Uses the system clock and rechecks expiry before publication. New reports only;
no signing, trust provisioning or training execution. All 25 training tests passed,
including subprocess fixture admission, no-overwrite and wrong-anchor rejection.
Only fixture signatures were used; no real source admission was performed.

Source-admission join: extracted read-only permission configuration inspection
and connected it to trusted signed-review verification in `admit_sources`.
A valid signature cannot bypass missing scopes or changed audio/evidence.
Successful fixture admission records sourcePermissionsAdmitted with policy,
review, configuration identities and expiry; trainingAdmitted remains false
because label/split/execution prerequisites are separate. All 25 training tests
passed, including valid signed source admission and signed-incomplete/changed
source rejection. No real source permission approval was issued.

Signed-review verification: added closed training review/policy contracts using
existing role-bound Ed25519 verification. Requires an independent policy hash,
exact captured configuration digest, explicit current time, training reviewer
role and matching signer. Expired/future, wrong-configuration, altered-signature
and wrong-anchor reviews reject. All 25 training tests passed using a test-only
signer. No real trust anchor or review was issued. Successful verification is
not execution admission; fresh inspected permissions must still be joined.

Complete integrated checkpoint: all 129 registered CTests passed in 85.50 seconds
with `-j 4`, including the 24-case training-tool group and native ONNX checks.
This supersedes the earlier split 128-plus-one test invocation. Reviewed the next
authorization boundary: existing `tools/public_release/crypto_validation.py`
provides role-bound signed-record verification. Training admission must bind a
distinct trusted reviewer policy and exact captured dataset/evidence identities;
a key embedded in an assertion cannot authenticate itself. No real reviewer
policy, signed training approval or trained model was created in this checkpoint.

Permission CLI follow-up: `permission-report` now captures bounded regular-file
evidence and actual audio from a captured configuration, publishes source-bound
scope assertions and retains explicit false training-admission/review-authentication
flags. Exit 3 reports missing asserted scopes; wrong evidence fails without new
output; existing reports reject. All 24 training tests passed, including subprocess
CLI success, missing modelTraining, changed evidence and no-overwrite cases.
This does not complete the plan's authenticated `admit` operation.

Permission/audio join follow-up: added `inspect_permission_sources`, which
reuses the bounded preparation reader and requires exact source-set and hash
agreement with permission assertions. Schema 2 reports now bind actual PCM
identity/geometry plus song/session/lineage. Source-byte verification is distinct
from review authentication and training admission; both latter flags stay false.
All 24 training tests passed, including mismatched permission digest, unmatched
source ID and altered recording rejection. CLI evidence capture and authenticated
execution admission remain open.

Training-permission capture: inspected existing external-beta source admission;
its four bank/render scopes do not include model training. Added a separate
training manifest checker reusing those names and requiring modelTraining,
modelRedistribution and commercialModels independently. Evidence bytes are
bounded and hash-checked; supplied reviews are not authenticated and no execution
authority is granted. All 23 training tests passed, including bank-only scope
rejection for complete assertions and missing/changed evidence. Actual source
joins and authenticated training admission remain unfinished.

Batch-to-split integration: batch report schema 2 now binds successful child
source IDs, WAV hashes and exact segment-record hashes and exposes `splitSources`
only when every entry succeeds and child IDs are unique. Duplicate IDs are
reported without deleting clips and cause exit 3 with no split inventory.
The integration test consumes the verified inventory in the existing splitter,
checks held-out grouping and exact-audio duplicate accounting, and verifies
duplicate-ID rejection. All 22 training tests passed. Permissions, reviewed
labels and trained-model qualification remain separate unfinished requirements.

Batch preparation follow-up: added `segment-batch` for 1..64 captured phrase
configurations, sequential bounded source reads, unique flat output names,
per-entry rejection reports and explicit resume through existing byte-verified
segment publication. Every attempt requires a new report. The added recovery
test preserves a successful clip and old report while a missing input is supplied
and completed on resume. All 22 training-tool tests passed. Dataset-wide identity
admission, rights, reviewed labels, actual training and musical qualification
are still incomplete; batch success is PREPARED_UNAPPROVED only.

Publication recovery follow-up: `segment --resume` verifies existing output
against re-derived source/configuration-bound bytes. Complete artifacts return
without writes; exact audio lacking a record receives only the final record.
Truncated audio, conflicting records, directory symlinks and unexpected entries
reject without overwrite. Existing no-resume behavior is preserved. This handles
the audio-complete/manifest-missing interruption boundary, not arbitrary partial
audio repair or concurrent hostile directory mutation.

Integrated checkpoint verification: Release build succeeded; the existing full
128-test CTest registry passed with zero failures in 85.53 seconds (`-j 4`).
Registered `seam_voice_model_training_tests` in the root CMake test suite with
a 60-second timeout and repository working directory. After CMake regeneration,
the new test group passed all 21 internal Python tests in 1.13 seconds. The new
registry contains 129 tests; this was 128 full-suite tests followed by the new
group, not a single post-registration 129-test invocation. No learned weights,
independent listening, installed nine-host qualification or Beta GO follows
from this engineering checkpoint.

Score-segmentation follow-up: segment schema 3 now crops explicit-silence score
supervision alongside acoustic labels. Notes/rests are clipped and rebased,
syllables/phone ranges/silence indices remapped, and an initial retained melisma
note becomes a local onset with review still invalidated. Inconsistent ownership
requires relabeling instead of invented supervision. All 21 training tests passed,
including CLI publication and melisma/silence reindexing. Silence-only score crops,
fresh feature extraction, source authorization and actual training remain open.

Label-segmentation follow-up: added copied/rebased acoustic labels to segment
config/output schema 2. Parent source identity, geometry and container/PCM hashes
are checked before output creation. Child labels bind the new clip hashes;
phoneme spans and hop-aligned F0/voicing slices are rebased without modifying the
parent, and review revision is always cleared. Off-grid starts explicitly require
fresh extraction. All 20 training tests passed, including CLI labeled publication
and wrong-parent rejection. Score cropping and feature re-extraction remain open.

Segmentation publication follow-up: the `segment` CLI now consumes a captured
configuration and source WAV and creates a new directory with sample-exact
`audio.wav` plus final hash-bound `segment.json`. Validation precedes directory
creation; existing/partial directories reject without overwrite. Interrupted
output is retained, not silently resumed. Tests verify CLI content identity,
frame count, lineage, invalid interval, existing/partial output and immutable
original bytes. All 19 training-tool tests passed. This is single-phrase
publication, not batch recovery, label rebasing, permission admission or training.

Phrase preparation follow-up: implemented `segment_source` to extract exact
half-open PCM frame intervals from captured mono 16/24/32-bit sources. Returns
new WAV bytes plus parent/child content hashes, transform revision/range and
inherited song/session/lineage. Tests verify exact nonzero sample slices,
repeatability, invalid intervals/identity rejection, and descendant grouping in
held-out splits. All 18 training-tool tests passed. No source is modified; no
rights admission, model training, label rebasing or batch publication is claimed.

Schema 3 follow-up adds explicit non-lyric silence phone ownership. Ordered
syllable ranges and silence indices must partition every phone exactly once;
duplicate, overlapping, missing, unordered and out-of-range ownership reject.
Schema 1/2 interpretation is preserved. Also closed the correction-report
budget check for diagnostics produced only by phonemes and missing review.
Verification: 17 training-tool tests passed, including schema 3 CLI publication
and leading/internal/trailing silence ownership. Supplied silence classification
is not acoustic evidence or authenticated review.

Follow-up: label configuration/report schema 2 adds explicit lyric syllables,
phone ranges, MIDI notes, rests and slur continuation checks. Schema 1 remains
unchanged. Canonical `label-report` command now matches the implementation plan,
with `labels` preserved as an alias. All 16 training-tool tests passed, including
source-bound schema 2 subprocess reporting and invalid slur rejection. These
are structural supervision checks, not acoustic alignment, silence-phone
ownership, authenticated review, training execution or musical acceptance.

Added the `tools.voice_model_training labels` CLI to connect label consistency
checks to actual inspected PCM sources. It requires one label per captured
source, verifies container/PCM hashes and frame geometry, and publishes a
configuration-bound report without overwriting earlier output. Inconsistent
labels produce exit 3 and a correction queue; invalid identities produce exit 2
without publication. Source data stays unchanged. This does not authenticate
reviews, admit training rights, train weights, or establish musical quality.

Verification: all 15 training-tool unit tests passed, including the subprocess
label CLI success/correction/mismatched-source/no-overwrite paths. Full native
regression suite was not rerun for this Python-only addition. Next training work
must connect lyrics, note/slur supervision and source/permission admission to
the prepared data; no milestone is claimed complete here.