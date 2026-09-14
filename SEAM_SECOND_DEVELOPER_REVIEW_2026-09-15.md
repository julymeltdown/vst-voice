---
title: SEAM second-developer review and joint completion recommendation
date: 2026-09-15
status: assessment and recommendation
authors: second developer (task 01a0a066), with the primary implementer (task 01a02275)
basis: September 15 developer handoff guide, plus a three-round technical exchange with the primary implementer
authority: none. This records an agreed technical recommendation for the owner's existing full-scope objective. It is not a new Beta definition, not a unit acceptance, and not a claim that any action below has been performed.
---

# SEAM: where the product stands, what is missing, and how to finish it

## 0. How this document was produced, and what that limits

I was given the September 15 handoff guide and deliberately nothing else. I did not read the project's
conversation history and did not survey the repository before forming an opinion, so that the primary
implementer would correct my reading instead of watching me re-derive theirs. Everything below that
describes current source is their statement, re-verified by them during the exchange. Everything that
is judgement is marked as judgement.

Three claims here rest on checks the implementer ran while we talked. Several rest on their reading of
source they have worked in for weeks. Nothing rests on a listening result, because nobody has listened
to the current output. That absence is the most important fact in this report and it organizes
everything else in it.

## 1. The finding that matters most

**We have reduced implementation uncertainty far faster than musical uncertainty, and that gap is now
the project's central risk.**

The evidence for the first half is strong. 164 registered test targets. A generatable pilot inventory
at 1026 of 1026 assignments. A campaign that committed 498 takes across three pitch layers. A neural
worker that executes admitted bundles and publishes 96000 nonzero samples through the ordinary render
path. Follow Host bounces authorized against a real observed tempo history. Six timbral expression
channels landed in about a week, several with oracles better than most audio code ever receives:
gender proved as bit-identical equality against formant and tension carrying gender's own amounts,
airiness proved distinct from breathiness by holding periodicity at 0.976 where breathiness collapses
it to 0.695.

The evidence for the second half does not exist. Not one of those 164 targets can tell us the voice
sounds good, or that a person could make a song with it. They establish that the system is
self-consistent, deterministic, bounded, recoverable and honest about what it refuses. They cannot
establish that anybody wants to sing with this instrument.

That is not a criticism of the tests. It is an observation that the feedback loop has been running on
the half of the problem that is cheap to measure. Coverage counts, determinism, successful
orchestration and channel breadth became the operational substitutes for hearing whether the thing
works. The September 13 plan did not ask for that. It places held-out audible material before the full
campaign and names listening experiments in M1.P2 and P3. Execution drifted; the specification did not.

## 2. Three corrections to the handoff guide

The guide is accurate and unusually disciplined about separating implemented capability, accepted
units and release readiness. Three things changed during the exchange, and a reader of the guide alone
would get them wrong.

### 2.1 The failing test is confirmed housekeeping and should stop dominating the status

The guide reports 163 of 164 targets passing with `seam_tracked_source_closure` failing, and leaves the
cause open. The implementer reran `scripts/verify_tracked_source_closure.py` during our exchange. It
reports exactly three "not indexed" inputs, `growl_automation.hpp`, `growl_automation.cpp` and
`test_growl_expression.cpp`, and nothing else. The script derives its set from `git ls-files --cached`,
so the three untracked growl files are the reported defect. This is an unfinished publication step,
not an unexplained synthesis failure.

The correct resolution is to finish reviewing the growl diff, write its ledger entry, stage the
intended checkpoint files, and rerun closure and the failed target. **That has not happened.** Until it
does, the honest statement is the one the implementer insisted on: "163 passed in the full run; closure
passed after staging", and only once the latter check actually passes. A full six-minute regression is
not warranted merely because Git indexing changed.

One nuance a casual reader will miss: the three new files are the reported defect, but the full
checkpoint also carries 22 modified integration files with schema 17 and compiler revision 14. Staging
is not the whole review.

### 2.2 The new expression channels do not work on the resource the journey installs

This is the most consequential item in this report and it appears nowhere in the handoff guide.

