# SEAM automated verification and acceleration plan

---
title: SEAM automated verification and acceleration plan
date: 2026-09-19
branch: codex/production-readiness-completion
baseline_commit: 3276b7df
status: analysis and plan; implementation performed only where named
language: English
companions: SEAM_JOINT_DEVELOPMENT_PLAN_R4_2026-09-16.md, SEAM_DETAILED_DEVELOPMENT_PLAN_2026-09-19.md
reviewed_with: second-developer session 01a0a066-1eba-71f2-8c0d-e21f9419cbcc
---

## 0. What this document answers

Three questions, asked directly:

1. Why has this taken a month, and was that reasonable?
2. Can the remaining work be developed quickly with multiple agents?
3. Can the human verification gates be replaced by Codex, open-source projects, downloaded audio, or
   other services, with OpenUtau as the main reference?

The short answers: the month was not wasted effort, but it was spent on the wrong risk; yes, the
remaining engineering can be parallelised, with hard conditions; and no, not all human gates can be
replaced — but **five of the seven can be partly or wholly replaced, and the two that cannot are
narrower than the current plan implies**. Section 6 gives the unit-by-unit boundary and the strongest
honest substitute for each.

## 1. The most important finding: the roadmap is nearly out of engineering work

This is the fact that reframes everything else, and it is the reason a month of continued work has not
produced a shippable product.

In R4 section 9, **every remaining unit is a human or external gate**:

| Unit | What it needs | Who |
|---|---|---|
| U1.4 | one unaided creator session | project owner |
| U2.1 | a listening judgment | recruited listener |
| U3.5 | rights-cleared learned singer + compatible vocoder | owner + external |
| U4.1 | an authorized recording | owner + external |
| U4.2b | native-speaker review per language | owner + external |
| U4.3 | two compatible aligned styles | follows from U4.1 |
| U5.1 | Windows host with installed DAWs | owner |
| M6 | five independent creators | owner + external |

There is no engineering-only unit left in the queue. That means **adding more engineering does not move
the product closer to Beta GO**, which is exactly what the last month demonstrated empirically. The
project did not stall because the agents were slow. It stalled because the remaining distance is gated
on people and materials that no amount of code can supply, and the plan kept generating code anyway.
The second developer identified this correctly and early: "We have reduced implementation uncertainty
far faster than musical uncertainty."

So the honest answer to "why has this taken a month" is: the first month bought a large, tested,
deterministic codebase and a precise map of what remains. That map is genuinely valuable — it is why
this document can be specific rather than speculative. But the map is now the asset, not the code. The
next month must be spent on the gates, and the automation below is how to shrink most of them.

## 2. Work completed in this session

The only failing CI job was diagnosed and repaired at the root, not silenced.

**Diagnosis.** The single red job was `native-platform-matrix (ubuntu-latest)`, failing
`seam_original_singer_song_journey_tests` on its 180-second CTest timeout. Three logs from that same
job prove it was throughput, not a stall:

| CI run | Result for that test |
|---|---|
| `35375628884` | `***Timeout 180.06 sec` |
| `35362687374` | `Passed 135.31 sec` |
| `35321985263` | `Passed 156.83 sec` |

A `sample(1)` profile of the Debug binary attributes roughly 98% of worker time to
`ArticulatedStream::renderOwned` -> `PhonationSource::render` ->
`CompiledScorePerformance::at/evaluate` and `applyCompiledPerformanceGain` — all of which run once
per output frame.

**Repair (`3276b7df`, pushed).** Two behaviour-preserving changes:

1. `CompiledScorePerformance::hasManualPerformance_` is set by the only factory that can construct
   the class, from the region's post-voice-filtering performance state. When no accepted selection or
   ownership record exists, every accepted and ownership bucket is empty, so `evaluate()` skips 36
   binary searches per output frame instead of repeating provably dead work.
2. `PhonationSource` substitutes the exact constant taper window `2.0` for `cos(pi * 0.0)` when a
   partial sits at or below the taper corner. `0.5 * (1.0 + cos(0.0))` is exactly `1.0` and both
   operands are exact powers of two, so the product is bit-identical.

`tests/test_framework.hpp` now prints and flushes a `[START]` line and an elapsed `[PASS]` line per
case. Previously a timed-out case printed nothing, which is why the CI log could not name the slow test.

**Measured result.** Debug, all four cases passing: 94.71s -> 62.37s. The full local Release suite
passes **172/172 under `-j8` in 117.70s**, with the song journey at 13.27s against its 135-160s CI
measurement. No test was weakened or removed; `SOURCE_CLOSURE=PASS`.

**Independent review.** The second-developer session verified both changes and reported KEEP for each,
including the subnormal and signed-zero cases and the `harmonic * frequency == 0.35 * rate` corner,
and confirmed that no construction path can produce a score performance with non-empty accepted or
ownership records while the flag is false.

## 3. What OpenUtau actually provides

Cloned to `/tmp/ou/OpenUtau`, MIT licensed, commit `83e02c7e`.

### 3.1 A reusable, machine-checkable oracle for pronunciation and lyric handling

This is the strongest reusable asset, and it is real rather than aspirational.
**`OpenUtau.Test/Plugins/` contains 117 golden phonemizer vectors** that run headlessly under Xunit:
EnToJa 56, EnArpaPlus 12, EnVCCV 8, Phonemizer 6, EnXSampa 6, EnArpa 6, DeVccv 6, DeDiphone 6,
ZhCvvc 4, JaVcv 4, JaCvvc 3. `PhonemizerTestBase` drives them through a real `ClassicSinger` and a
real voicebank fixture and asserts the exact produced aliases, for example lyric `お にょ ひょ` at
given tones must produce `- おA3`, `o にょCA3`, `o ひょD4` (`OpenUtau.Test/Plugins/JaVcvTest.cs:13-21`).

What this establishes: string-in to alias-out correctness for kana normalization (`か` + combining
dakuten -> `が`), consonant/vowel decomposition, and multi-note context handling. That is a genuine
oracle for the mechanical half of R7.

What it does not establish: any acoustic or prosodic claim. These are dictionary and rule tests. They
say nothing about whether the sung output sounds like Japanese.

Also reusable: OpenUtau ships Korean phonemizers (`BaseKoreanPhonemizer.cs`,
`KoreanCVPhonemizer.cs`, `KoreanVCVPhonemizer.cs`, `KoreanCVVCStandardPronunciationPhonemizer.cs`
and others) and a G2p directory covering arpabet, German, French, Italian, Japanese and more. These are
reference implementations suitable for cross-checking SEAM's own decompositions, under MIT terms with
naming.

### 3.2 A conformance fixture for interchange

`OpenUtau.Test/Classic/UstTest.cs` is substantive rather than a smoke test: it unpacks real-world UST
archives and asserts that loading produces valid note structures, and it specifically covers the legacy
edge cases that break naive importers — `=` inside lyrics, multi-point pitch bend curves (PBS/PBW/PBY),
and legacy vibrato parameters. `UstLoadingTest` and `EqualInLyric` are usable as conformance fixtures
for SEAM's R12 (USTX/SMF round-trip) and R13 interchange work.

### 3.3 What OpenUtau cannot do

It cannot replace a listening judgment, and two of its components are weaker than their names suggest:

- `cpp/worldline/worldline_test.cpp` is a hollow smoke test — it dlopens the library, asserts
  `PhraseSynthNew()` is non-null, and deletes it. It contains no acoustic assertion at all.
- The fixture voicebanks in `OpenUtau.Test/Files/` are metadata only (`oto.ini`, `character.yaml`,
  `prefix.map`). There is exactly one audio file in the entire fixture tree, `Files/sine.wav`.
  **OpenUtau therefore supplies no rights-clean singing audio**, so it cannot be used as a corpus.

One practical caveat: the suite needs the .NET SDK, which is not installed on this machine
(`dotnet` absent), so the vectors cannot be run as-is today. They are directly usable as fixtures by
extracting the expected aliases and asserting them against SEAM's own phonemizer output, which avoids
the toolchain dependency entirely.