The six timbral channels, formant, breathiness, tension, airiness, gender and growl, are implemented on
the source-filter carrier, because that carrier owns its own excitation and vocal tract. **The
sample-bank path refuses them by name.** Baking a recipe into a bank does not preserve access to an
excitation or a tract; it preserves the audio those things produced.

Follow the consequence. A creator sculpts an original voice as a recipe. That recipe can do twelve
things. We then run a 1026-assignment campaign and 498 takes to bake it into a sample bank, and the
resulting resource can do six of them. **We spend the project's largest production effort to produce a
resource strictly less expressive than its own source.** For a recorded singer that trade is obviously
correct, because there was never a tract to keep. For a synthesized original voice it is backwards.

Any statement of the form "installed bank plus drawable expression" is therefore incoherent until it
names which expression and which renderer. A curve lane will not make breathiness or gender work on an
installed bank. Closing the gap requires either a bank-side implementation of those controls or an
explicit procedural-resource route, which is the third correction.

### 2.3 The direct procedural route already exists; its distribution lifecycle does not

I proposed making procedural recipes a first-class publishable resource so the bake becomes optional.
The implementer corrected me with source: that path is substantially built. `ProceduralSingerResource`
with freeze, load, decode and save operations lives in
`libs/seam-voice-design/include/seam/voice_design/recipe_resource.hpp`. A vocal track persists a
`ProceduralRecipeReference`. The native menu carries "Select Procedural Recipe..." and "Relink
Procedural Recipe...". `RenderSnapshotFactory::createProcedural` and `TrackRecipeFileSource` route
ordinary standalone selection. Final export supports procedural sources, and `includeProjectAndRecipes`
packaging retains canonical recipe bytes under content-addressed paths while rewriting the packaged
project's references.

So "the application only understands banks" is false. What does not exist is a qualified,
independently reviewed, signed and catalogued procedural installation lifecycle equivalent to the
bank's. A portable project-and-recipe export is not that.

Asked why the journey bakes at all, the implementer's assessment was historical execution order, reuse
of the established bank production lifecycle, and the explicit requirement to demonstrate
generated-material-to-bank production. There is **no measured performance evidence that forces baking**,
and they declined to claim one.

The practical consequence reorders near-term work: the first usable original-voice route should be the
direct procedural one, and the baked comparison becomes a diagnostic for the separate bank route rather
than a prerequisite for deciding whether the voice has promise.

## 3. Where the product actually stands

### 3.1 What is real

Musical state, persistence, migration, undo and ownership are mature. The score-to-audio compiler is
consumed by rendering rather than declared. Classical rendering dispatches four backends with explicit
capability refusal. The producer repository has durable generations, writer exclusion, provenance,
review and publication. Voice Designer has versioned recipes with phonation, resonance, noise, nasal
and plosive control, preview, A/B and batch generation. The articulation model admits oral vowels,
nasals, frication, voiced frication, released and voiced stops, unvoiced and voiced affricates,
approximants, palatalized consonants, coda placement, silence and breath events, with declared
transitions rather than derived windows. Follow Host preparation is authorized from a bounded observed
tempo map. Neural transport, deployment, bundle admission and render integration are real and tested
against the shipped worker.

### 3.2 What is not, stated precisely

**There is no learned singer.** The implementer was careful here and the precision is worth preserving:
"the model is not real" is too broad. There is actual DiffSinger architecture work, bounded training,
checkpoint and resume, and acoustic export. But the training diagnostic uses oscillator fixtures and
the production-worker song proof uses arithmetic graphs. Those demonstrate different real pieces.
Neither establishes a useful learned singer. Remaining: an appropriate corpus and labels, a compatible
vocoder, a complete candidate bundle, learned-model song inference, and recorded runtime reliability.

Two traps worth naming. Source permission is a prerequisite when acquiring material and is not a
substitute for data suitability; generated audio can be fully authorized and still be poor supervision.
And training on an unintelligible procedural teacher does not, by itself, produce intelligibility.

**Style blend is a name, not a path.** Current source has `StyleBlend` vocabulary and a capability name.
There is no implemented interpolation. The resource dependency is real, since the repository has no two
compatible aligned styles, but producing a second style is necessary and insufficient: two
independently approved banks are not automatically blend-compatible, because the pair needs matching
coverage, a usable common range, and alignment or conditioning suited to the algorithm. Implementation
still needs pair identity, selection, validation, interpolation, cache identity, persistence, and
endpoint and intermediate audio tests. A raw crossfade invites phase cancellation and double-voice
artifacts and should not be assumed as the solution.

**The expression channels have no editing surface.** No drawn lanes, no curve editor, no inspector
applicability rows, which is M4.P1 item 6. Today a creator nudges a channel by a tenth with a key
combination and undoes it. Nobody tunes breathiness by pressing a key forty times.

**The 498 generated takes are all unapproved**, the connected campaign-to-install-to-song regression
uses a narrow synthetic recipe, and 1026 of 1026 means the declared inventory is preparable. None of
these is a phonetic or musical qualification.

**Nothing has been listened to.**

### 3.3 On expression breadth, where the implementer conceded a sequencing error

There was a defensible reason to build a few channels before designing the shared editor: establish
real units, neutral behaviour, backend applicability, persistence, undo and an audible consumer first.
Formant, breathiness and bipolar gender give the editor contract genuinely different shapes to satisfy.

That does not justify completing every analogous channel before trying the surface. Each later channel
bought progressively less information about whether a person can tune a song, and the implementer
agreed the ordering went too far. Six channels with no lane is a weaker position than three channels
with a lane, an editor and an applicability row, because the sixth channel teaches nothing new about
the interaction while the first lane would have tested whether the design survives a bipolar unit, a
unipolar unit and share-based nudges at once.

The forward rule we agreed: finish growl because it is already implemented, then **stop adding
expression breadth** until the lane exists.

## 4. The agreed sequence

This is a joint recommendation, in the implementer's wording. It serves the owner's existing
full-scope objective and changes sequencing, not the Beta GO contract.

1. **Finish the growl checkpoint.** Review the diff, write the ledger entry, stage the intended files,
   rerun source closure and the failed target. Report the result honestly and only after it passes.
2. **Render and retain the first direct procedural listening packet immediately, before any new
   capability.** This is the step the project has been deferring by building more.
3. **Build the shared expression lane against that same song:** unit-aware scales, neutral reference,
   point insert, move and delete, drag-level undo grouping, playback feedback, explicit resource
   applicability. Exercise a bipolar channel, a unipolar channel and a pitch or formant-style unit
   through one interaction.
4. **Let listener and creator findings choose the acoustic repairs.** Not a backlog; the observed
   dominant failure picks the next change.
5. **Finish procedural distribution** from its existing foundations as a bounded slice (section 5).
6. **Investigate neural feasibility on a separate lane,** which can begin alongside items 2 and 3
   rather than waiting for item 5.
7. **Expand baked inventory only when the bank-specific comparison justifies it.** Paired-style
   experiments can start small once the basic voice shows promise; full inventory production and formal
   qualification should not precede that signal.

### 4.1 The smallest outcome worth calling a product

I proposed: one original voice, one language, an unfamiliar song, an installed standalone workflow.
The implementer accepted it with three amendments that are load-bearing, and named it the **first
usable original-singer milestone**:

- The creator can **change the intended voice**, preserve that recipe and resource identity, and
  reproduce the result. Without this we have proved a singer player, while the owner's central
  differentiator is voice creation.
- The expression used in that journey **actually works with its selected resource.** Drawn but
  unsupported timbral lanes do not count. This turns section 2.2 into an acceptance condition.
- Save and reopen, retake and edit, undo and final export work **without developer intervention.** A
  technically successful render is not yet a usable instrument.

The other languages, neural singing, the nine-DAW matrix and character performance are not
prerequisites for proving that one musical workflow works. They remain mandatory for the owner's agreed
Beta GO. Neural singing may additionally become a practical quality dependency if procedural synthesis
cannot reach the intended identity and intelligibility, which fixture tests cannot settle.

## 5. Procedural distribution: the finite slice