## 4. The honest boundary: what automation can and cannot replace

This is the part where the answer must be exact, because the project's failure mode so far has been
optimistic evidence, and an equally optimistic "we will automate the gates" would repeat that error in
the opposite direction.

The distinction that matters:

**Automation can fully replace gates that are really determinism, coverage, conformance, or
mechanics.** If the requirement is "the same input produces the same output", "every declared language
has coverage", "the file round-trips without loss", or "the plugin loads in a host", a machine decides
it completely and better than a person.

**Automation can partly replace perceptual gates, as a negative screen.** A machine can prove that
audio is not silent, not clipping, not NaN, not pitch-wrong, not discontinuously spliced, and that its
phonetic content is recoverable. It can catch gross failure reliably at scale. It cannot prove
naturalness, and a metric-satisfying render can still sound robotic.

**Automation cannot replace two specific things.** It cannot produce an authorized recording, which is
a legal and physical fact. And it cannot supply a human's first-impression usability judgment, which is
by definition the observation of a person interacting without prior knowledge.

Section 6 applies this unit by unit.

## 5. Acceleration: how to run the remaining work with multiple agents

The parallelisable work is real but narrower than it looks, because most open units are gates. Three
lanes can proceed at once, and two of them are verification infrastructure rather than product code.

### Lane 1 — pronunciation and interchange conformance (highest value, lowest risk)

Port OpenUtau's 117 phonemizer vectors into SEAM's test suite as fixtures, asserting SEAM's
`resolvePronunciation` output against the expected aliases where the two projects model the same
decision, and recording the remainder as explicit divergences with reasons. Add the UST and USTX
conformance fixtures from `UstTest.cs` as R12 round-trip evidence. This directly strengthens R7, R11
and R12 with an independent implementation as the oracle, at no scope cost.

### Lane 2 — acoustic and phonetic triage

Extend the existing `tools/singing_quality/` work, which already implements frozen pitch criteria
(`pitch-median <= 30 cents`, `pitch-within-50 >= 90%` in
`tools/singing_quality/acoustic_metrics.py:10-11`), with a real ASR-based negative screen over the
retained packet. Record results explicitly as triage, never as perceptual acceptance.

### Lane 3 — host and platform qualification

Turn the Windows and macOS matrix into automated evidence using a headless host harness and plugin
validation, replacing the "owner sits in front of REAPER" step with a scripted run that retains
scan, session-log and bounce artifacts.

### Conditions for multi-agent work

The second developer's warning is correct and should be a hard rule:

- **One writer per file.** Shared worktrees, `CMakeLists.txt` and asset paths must have a single owner
  per change, or parallel agents will produce regressions that look like flaky tests.
- **One committer.** Agents propose diffs; a single integrator builds, runs the full suite and commits.
  This preserves the "one reviewable commit per unit" property R4 section 10 already requires.
- **No gate acceptance from an agent.** An agent may produce triage evidence; only the owner may
  convert a gate to accepted, and the contract's `evaluationProfile` decides when that is allowed.
- **Never weaken a test to make a lane green.** This session's own CI repair is the model: find the
  root cause and fix throughput, rather than raising the timeout.

## 6. Unit-by-unit verdict on the seven gates