The implementer inspected packaging and recipe-selection code to separate existing mechanism from
missing product connection. `SeambankPackageInfo` currently owns `voicebank::Manifest`, and pack and
verify decode the sample-bank manifest; procedural selection today picks an explicit JSON file and
stores a path plus exact resource identity. What follows is a decomposition, not eight new subsystems.

1. **A procedural distribution manifest with bounded admission.** Resource family, immutable identity,
   canonical recipe digest, producer and version, styles, declared language, phone and range support,
   renderer compatibility. Keep declared support distinct from reviewed qualification. The recipe is
   data for a first-party renderer; a package must never select an arbitrary executable. Existing
   recipe decoding stays the semantic validator. Decide explicitly how distribution versions relate to
   existing resource identity; do not overload schema version.
2. **A revision-bound review candidate.** Freeze the recipe with representative score and audio
   evidence at an exact build and render settings. Review coverage, range and intended identity.
   Persist decisions and invalidate them when bound material changes. Reuse review and provenance
   mechanisms where they fit; do not force recipes into thousands of fake recorded-take records.
   Signing authenticity and musical review are different statuses.
3. **Pack and verify support.** Reuse signature, digest, path-safety and bounded-archive primitives,
   add a typed procedural manifest route. The sample-manifest requirement cannot simply be deleted.
   Whether this is a versioned extension or a separate envelope over shared primitives is a format
   decision needing producer and consumer review before code.
4. **Transactional installation and receipts.** Verify trust, entry hashes, recipe semantics and
   renderer compatibility before publishing an immutable installed resource. Handle interruption,
   duplicate installation, conflicting content and side-by-side versions through the existing installer
   transaction. Installing must never overwrite another resource or a creator's editable draft.
5. **Discovery and exact identity resolution.** Distinguish missing, changed, untrusted and
   incompatible. Preserve exact relink versus intentional replacement. A signed but incompatible recipe
   should be visible with a useful reason rather than silently loaded or replaced.
6. **Native installed selection and editable-copy workflow.** Surface procedural singers with family,
   styles, trust, qualification and applicable controls, routed through existing undoable commands.
   Editing an installed singer creates a copy or new version and must not mutate signed content. This
   extends the current Select and Relink actions rather than replacing the rendering path.
7. **Compatibility and reproducibility policy.** `buildProceduralIdentity` already includes DSP
   revisions, compiler revision, render ABI, score, recipe hash, pronunciation, style and rate, which
   prevents stale cache reuse when algorithms change. **It does not preserve the old renderer's
   behaviour.** Reproducing a past sound requires a retained runnable build or implemented
   compatibility behaviour, not a revision number in a hash. The same caveat applies to neural model
   bytes. Automatic migration must not silently inherit an old acoustic approval.
8. **One connected acceptance journey.** Create and edit a recipe, freeze a candidate, review, sign and
   package, install, discover and select, tune a new song with applicable expression, save and reopen,
   export. Repeat with the authoring source directory unavailable. Exercise tampering, incompatible
   revision, missing resource, interrupted install, exact relink and intentional replacement. Technical
   automation first; qualification and fresh installed-machine observation remain separate evidence.

This slice follows the listening checkpoint. It must not delay it.

## 6. The listening packet, and what each result should change

### 6.1 Protocol

A small held-out phrase set plus one short unfamiliar melody, covering vowels, consonant contrasts,
transitions, short notes, sustained notes and the proposed range. Render dry audio through the direct
procedural path and through generated material installed as a sample bank, at comparable pitch and
level. A language-capable listener identifies words without first reading the lyrics. A musician
assesses phrasing, pitch continuity and usability. A creator corrects at least one observed failure
through the editor.

This separates a weak voice source from damage introduced by baking, unit selection or rendering, and
from a workflow that makes otherwise usable audio impractical to tune.

### 6.2 The staffing dependency, and ASR's actual role

The pilot language is Japanese and the team is Korean. A listener available on demand for every
iteration is the kind of dependency that quietly turns a two-day experiment into a two-week one.