| Gate | Automatable? | Strongest honest substitute | What stays human |
|---|---|---|---|
| **U1.4** creator observation | **No** | Headless workflow qualification: drive the real controller and document scene through a scripted session (add notes, edit lyric, apply expression, preview, export), asserting no blocking modal, no error path, and a preview-latency budget. Records "Workflow machinery: PASS"; the ergonomic claim stays open. | Whether a naive person finds it usable without coaching. |
| **U2.1** listening result | **Partly** | Negative screen over the retained packet: ASR intelligibility of the intended lyrics, pitch criteria already implemented, spectral-distance and continuity checks, plus pinned negative controls (silence, noise, glitch) that must fail. Records "Acoustic triage: PASS"; perceptual naturalness stays UNREVIEWED. | Whether it sounds good, and whether phrasing is musical. |
| **U3.5** learned-singer qualification | **Yes, for the technical half** | Admit a real ONNX candidate and run the existing qualification pipeline: pitch adherence, spectral distance, determinism, latency budget, vocoder receipt. The blocker is a rights-cleared model, not the harness. | Timbral verdict on the learned voice. |
| **U4.1** authorized recording | **No** | Use the generated-teacher path (`tools/voice_model_training/generated_teacher.py`) to exercise every downstream mechanic — marker extraction, take rejection, review invalidation, lineage — and declare the origin as synthetic. | The legal rights and the physical session. Nothing substitutes. |
| **U4.2b** native-speaker review | **Partly** | Automated coverage and phonotactic legality: 100% vocabulary coverage per declared language against an independent dictionary, plus OpenUtau cross-checks for kana and Hangul decomposition. Records "Phonetic coverage: PASS"; accent and naturalness stay UNREVIEWED. | Whether the pronunciation is acceptable to a native speaker. |
| **U4.3** style pair | **Yes, technically** | Produce two distinct style profiles with identical phone inventories from the procedural carrier (for example neutral versus soft/whisper phonation), then run the existing StyleBlend suite for crossfade continuity, phase continuity and missing-pair refusal. | Whether the styles are musically the right pair. |
| **U5.1** Windows host | **Mostly, with an important correction** | A headless host harness with plugin validation produces genuine non-human scan, session-log and bounce evidence, and the Windows CI job already exists (`native-platform-matrix (windows-latest)`, currently passing). | The `external-beta-host-matrix.json` contract currently names REAPER and Bitwig on real installed hosts with `screenshot` evidence, so a scripted harness strengthens the evidence but does not satisfy that contract as written. Changing it is a contract revision, not an engineering task. |

One correction to the second developer's summary, from reading the contract directly: U5.1 is not
"completely" automatable as stated. `docs/product/external-beta-host-matrix.json` requires REAPER and
Bitwig on both platforms in both CLAP and VST3, with `scan`, `session-log`, `bounce` **and
`screenshot`** evidence, and the gate is evaluated by
`tools/external_beta/full_product_contract_validation.py`. A headless harness can supply three of the
four evidence kinds; the fourth is defined by the contract. The right move is to propose an explicit
contract revision that accepts scripted host evidence, and to say plainly that it is a revision.

## 7. Recommended plan

Ordered so that each step either retires a real gate or produces an asset that does:

1. **Land the CI repair** (done, `3276b7df`) and confirm the Ubuntu job is green.
2. **Lane 1**: port the OpenUtau phonemizer vectors and UST/USTX conformance fixtures. This is
   independent verification of R7/R11/R12 and is the single highest-value remaining engineering task.
3. **Lane 2**: build the ASR negative screen over the retained packet, with pinned negative controls.
   Publish it as triage, with the wording the project already uses for unreviewed audio.
4. **U1.4 substitute**: build the headless workflow qualification and record it as machinery-only.
5. **U4.1 substitute**: exercise the full production pipeline from generated-teacher audio, labelling
   the origin as synthetic.
6. **U3.5**: admit a real candidate model the moment a rights-cleared one exists — the harness is ready.
7. **U5.1**: write the contract revision proposal for scripted host evidence, then implement the
   harness. Do not claim host acceptance under the current contract without the contract change.
8. **U2.1, U4.2b, U6**: these need people. Recruit a listener, a native speaker and five creators, and
   start that now rather than after the code — it is the long pole and it does not run faster later.

The realistic conclusion: steps 1-5 are achievable by agents in a short sequence of working sessions
and will move every automatable gate to done. Steps 6-8 are the irreducible ones, and they should be
started in parallel with the engineering rather than treated as the reward for finishing it.

## 8. What this document is not

It is not acceptance evidence for any unit, and it does not authorise release. It performs no
implementation beyond the CI repair named in section 2 and the ledger entry that records it. Where it
cites OpenUtau, the citation is to a specific file and, where given, a specific line. Where automation
cannot establish a claim, the claim is marked UNREVIEWED rather than passed.