I proposed ASR screening as a cheap gate. The implementer rejected the gate framing and I accept the
correction: "a phrase ASR cannot transcribe will not survive a human" is not defensible, because the
recognizer has its own domain mismatch and decoding priors, and can equally invent plausible text from
weak audio. Whisper's model card documents hallucinated text and recommends domain-specific evaluation.
What survives is **ASR-assisted triage**: pin model, version, decoding settings, Japanese transcription
mode, preprocessing and normalization; never supply the intended lyric as a decoding hint; include
known-understandable singing references, silence and noise negatives, and paired intact and
consonant-degraded probes so we first learn whether the recognizer can distinguish the failure we care
about; retain raw transcript, normalized comparison, omissions and insertions alongside the audio;
treat a kanji or kana spelling difference as orthography rather than a pronunciation error; flag
relative regressions and never label a score "intelligibility PASS"; have a human sample both good and
bad ASR results at first, not only the passes. Run it at relevant DSP and pilot checkpoints, not on
every unrelated build.

An all-empty result is ambiguous between a poor voice, an unsuitable singing domain for the recognizer,
and a wrong screening configuration. Test the method before depending on it.

On staffing: batch a comparison packet and use a short initial listening session, iterating engineering
and ASR checks between human checkpoints. Korean team members can assess usability and some musical and
artifact properties without certifying Japanese pronunciation. Switching the pilot to Korean now would
add a second synthesis and pronunciation confound.

### 6.3 A listenable regression, not just a conclusion

The project's regression evidence is almost entirely propositional: 164 targets asserting
relationships. They can tell you the system stayed self-consistent; none can tell you the voice got
worse.

Partial credit where due. The pilot tool already writes scores, recipes, WAVs, markers and hash and
measurement reports, and `test_singer_pilot_cli.py` compares repeated renders. That establishes
repeatability, not a reviewed perceptual baseline across versions.

What is missing is a compact, durable, versioned listening reference set with a manifest tying score,
recipe, resource, build, renderer and settings to original dry audio and measurements. Preserve the
reference audio and, where feasible, a runnable rendering environment. Generate new output alongside it
and never regenerate the reference in place. Use a phrase set and a melody rather than one sustained
vowel. A byte difference should request investigation, not veto a deliberate improvement; promoting a
new reference requires an explicit reason and listening evidence. No hash and no distance metric can
tell us on its own that a voice got worse.

### 6.4 Expected results and the decision each one forces

The implementer has not listened to the retained output, so this is an engineering hypothesis from the
implementation, without attached probabilities. Their overall expectation is a **mixed** result: some
steady vowels and simple syllables giving useful musical material, some consonant and transitional
contexts unclear or uneven, perceived identity inconsistent across context and range, and tuning
exposing workflow friction. A single global intelligible or unintelligible verdict would hide exactly
the information we need.

| Result | What it means | What it starts, stops or redirects |
|---|---|---|
| **A. Intelligible but characterless** | A major risk has fallen | Encouraging, but "the lane will fix it" is too optimistic: a lane exposes dimensions the model already represents and cannot supply distinctions it lacks. Test whether bounded recipe changes preserve intelligibility while producing a clearly preferred identity across held-out phrases. If yes, a contained recipe, control and UX task. If no, source-model work remains. Female identity still needs listener evidence. |
| **B. Unclear consonants or transitions** | The predicted failure | Localize first: isolated syllable versus phrase, slow versus short notes, one pitch versus declared range, direct versus baked. A timing displacement, weak burst, masking transition or level mismatch is a bounded defect. "Every phonetic class correct on paper, speech unclear everywhere" is a much larger model problem. |
| **C. Audio works, editing fails** | A workflow verdict | The editor becomes the immediate critical path. Observe one person making specified corrections, saving, reopening and exporting unaided. Identify the missing interaction rather than calling all of it "the lane". Lower scientific uncertainty than model repair; effort still unknown until the failures are visible. |
| **D. Context-dependent failure** | The outcome I had missed | Works on a short slow mid-range probe, fails on an unfamiliar melody, rapid syllables, range edges, sustained notes or mixed expression. Or direct synthesis works while the installed bank damages it. **This can masquerade as success if the demonstration is too friendly.** Report a failure matrix by context and route; test combinations, not one control at a time. A narrow successful pilot must not silently redefine the declared range or language coverage. |
| **Invalid** | No reproducible artifact, or no useful listening observation | The experiment has not answered the musical question. Record it as neither failure nor success and fix the experiment. |

### 6.5 A stopping rule for acoustic repair

The implementer would not claim that a learned model is the only possible path, and equally would not
authorize endless source-filter refinement. The proposed cap is **two focused repair cycles before a
route decision**, a resource-allocation rule rather than a scientific threshold. Each cycle must name a
failure class, one main hypothesis, the change, and a held-out observation capable of disproving the
hypothesis. Set the effort cap before starting and do not extend it by adding inventory.

If improvement is reproducible and generalizes, continue. If gains appear only on the training phrase,
damage other classes, or leave the principal failure unchanged, pause that route and compare
alternatives: a better procedural model, authorized recorded excitation or transients, or a learned
acoustic and vocoder path. The independent neural experiment is what makes that last option
evidence-backed rather than a rescue promise. The recording-free voice-creation requirement survives
any such comparison; it cannot disappear as an implementation shortcut.

## 7. Ownership and coordination

| Lane | Owner | Scope |
|---|---|---|
| Expression surface, direct procedural song checkpoint, acoustic integration repairs | Primary implementer (01a02275) | Needs the editor internals and recipe DSP they have been living in |
| Neural feasibility | Second developer (01a0a066), prospective | Corpus strategy, model and vocoder compatibility, end-to-end experimental protocol, and measured results when an authorized run is actually performed |

The neural lane is a **bounded feasibility experiment**, not a production milestone: choose one viable
source strategy, assemble a small representative corpus, train or adapt a real acoustic candidate,
connect a compatible vocoder, render held-out lyrics through the shipped worker, and measure throughput
and learning improvement. Only then estimate scaling. Until that exists, M2 is partly a research
milestone and this report says so. Neither implementer can rank corpus, code and compute by a numeric
factor today; there is no production-corpus throughput, no demonstrated learning curve and no held-out
quality result, and "data is 5x longer than code" would be fabricated precision. CPU inference support
does not imply that CPU training is a sensible schedule.

Coordination boundaries we agreed: the owner's standing development authorization is not evidence that
a new corpus or expenditure has been approved. The shared seams are resource identity, capabilities,
runtime ABI, packaging and CMake, so "neural barely intersects" was wrong. Keep public contracts stable
during the first experiment; propose changes with producer and consumer implications named, and let one
integrator own shared edits. Both tasks must not commit into the same dirty checkout; an isolated
checkout of a named base is preferable for a separate implementation track.

## 8. What efficiency means here

Efficiency on this project is not more throughput. The last week produced six expression channels with
excellent oracles, and the project learned almost nothing about whether anyone can make a song with
this voice. The expensive mistake available right now is another large implementation push whose
completion is mistaken for proof.

Three habits change that.

**Stop using channel count and inventory count as the primary evidence of progress.** 1026 of 1026
assignments means the inventory is preparable. 498 takes means 498 unapproved takes. Both are real
engineering results and neither is musical evidence.

**Put the cheap experiment in front of the expensive investment and let its result choose the next
build.** A failed phrase render is an engineering defect. A rendered phrase nobody understands is a
different result demanding a different action. The development loop needs room for both. Nothing makes
the first listening packet difficult: choosing representative phrases, establishing an ASR baseline and
getting a little real human feedback is work, but it costs far less than qualifying or training against
a corpus whose source voice has not passed that first test.

**Report in three separable claims and never merge them:** implemented behaviour, demonstrated
workflow, and accepted unit or release evidence, each naming its source revision and distinguishing
retained evidence from fresh verification. The project already does this well in its ledger; the risk
is losing it under schedule pressure.

## 9. What we still do not know

Whether someone wants to make a song with this voice.

Everything in section 4 is arranged to answer that question sooner and more cheaply than the current
trajectory would. The owner should prepare for a short diagnostic loop with an uncertain acoustic
outcome. The first packet's job is not to succeed. Its job is to buy a better next decision.
