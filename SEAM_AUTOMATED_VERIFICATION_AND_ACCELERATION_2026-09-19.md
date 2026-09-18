# SEAM automated verification and acceleration plan

---
title: SEAM automated verification and acceleration plan
date: 2026-09-19
branch: codex/production-readiness-completion
baseline_commit: 3276b7df
followup_audit_baseline: 7839c72f
status: revised after source audit; implementation and validation recorded below
latest_followup_baseline: ba6dbfa086596cea66a89c9de95b7ec411406764
persistent_training_batch_baseline: cddf4f9f0a38ef83f6f1acbffc16f15b8bc1b6b3
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

The source audit gives a more useful answer than the roadmap labels alone: **there is still
valuable engineering to do, and some earlier verification was only a stub**. Three agents can repair
independent product paths while an integrator runs real audio evaluation and a second developer
reviews the evidence. Automated checks can resolve mechanical claims; their ability to predict
acceptable singing must itself be measured. No current machine result establishes Beta GO.

## 1. Corrected finding: the queue hides unfinished engineering inside gate labels

R4 section 9 presents the remaining top-level units as human or external gates:

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

An earlier version inferred that no further engineering could advance Beta GO. **That inference is
withdrawn.** A milestone's final sign-off dependency says nothing about whether the machinery below
it is complete or correct. The follow-up audit with the second developer found these counterexamples:

| Source defect at `7839c72f` | Consequence | Current repair |
|---|---|---|
| `scripts/compare_listening_packets.py::run_asr_triage` never opened audio; controls always assigned `detected = False` | Fabricated recognition confidence and unexecuted controls looked like evidence | Actual local recognizer, WAV hash verification, generated controls, schema-2 diagnostic results |
| English hint inventory omitted `ao`, although the shared classifier supported it | Valid English pronunciations were rejected | Inventory correction plus stressed/unstressed hint, continuation, and stale-edit regressions |
| USTX depth multiplied by ten; phase treated as degrees | A 25-cent vibrato became 200 cents; a 0.75-cycle export wrote invalid shift 270 | Upstream cents/percentage units and independent import/export assertions |
| USTX `sp` interpreted as Step | A smooth pitch segment became discontinuous | Smooth approximation with explicit loss; exact nonlinear preservation remains open |
| Plugin scene lacked performance/mouth binding; sample renders also published an empty implicit style | Available mouth artwork could not follow DAW playback | Carry the actual prepared style, bind immutable published cues to host time, and draw declared assets |

These findings explain a concrete weakness in the process: self-consistency tests and nominal report
fields did not establish the intended user behavior. A round trip can preserve a reciprocal unit
mistake; a test asserting hard-coded control success can pass without processing audio. They do not
justify a complete retrospective judgment about every week of development, but they do require a
change in how the remaining work is selected and verified.

The working rule is now: choose a usable singing outcome, inspect its complete production path, use
independent expectations, and retain the observed result. Engineering and external acceptance proceed
in parallel. Do not automatically raise the progress percentage when adding tests or repairing a bug;
reassess the corresponding contract requirement against its complete acceptance criteria.

### 1.1 Why the owner's concern is justified

The concern is justified: a month of activity is not an acceptable substitute for a demonstrated
singing product. This audit cannot reconstruct every hour of that month, but it identifies concrete
causes of wasted effort and misleading confidence:

1. **The main outcome was tested too indirectly.** Recognition confidence existed without a
   recognizer. The corrected run now exposes an unresolved result that should have been visible
   earlier. A passing infrastructure suite cannot answer whether a lyric song is understandable.
2. **Tests sometimes shared the implementation's assumptions.** Reciprocal USTX conversion errors
   survived round trips; independent upstream values found them. More tests of the same assumption
   would not have fixed this.
3. **Gate labels were used to stop engineering discovery.** An external final judgment was mistaken
   for proof that all prerequisite code was finished. The concrete defects above refute that inference.
4. **Integration discipline wasted build time.** Concurrent builds in one directory damaged generated
   dependency state. More agents are useful only when ownership and integration are controlled.
5. **A genuine research dependency remains.** A compatible learned singer, vocoder, usable source
   material, and held-out singing results cannot be obtained merely by adding admission machinery.
   This work needs actual candidate experiments and retained output.

The remedy is not to abandon testing. It is to spend verification effort on an installed singer and
the same 30–60-second song, then expand that proven workflow. Every implementation batch must show
what a creator can now do, what audio actually came out, and what remains unproven. Repairs and
documentation do not automatically count as additional completed roadmap units.

## 2. Initial CI repair and its historical evidence

The native-matrix timeout was diagnosed and repaired at the root. The separately parked coordinator
failure remains described in section 3.

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

## 3. CI result for the initial repair and what remained red

This section records run `35392919351`, not CI qualification of the later follow-up changes.

Run `35392919351` confirms the diagnosis and the fix. `native-platform-matrix (ubuntu-latest)` —
the only job that was red — now **passes**, together with the macOS and Windows matrix jobs and
`windows-helper-process`. `seam_original_singer_song_journey_tests` no longer appears in the failure
list, so the 180-second treadmill it had been on is cleared.

One job remains red: `isolated-release-candidate`, failing
`seam_authoring_render_coordinator_tests` and the `seam_tests` target that embeds it. This is the
pre-existing, documented GCC `-O3` flake, and it is **not** caused by this session's changes:

- It has failed and passed on unchanged sources across separate runs (`9bcfd9c9` failed, `04427155`
  passed), so it is intermittent by observation.
- The diagnostic signature differs between the two jobs in this single run — `progState=1, stale=0`
  in one and `progState=2, stale=1` in the other — which is a race's signature, not a deterministic
  defect.
- The affected test renders through the **sample-bank** path
  (`tests/test_authoring_render_coordinator.cpp:101-102` selects `ClassicPsola` and `Raw` unit
  renderers over `SEAM_SOURCE_PRODUCTION_VOICEBANK`). This session changed the **procedural** path
  only (`CompiledScorePerformance` and `PhonationSource`), which that test never enters.
- It passes 15 consecutive times on this machine's arm64 Release build.

The owner has explicitly parked this race, so it is reported as a fact rather than worked. It should be
fixed or its owning test split, because a red job makes every future green result harder to trust.

## 4. What OpenUtau actually provides

### 4.1 A reusable, machine-checkable oracle for pronunciation and lyric handling

Cloned to `/tmp/ou/OpenUtau`, MIT licensed, commit
[`83e02c7e4a4d9ea5fca72806b2aa27c5382be015`](https://github.com/openutau/OpenUtau/tree/83e02c7e4a4d9ea5fca72806b2aa27c5382be015).

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

### 4.2 A conformance fixture for interchange

`OpenUtau.Test/Classic/UstTest.cs` is substantive rather than a smoke test: it unpacks real-world UST
archives and asserts that loading produces valid note structures, and it specifically covers the legacy
edge cases that break naive importers — `=` inside lyrics, multi-point pitch bend curves (PBS/PBW/PBY),
and legacy vibrato parameters. These are **UST**, not USTX or SMF, and cannot directly establish
conformance of SEAM's supported importers. They are semantic references only unless legacy UST
support is separately implemented. The present changes instead use
`OpenUtau.Core/Ustx/UNote.cs`, `Render/RenderPhrase.cs`, and `Util/MusicMath.cs` at the pinned revision.

Primary implementation references:

- [UNote: vibrato units, pitch shapes, and note-relative coordinates](https://github.com/openutau/OpenUtau/blob/83e02c7e4a4d9ea5fca72806b2aa27c5382be015/OpenUtau.Core/Ustx/UNote.cs).
- [RenderPhrase: overlapping pitch-curve composition](https://github.com/openutau/OpenUtau/blob/83e02c7e4a4d9ea5fca72806b2aa27c5382be015/OpenUtau.Core/Render/RenderPhrase.cs).
- [UstxYamlTest: an independent negative-X fixture](https://github.com/openutau/OpenUtau/blob/83e02c7e4a4d9ea5fca72806b2aa27c5382be015/OpenUtau.Test/Core/USTx/UstxYamlTest.cs).
- [Phonemizer test inventory](https://github.com/openutau/OpenUtau/tree/83e02c7e4a4d9ea5fca72806b2aa27c5382be015/OpenUtau.Test/Plugins).

### 4.3 What OpenUtau cannot do

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

## 5. The honest boundary: what automation can and cannot replace

This is the part where the answer must be exact, because the project's failure mode so far has been
optimistic evidence, and an equally optimistic "we will automate the gates" would repeat that error in
the opposite direction.

The distinction that matters:

**Automation can fully replace gates that are really determinism, coverage, conformance, or
mechanics.** If the requirement is "the same input produces the same output", "every declared language
has coverage", "the file round-trips without loss", or "the plugin loads in a host", a machine decides
it completely and better than a person.

**Automation can partly replace perceptual gates, as a negative screen.** A machine can measure
silence, clipping, nonfinite samples, estimated pitch error, splice discontinuities, and recovered
text. Exact sample properties are distinct from fallible pitch/transcription estimates. Calibrated
metrics can catch gross failures at scale, but a successful metric result does not prove naturalness;
a metric-satisfying render can still sound robotic.

**Automation cannot replace two specific things.** It cannot produce an authorized recording, which is
a legal and physical fact. And it cannot supply a human's first-impression usability judgment, which is
by definition the observation of a person interacting without prior knowledge.

Section 7 applies this unit by unit.

## 6. Acceleration: how to run the remaining work with multiple agents

Parallelism should follow independent production paths, not the labels on the remaining gates.
This session used three implementation agents for pronunciation, interchange, and plugin character
binding; the integrator owned real audio evaluation and cross-cutting renderer integration. The next
work can follow the three lanes below, with explicit file ownership and a single build owner.

### Lane 1 — pronunciation and interchange conformance (highest value, lowest risk)

Select comparable decisions from OpenUtau's fixtures and source, with explicit inventory mapping.
Assert actual SEAM phones, roles, ownership, and context; bank-specific oto aliases are not phonemes.
Use independently specified USTX wire values for import and export so reciprocal errors cannot hide
inside a round trip. Record unsupported behavior as loss or refusal. The 117 upstream cases are a
reference inventory, not a promise that 117 directly portable SEAM cases exist.

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

## 7. Unit-by-unit verdict on the remaining gates

| Gate | Automatable? | Strongest honest substitute | What stays human |
|---|---|---|---|
| **U1.4** creator observation | **No** | Headless workflow qualification: drive the real controller and document scene through a scripted session (add notes, edit lyric, apply expression, preview, export), asserting no blocking modal, no error path, and a preview-latency budget. Records "Workflow machinery: PASS"; the ergonomic claim stays open. | Whether a naive person finds it usable without coaching. |
| **U2.1** listening result | **Partly** | Negative screen over retained audio: actual transcription, optional text comparison, existing pitch measurements, spectral/continuity diagnostics, and executed controls. Record the measurements and failures; do not issue an ASR-based acceptance verdict. | Whether it sounds good, and whether phrasing is musical. |
| **U3.5** learned-singer qualification | **Yes, for the technical half** | Admit a real ONNX candidate and run the existing qualification pipeline: pitch adherence, spectral distance, determinism, latency budget, vocoder receipt. The blocker is a rights-cleared model, not the harness. | Timbral verdict on the learned voice. |
| **U4.1** authorized recording | **No** | Use the generated-teacher path (`tools/voice_model_training/generated_teacher.py`) to exercise every downstream mechanic — marker extraction, take rejection, review invalidation, lineage — and declare the origin as synthetic. | The legal rights and the physical session. Nothing substitutes. |
| **U4.2b** native-speaker review | **Partly** | Automated coverage and phonotactic legality: 100% vocabulary coverage per declared language against an independent dictionary, plus OpenUtau cross-checks for kana and Hangul decomposition. Records "Phonetic coverage: PASS"; accent and naturalness stay UNREVIEWED. | Whether the pronunciation is acceptable to a native speaker. |
| **U4.3** style pair | **Yes, technically** | Produce two distinct style profiles with identical phone inventories from the procedural carrier (for example neutral versus soft/whisper phonation), then run the existing StyleBlend suite for crossfade continuity, phase continuity and missing-pair refusal. | Whether the styles are musically the right pair. |
| **U5.1** Windows host | **Mostly** | Automate the named, installed DAWs and retain real scan, session-log, bounce, and screenshot evidence. A generic headless host provides an additional mechanical check. | Actual named-host installation and full required artifacts remain necessary. A generic harness alone does not prove REAPER/Bitwig behavior. |
| **M6** independent creator cohort | **Partly** | Automate installation, instrumentation, task capture, artifact checks, and analysis. Agent sessions can reveal workflow defects before recruitment. | Agents do not count as five independent human creators under the current contract. |

`docs/product/external-beta-host-matrix.json` requires REAPER and Bitwig on both platforms in both
CLAP and VST3, with `scan`, `session-log`, `bounce` and `screenshot` evidence. A screenshot can itself
be automated. Thus scripted execution in the actual named hosts need not imply a contract revision;
substituting a different headless host or omitting required artifacts would. The evaluator remains
`tools/external_beta/full_product_contract_validation.py`.

## 8. Recommended plan

Ordered so that each step either retires a real gate or produces an asset that does:

1. **Land the CI repair** (done, `3276b7df`) and confirm the Ubuntu job is green.
2. **Lane 1**: repair comparable pronunciation and USTX semantics using pinned OpenUtau source.
   `71f99a68` started with 14 Japanese exact-symbol cases. The follow-up expands Japanese to 18
   inputs, adds selected English/Korean mappings, fixes English `ao` admission and USTX vibrato
   units, and reports curve approximations. Signed pitch offsets and pickups in rests now survive
   import/export. Cross-note portamento composition, neighbor-note `snap_first`, and exact nonlinear
   curve preservation still need implementation before broad USTX claims.
3. **Lane 2**: build the ASR negative screen over the retained packet, with pinned negative controls.
   Publish it as triage, with the wording the project already uses for unreviewed audio.
4. **U1.4 mechanical companion**: extend the existing installed-song/controller journeys to cover
   missing actions; record them as machinery-only, not a replacement for the human observation.
5. **U4.1 mechanical companion**: exercise the full production pipeline from generated-teacher audio, labelling
   the origin as synthetic.
6. **U3.5**: admit a real candidate model the moment a rights-cleared one exists — the harness is ready.
7. **U5.1**: automate the actual named host matrix, including screenshots. Use a generic plugin
   harness for additional coverage. Propose a contract revision only if substituting required hosts
   or evidence kinds, not merely because execution is automated.
8. **U2.1, U4.2b, M6**: these need people under the existing contract. Recruit a listener, a native speaker and five creators, and
   start that now rather than after the code — it is the long pole and it does not run faster later.

These are work streams, not completion promises. Each needs its own inspected production path and
observed acceptance evidence. External inputs can be prepared alongside the engineering. A scripted
workflow still needs installed-surface coverage; generated teacher success still needs a learned
singer and an audio-quality result. No generic agent count or elapsed-time estimate resolves those.

## 9. Evidence correction and current implementation

The previous `packet-01/asr-triage.json` is retained unchanged for audit history. Its schema-1
`confidence` and `PINNED_HELD` fields are **invalid as recognition or negative-control evidence**:
the producing code never opened audio or invoked a recognizer. No later report may use them to
establish intelligibility. Schema 2 records actual transcripts, model-file hashes, runtime versions,
fixed unprompted decoding, WAV hashes, executed control results, and optional character error rates.

Character error rate is orthographic only. Japanese kanji/kana alternatives, held vowels, isolated
morae, and singing differ from normal speech recognition; disagreement cannot by itself diagnose a
singer defect. Empty output means no text was recognized under the recorded configuration. Negative
controls alone do not measure sensitivity to intelligible singing. A control hallucination makes the
run's calibration fail and the CLI exit nonzero, while retaining its diagnostic output. No fabricated
confidence, readiness rating, or perceptual acceptance is emitted.

The optional recognizer is [faster-whisper](https://github.com/SYSTRAN/faster-whisper) with a local
[base-model snapshot](https://huggingface.co/Systran/faster-whisper-base/tree/ebe41f70d5b6dfa9166e2c581c45c9c0cfc57b66).
Model files are fetched separately; the runner itself operates locally. The implementation is
`tools/singing_quality/asr_triage.py`; usage and evidence limits are in `tools/singing_quality/README.md`.

### 9.1 Actual recognition run, 2026-09-19

Executed on all 66 WAVs in `/Users/lhs/seam-listening-reference/packet-01`, without changing that
packet. Backend: faster-whisper 1.2.1, CTranslate2 4.8.2, CPU int8, four threads, fixed Japanese
decoding. Model `model.bin` SHA-256:
`d01c3014881c9c6f3133c182f3d2887eb6ca1c789a7538c5c007196857a0a6a9`.
Runner SHA-256:
`f62684d585090f711ec7d98d7da2e292748f6f216cd7759610f2bf3d68b2b75a`.

| Observation | Result | Interpretation |
|---|---|---|
| Actual retained WAVs transcribed | 66/66 | Executed inference, with each audio hash checked |
| Empty transcripts | 31/66 | No text recognized in this configuration; not an intelligibility score |
| Nonempty transcripts | 35/66 | Includes repetitive/hallucinated text; not 35 successful lyric recognitions |
| Silence, white noise, impulse controls | 3/3 produced no text | This negative screen held on these three signals |
| Complete-song outputs | 0/6 exact normalized matches to intended kana text | Diagnostic failure to recover the reference text; no broad perceptual verdict |
| Independent local Japanese speech control | Normalized CER 0.0 | The same backend/settings can transcribe this known speech example |

The original packet run deliberately retained unprompted transcripts without expected-text input:
its `textComparedItems` is therefore **0**, not 66. The complete-song comparison above was computed
after capture with the same `character_error_rate` function, against the 29 kana from
`tools/singing_quality/listening_packet.py::CASES`, joining note lyrics and omitting the terminal
continuation marker. CER ranges from 2.6552 to 7.6552 (insertion errors can exceed 1.0). Baseline
master output repeatedly mentions ice cream; other variants repeatedly output unrelated words or
the same kana. These are observed transcripts, not claims that the rendered singer literally sang
those words. Orthographic alternatives and speech-to-singing domain shift still limit interpretation.

The positive speech control was generated locally using macOS `say`, voice Kyoko, rate 160, mono
PCM16 at 16 kHz, reading: `今日は晴れです。私は音楽を作っています。明日も一緒に歌いましょう。`.
Its WAV SHA-256 is `28036f65de7a8fbccbf73eb158cfefad3d55f57075521e5f15aeeb3f678a6ab0`.
The returned text differs only in punctuation. This is a diagnostic speech control, not a singing
voicebank, training corpus, or proof that Whisper is calibrated for singing. No control audio is
redistributed in the repository.

Retained artifacts:

- `/Users/lhs/seam-listening-reference/packet-01-asr-schema2-2026-09-19.json`
  — SHA-256 `df630e040f40013231bc445e5dea2ea49366d8911478bf58547dcdeae64bac1f`.
- `/Users/lhs/seam-listening-reference/japanese-speech-control-asr-schema2-2026-09-19.json`
  — SHA-256 `f3551018282d6b4b8ccb56e2f8f3504d8ff2e8f52ce55e258b8d6c653048f71b`.
- `/Users/lhs/seam-listening-reference/speech-control-2026-09-19/`
  — control WAV, manifest and exact expected text. Historical packet/report files remain unchanged.

The immediate conclusion is that the former metadata-only ASR report was masking an unresolved
recognition problem. Next, compare the same backend with independently intelligible singing and a
stronger recognizer before attributing every failure to SEAM's articulation. Meanwhile, production
timing/pitch/pronunciation/interchange fixes can proceed on their own evidence.

Pronunciation attribution and the upstream MIT text are in
`libs/seam-phonemizer/OPENUTAU_REFERENCE_NOTICE.md`. USTX tests refer to the same OpenUtau commit,
with explicit independent wire expectations. Current changes make selected product paths more
correct; they do not complete all 117 reference cases, exact OpenUtau compatibility, or Beta GO.

## 10. Next development batches and concrete exit evidence

Use three implementation agents with distinct files, one integrator, and the existing second-developer
task for review. The cap remains below the owner's limit of 30. A shared build directory also needs
one owner: separate source ownership does not make concurrent Ninja invocations safe.

| Batch | Code and intended change | Evidence required before calling the batch complete |
|---|---|---|
| A — faithful authoring/interchange | Build on the now-supported signed offsets/rest pickups: finish previous-note absolute-pitch composition, exact nonlinear curve handling, and `snap_first` in `libs/seam-interchange`; extend controller/import service journeys | Independent OpenUtau wire cases and sampled pitch values across note boundaries; preserved notes/lyrics/tempo/part offsets; explicit losses for genuinely unsupported fields; import → tune → save/reopen → export on the production route |
| B — useful audio diagnosis | Calibrate `tools/singing_quality/asr_triage.py` with known intelligible speech, rights-documented intelligible singing, synthetic failures, and the retained SEAM packet; combine with existing pitch/timing diagnostics | Real transcripts and model/audio hashes; false-positive/false-negative observations; declared limits per language; no acceptance from self-derived reference labels or source loudness |
| C — installed creator surfaces | Extend existing `tests/test_original_singer_song_journey.cpp`, plugin editor tests, and native controller journeys instead of creating a second rendering stack | One original singer and 30–60-second lyric song; edit/undo/redo, preview, transport seek/loop, save/reopen, cancellation, and decoded export. Record measured latency and test conditions; do not invent a 50 ms render target or call an agent an unaided human creator |
| D — deployable singer and host matrix | Run the actual generated-teacher → corpus → train → ONNX → vocoder → admitted singer route, alongside scripted named-DAW installation/scan/session/bounce/screenshots | A retained compatible learned-singer artifact with truthful source rights/provenance and held-out rendering; required platform/DAW artifacts. Arithmetic fixtures, teacher reconstruction, and a generic plugin host do not close the corresponding full-product requirement |

Batches A–C can expose product defects without waiting for a listening verdict. Batch D should begin
with an inventory of actual usable models/corpora/runtime artifacts, not another layer of hypothetical
admission documents. If a needed input is absent, keep working on independent mechanics and identify
the exact missing artifact. Stop adding new acceptance abstractions unless a concrete executed path
demonstrates that they are necessary.

Release acceptance remains the existing full-product contract. Proposed changes to that contract
must be shown explicitly; this execution plan changes work ordering and evidence quality, not the
definition of the user's virtual-singer goal.

## 11. How to use outside materials without creating another verification shortcut

| Material | Appropriate use in the next batch | What it does not establish |
|---|---|---|
| Pinned OpenUtau source and test fixtures | Independently specified pitch/vibrato/lyric cases; compare intermediate phonemes and sampled pitch curves; retain attribution and inventory mappings | Availability of a singing corpus, identical bank aliases, acoustic quality, or complete compatibility |
| Rights-documented external singing recordings | Positive ASR calibration and pitch/timing diagnostics using retained source, license, hash, transcript, and evaluation-only purpose | Permission to train a singer, redistribute samples, or reproduce a person's voice merely because the file can be downloaded |
| Another service's public documentation and example projects | Reproduce note-entry, tuning, transport, and export workflows against SEAM's actual installed surfaces | Access to private source code or proof that SEAM implements the documented behavior |
| A compatible public model or vocoder | Inspect the actual input/output tensors, sample rate, hop, vocabulary, runtime support, and applicable usage terms before running held-out inference | Drop-in compatibility or commercial redistribution rights from the repository's top-level code license alone |
| Codex-driven UI and source review | Repeatable journeys, visual/layout inspections, exception detection, independent assertions, and review of retained output | Five independent creators, native-speaker acceptance, or evidence from a model that never actually consumed the audio |

For an audio-quality comparison, freeze the candidate and comparison inputs first. Run the same
recognizer/settings on an independently intelligible singing control, deliberately degraded controls,
and the candidate; retain failures as well as successes. Use a second recognizer to investigate
disagreement, not to select whichever score looks best. A known lyric must not be supplied as the
decoder's prompt and then counted as successful recognition. Compare speech and singing separately.

The throughput rule is equally concrete: each batch should end with a usable behavior and its
executed evidence, not another framework for accepting hypothetical evidence. Run focused tests per
agent; run the full suite once on the integrated source. After a shared-header change, rebuild all
affected consumers. Never run two build processes in the same build directory. In this session,
concurrent builds damaged Ninja's generated dependency database and forced substantial rebuilding;
the database was preserved outside the build tree, regenerated, and an unchanged build subsequently
completed in 0.195 seconds. This was avoidable integration overhead, not product development.

## 12. Integrated verification and handoff

The follow-up source is based on `7839c72f`, with the repairs described in sections 1 and 9.
Verification ran locally on arm64 macOS in the existing Release configuration:

| Check | Observed result |
|---|---|
| Full native build: `cmake --build build/release --parallel 8` | PASS; unchanged follow-up build reports `ninja: no work to do` |
| First full suite: `ctest --test-dir build/release --output-on-failure -j8` | 171/172 passed in 127.94 seconds; `seam_neural_native_owned_bytes` timed out at 20.02 seconds |
| Unchanged isolated timeout investigation | The same native-owned-bytes test passed five consecutive runs in 0.04–0.08 seconds each; 0.24 seconds total |
| Second complete suite, unchanged source and same `-j8` | **172/172 passed in 91.01 seconds**; native-owned-bytes passed in 0.05 seconds |
| CLAP mouth mutation | Removing only the mouth-artwork pointer failed the exact pixel assertion; restoring it passed |
| Pronunciation and USTX regressions | Independent wire/phone expectations failed before their fixes and pass after integration; USTX has 14 cases |
| Actual ASR execution | 66 retained WAVs plus generated negative controls; separate positive speech-control execution; results in section 9.1 |
| Source closure and whitespace | `SOURCE_CLOSURE=PASS`; `git diff --check` and staged diff check pass |

The first timeout is not erased by the successful rerun. Its cause was not established, and neither
its source nor its timeout was changed. It is distinct from the already parked coordinator race;
that race was not worked. The full-suite result is local evidence, not a claim that a new remote CI
run, the named installed-DAW matrix, or the Beta contract has passed.

The second-developer task reviewed the revised scope, evidence interpretation, OpenUtau references,
and sequencing and reported no remaining overclaims in that review. Its final review was read-only:
it did not independently rerun the full suite or the audio inference. The integration results above
come from the actual local executions, not from that review statement.

Remaining limits are explicit: nonlinear/cross-note USTX semantics are not fully preserved; the
CLAP regression binds a test character card and does not qualify the demo bank's character identity;
ASR is not calibrated for singing; a deployed learned singer, complete installed-host evidence, and
the required human observations remain open. The implementation goal continues. No full-scope
roadmap unit or Beta GO gate is newly accepted solely because this repair batch passed.

## 13. Executed next batch: production defects, not hypothetical gates

This follow-up starts from `ba6dbfa0`. Two implementation agents worked independently
on interchange and vocoder evaluation; the integrator repaired captured-source intake
and teacher labels. The requested second-developer task reviewed the evidence and
agreed to the corrections below. No second build process shared the Release directory.

### 13.1 What actually changed

| Production path | Reproduced defect | Repair and limits |
|---|---|---|
| USTX → compiled pitch | Previous/current tuning and negative-offset contours were not composed correctly; `snap_first` was not materialized | Sparse bounded linear composition using absolute cents, complete tempo conversion and ties-to-even endpoints; explicit losses remain for nonlinear/polyphonic cases and sub-tick discontinuities |
| Compiled pitch at a note boundary | The last frame of a note sampled the next note's offset against the previous MIDI base: **5800 instead of 6199.270833 cents** in the regression | Retain note tick bounds and clamp manual pitch sampling to the active note; compiler revision 14 → 15 invalidates stale caches; non-pitch controls unchanged |
| Captured SEAM WAV → training targets | Source inspection rejected the renderer's own IEEE float32 WAVs | Bounded float32/extensible-WAV inspection and exact-byte cropping; no clipping/resampling; integer identities unchanged and float encoding included in its identity |
| Candidate phones → score supervision | One phone became one note; extra consonants/vowels silently became rests after MIDI inputs ran out | Group canonical marker keys by note, preserve multi-phone syllables and continuation ownership, require exact note counts, verify WAV and native pitch hashes/grid/settings |
| Real Torch vocoder → evaluation | NumPy conversion could crash on gradient-bearing output; minimum-length slicing hid missing audio; unknown pitch could satisfy reconstruction | `eval`/`no_grad`, detached CPU float32 capture, exact hop/padding lengths, unresolved-pitch status, retained per-item WAVs and complete receipts |
| Reviewed epoch → held-out inputs | Caller-supplied tensor dictionaries were not a demonstrated held-out dataset path | Select validation/test source IDs; obtain exact source/target/conditioning bytes through the existing admitted batch reader |

The OpenUtau oracle remains commit `83e02c7e4a4d9ea5fca72806b2aa27c5382be015`,
specifically `UNote.cs:35–36,108–114`, `RenderPhrase.cs:301–352`, and
`TimeAxis.cs:222–230`. These provided independent pitch-composition and rounding
expectations; see the [pinned renderer source](https://github.com/openutau/OpenUtau/blob/83e02c7e4a4d9ea5fca72806b2aa27c5382be015/OpenUtau.Core/Render/RenderPhrase.cs).
Four composition regressions failed before implementation. The production sampler
then exposed the separate last-frame error. Final focused tests cover tuning,
touching notes, gaps, tempo changes, four sample rates, silence/tails and ordinary
controls. Exact imported melisma pitch is **not** claimed: the existing automatic
glide can still combine with authored offsets. Step-edge export remains explicitly lossy.

### 13.2 Real audio executions and what they establish

**SEAM captured teacher.** The retained original float32 render at
`/Users/lhs/Downloads/seam-listening-artifacts/2026-09-15-d1-02/unfamiliar-song/baseline/candidates/0000000000016379-000000000001637a.wav`
has SHA-256 `6bea61ba18acc3861111479ce3be93f99cf20bb27e502c53f8682876cb4bf13a`.
The normal acoustic-target CLI now consumes its exact bytes and produces 3,000 × 80
mel values. Native pitch extraction, the repaired teacher adapter, the ordinary
label-config inspector and frame conditioning also execute successfully:

- 48 kHz, 768,000 samples / 16 seconds, 3,000 analysis frames;
- 50 phones → 30 captured notes → 29 syllables, including one continuation;
- `labelOrigin=renderer-intent-not-acoustic-truth`; no rights/label/training approval;
- note intervals follow captured renderer-marker ownership, not the original
  piano-roll clock. This distinction matters for anticipated consonants.

This is a usable data-path repair. It is not proof of good articulation, a training
run, a learned singer, or a deployable voicebank. The adapter currently covers
Japanese; ambiguous/uncovered inputs reject rather than inventing supervision.

**Independent speech/singing controls.** Downloaded only the two small PJS samples
linked by its authors, Junya Koguchi and Shinnosuke Takamichi. Their
[publisher page](https://sites.google.com/site/shinnosuketakamichi/research-topics/pjs_corpus)
identifies 48 kHz speech/singing material and CC BY-SA 4.0 terms. This experiment uses
the samples locally for evaluation, not training or redistribution. Original URLs,
attribution, hashes and the evaluation-only purpose are retained in `manifest.json`.
No full corpus was downloaded and no original female singer was qualified.

The base recognizer returned lyric-related but imperfect text for the song and
speech. Its three generated negative controls returned no text. This single
speech/song pair is not enough to estimate sensitivity, specificity or language-wide
quality thresholds; unusual proper nouns and orthography further confound exact match.

The stronger [faster-whisper-small model](https://huggingface.co/Systran/faster-whisper-small),
pinned at `536b0662742c02347bc0e980a01041f333bce120`, produced text on **all three
negative controls**: silence, white noise and an impulse. The report correctly
records `triage_control_breach`, and the command exits 3. Its model.bin hash is
`3e305921506d8872816023e4c273e75d2419fb89b24da97b4fe7bce14170d671`.
Do not choose this run because a singing transcript looks better, suppress its
controls, or treat a larger model as inherently more reliable. Further recognizer
work must predeclare decoding/control rules and validate them on separate material.

**Actual upstream vocoder execution.** The new bounded
`tools/voice_model_training/check_vocoder_reconstruction.py` executed pinned
SingingVocoders `4d0889c4c180c75ad3000cc565864656344f8190`, using a 48 kHz/80-mel/
256-hop/1024-FFT MiniNSF configuration and native measured F0. A PJS song crop
starting at sample 48,000 contains 48,037 valid samples. The model produces
188 hops / 48,128 samples; the retained output contains exactly 48,037 samples,
accounting for 91 padding samples. This tests the real Torch adapter, not a NumPy mock.

Tensor/audio execution passes. **Untrained reconstruction correctly fails**:
spectral distance 51.173231 and whole-phrase median pitch difference 832.909 cents.
Output WAV SHA-256 is `754921fd27dc59761b2006a69327beacdc9257f0caf1bcdec44bd539d0c31534`.
About 427 KB of diagnostic artifacts are retained. This experiment did not train,
load learned weights, export ONNX, execute a native learned bundle, or establish a
held-out singing study. The separately observed complete GAN checkpoint is about
553 MB; earlier “under 10 MB” full-checkpoint estimates were incorrect.

### 13.3 Retained evidence

Root: `/Users/lhs/seam-listening-reference/pjs-calibration-2026-09-19-tsamkb/`.
These files are local evaluation artifacts, not Git-distributed audio assets.

| Relative artifact | SHA-256 |
|---|---|
| `manifest.json` | `b44600cdebffb81615e2f50c88ec8e7b56f88ea4cd38a4aafefd68ba42fabd6d` |
| `asr-base-unprompted.json` | `66f9b9dd2cbde5c03b62dd0f7bc12e86325f4af603aed7d9fbb5381562e8a400` |
| `asr-small-unprompted.json` | `b1b6f439586b5964ae13c497e9a409658c2403ba7ceca3c530f9140ad0179c79` |
| `real-teacher-labels/check.json` | `ce43e4b5a5f73028c914cffb93d9a897a9447a8017498bcd59d197cdef47e8cf` |
| `real-teacher-labels/inspection.json` | `875048c13b71edcf5d8a5fbd5884d9b0af0ad48246045718c666577f085b9e27` |
| `real-teacher-acoustic-targets/target.json` | `5c4f44c4a8ea40d46fe0bea4a3f17b29b7c894f713a2bd49eab6e34907c12598` |
| `untrained-vocoder-reconstruction-v2/check.json` | `abe79e9d03a382eca6688e016e1855d7937557728e5e03818ba965a8becdc25e` |

`capture_retained_teacher.py` in the same root reproduces the captured-teacher
experiment against a new output directory. Original input bytes and failed ASR
results are retained unchanged. The README documents the upstream diagnostic command.

### 13.4 Integrated verification

- Complete Release build: PASS, 380 build actions after the compiler-header change.
- Full CTest: **171/172 PASS in 85.58 seconds**. The sole failure was source closure:
  the new reconstruction diagnostic was not yet in the Git index. It was staged;
  `SOURCE_CLOSURE=PASS` and the failed CTest then passed in 0.25 seconds. This is
  a combined full-run + one-check rerun result, not a claimed all-green single run.
- Optional neural-environment Python discovery: **115 tests PASS in 31.805 seconds**.
- Focused captured-audio/labels/features tests: **34 PASS**; agent vocoder tests:
  **18 PASS**; focused USTX **21 cases** and performance compiler **20 cases** pass.
  Counts overlap and must not be added into a fabricated independent-test total.
- Independent source review found no issue in float32 intake/cropping or teacher
  grouping/source binding. This was source review, not another runtime execution.
- No timeout, assertion or approval policy was weakened. The parked coordinator
  race was not modified. No new remote CI or installed-DAW pass is claimed here.

## 14. Agreed next execution order: three capability lanes

This section updates section 10's immediate order using the executed results.
Keep one implementation owner per lane, one integrator, and the existing
second-developer task as a read-only reviewer. Do not spawn agents to write
parallel versions of the same acceptance document. The broad contract is unchanged.

### Lane A — retain and deploy an actually trained candidate

**Priority: highest.** The missing deliverable is learned output, not another
demonstration that an untrained model can execute.

1. Add a persistent, bounded vocoder training entrypoint around
   `vocoder_training_run.py`, `vocoder_checkpoint.py`, `vocoder_batches.py`, and
   the existing pinned upstream model configuration. Reuse current admission and
   checkpoint services; do not introduce a second training architecture.
2. Accept explicit dataset/config/profile identities, CPU thread count, update/time/
   disk bounds, seed, resume checkpoint and a new destination. Record the actual
   model/optimizer/scheduler/configuration and completed-epoch identity. Cancellation
   must preserve the last complete checkpoint and label partial attempts honestly.
3. Assemble a small, source-documented corpus through the existing path. Keep teacher
   reconstruction separate from independent singing evaluation; split by song and
   shared source lineage, never random frames. Do not fabricate review signatures.
   Determine exact authority/input gaps while continuing independent implementation.
4. Train, stop, resume, and retain both model artifacts and held-out WAVs. Report loss
   movement and complete reconstruction results. A lower training loss is not an
   intelligibility result; use a predeclared finite experiment budget.
5. Export the retained checkpoint using the existing vocoder/acoustic exporters;
   compare Torch/ONNX outputs on the same captured tensors, then feed the actual
   compatible bundle through `libs/seam-neural-synthesis` and the normal worker.
   Reuse the native route already present; do not count arithmetic fixtures as new
   learned-model deployment evidence.

**Exit artifact:** retained checkpoints, reproducible resume command, compatible
ONNX bundle, Python/native comparison, and held-out decoded song audio with exact
hashes and truthful provenance. This closes an engineering experiment only until
the full singer requirement's musical and release criteria are independently met.

### Lane B — preserve authored music across import and editing

Build on the linear cross-note/snap work already completed, rather than scheduling
it again. Own `libs/seam-interchange`, the necessary narrow compiler changes, and
their production-journey tests.

1. Resolve imported manual-pitch versus automatic melisma-glide ownership using a
   failing actual-sampler case. Do not remove native SEAM glides globally.
2. Implement the next bounded nonlinear curve semantics against pinned OpenUtau
   values, or retain explicit loss. Examine Step export and polyphonic limits
   separately; no blanket “lossless round trip” target is justified.
3. Exercise a multi-part score through import → tune → undo/redo → save/reopen →
   export. Assert independent sampled pitch and note/lyric/tempo/part identities,
   not just importer/exporter agreement.

**Exit artifact:** runnable production journey and a field-by-field supported/lossy
matrix backed by values from the OpenUtau implementation. Exact compatibility is
claimed only for the cases actually represented.

### Lane C — make automation useful on the installed song workflow

Own diagnostics and installed-surface journeys, not model training or interchange.

1. Keep base ASR as an observed diagnostic on these controls, not a universally
   correct rejection screen. Add independently sourced singing material across
   phones/ranges, negative/degraded versions and predeclared settings. Report
   control breaches and disagreement; do not tune until a preferred singer passes.
2. Extend pitch evaluation from whole-phrase medians to time-aligned voiced frames,
   retaining voicing disagreement, unmeasurable spans and note-level errors. A song
   with reversed notes can preserve its median, so the present 50-cent aggregate
   tolerance cannot establish melodic fidelity.
3. Run the same 30–60-second lyric song on the installed standalone/available named
   DAWs: input, tuning, preview, seek/loop, save/reopen and decoded bounce. Capture
   real timings, screenshots and errors with the actual installed singer. Clearly
   separate scripted-host results from the remaining platform/DAW matrix.

**Exit artifact:** reproducible installed-surface song journey and an audio-diagnostic
report that demonstrates its own limits. Agent-driven UI runs do not count as five
independent creators or native-speaker acceptance.

### What automation replaces—and what it does not

Codex plus independent open-source expectations can perform source review, execute
fixtures and UI journeys, validate hashes/clocks/shapes, detect numerical regressions,
measure timing, compare native/ONNX output and screen audio for suspicious results.
That removes a large amount of repetitive human QA. It must actually consume the
inputs and fail when deliberately corrupted; naming a tool is not verification.

The current evidence does **not** support replacing final lyric intelligibility,
naturalness, musical phrasing, unaided-creator usability or source authorization
with machine scores. This is an evidence limit, not a claim that such assessments
can never improve. Existing explicit human/rights requirements cannot be silently
redefined. Obtain those observations against a frozen candidate while the three
engineering lanes continue; do not let them stop unrelated development.

The next progress claim must name what a creator can do or what learned audio was
retained. Neither this repair batch nor another large test count completes R9,
U3.5 or Beta GO. A new percentage is unwarranted without re-evaluating the complete
requirement denominator. The central schedule risk remains the quality of an actual
original singer and its source/model path, not the amount of remaining documentation.

## 15. Next implementation completed: persistent training and imported glide ownership

This batch starts from `cddf4f9f` and executes parts of lanes A and B above. It does
not reclassify the original-singer requirement as an oscillator reconstruction task.
Two agents worked on independent orchestration and interchange paths; the integrator
implemented the training CLI and exercised actual upstream training/export.

### 15.1 Persistent vocoder command

`tools/voice_model_training/train_vocoder.py` is now a usable CPU entrypoint over
the existing reviewed dataset, batch reader, GAN optimizer and checkpoint services.
`vocoder_epochs.py` retains each complete epoch and its held-out reconstruction in
a new run directory. It supports explicit additional epochs, a captured resume
receipt, seeded execution, bounded threads, cooperative time/cancellation limits,
and cumulative checkpoint-byte limits. Config/commands and failure semantics are
documented in `tools/voice_model_training/README.md` under “Persistent vocoder GAN
training and resume.”

Resume restores the generator, discriminator buffers, both optimizers, both
schedulers, and Python/NumPy/Torch random states. It requires the same captured
inputs and selected library versions; source/label admission is checked again.
Earlier complete checkpoints survive a failed or interrupted later epoch. Partial
files do not have a completion receipt and cannot be promoted as success.

The former 512 MiB limit was per file, while a complete GAN has two state files.
The new aggregate limit is checked **before each write** across both streams and
against the run's remaining binary-checkpoint budget. An exact asymmetric-file
budget succeeds; a one-byte-smaller budget fails without exceeding it. JSON and
reconstruction audio are explicitly outside this binary budget and need separate
disk provisioning. Deadlines are cooperative and begin after model initialization;
they are not hard OS-level resource isolation.

### 15.2 Actual training → restart → retained ONNX execution

Executed the new production command using pinned SingingVocoders on the existing
original oscillator engineering fixtures. These are short synthetic waveforms with
explicitly public **fixture-only** review keys, not human voice, singing supervision,
production rights authority or an admitted original-singer corpus. All fixture data,
commands, logs, checkpoints and held-out audio are retained at:

`/Users/lhs/seam-listening-reference/vocoder-runner-2026-09-19-attempt1/`

The repeatable entrypoint is `python -B -m
tools.voice_model_training.check_vocoder_train_command --trusted-checkout CHECKOUT
--output NEW_DIRECTORY`. It reuses the existing fixture admission path rather than
mocking training, file verification or checkpoint restore.

| Execution | Observed result |
|---|---|
| Two uninterrupted upstream GAN epochs | Completed one full training-phrase update per epoch; generator loss 218.6424255 → 213.5378571, discriminator loss 4.9229078 → 4.4336958 |
| Separate process resumes epoch 1 | Epoch 2 generator/discriminator state, optimizer/scheduler/RNG state and held-out WAV bytes exactly equal the uninterrupted result |
| Persistent checkpoint size | 553,464,732 bytes each; three retained complete GAN checkpoints total 1,660,394,196 bytes |
| Held-out fixture reconstruction | Correctly **fails**: spectral distance 3.970544, whole-phrase median pitch difference 979.128 cents |
| Existing `export_vocoder` consumes resumed checkpoint | Retained 168,336-byte ONNX graph; Torch/ONNX comparisons pass for 1, 3, 16 and 23 frames; maximum observed error 3.3527613e-8 |

The model weights actually changed; this is no longer an untrained forward-only
check. But two oscillator updates do not create a singer or support any inference
about singing naturalness/intelligibility. Loss movement is not quality acceptance.
The exact resume result is measured on this runtime/configuration, not guaranteed
across other hardware, thread counts, libraries or arbitrary forward methods.
No new native learned-singer bundle or installed singer is claimed by this export.

Selected artifact identities:

- `vocoder-command-check.json`: `240fbde892163c9c5f331dcd8396e3c6c535c9599dbd6e58ef56748bf66f25c7`.
- Resumed `epoch-000002/checkpoint.json`:
  `7f15ea687e9aa87ea7684fd429921743dfc1405b63da9657317d5c40e85f14a4`.
- Resumed held-out `reconstruction-000002/item-000001.wav`:
  `e0a56174ba971a1af537f862087c63c3f167523bde1f6170cc5a4097aa113ac3`.
- `vocoder-export/vocoder.onnx`:
  `76c75fafb3b93bad306be67708248d104dc8b03f6d81732aa38f0f8a7c248bca`.
- `vocoder-export/export.json`:
  `aca6b5acd93f4453a82306af9ac5b54be6b3df291387f10116ecaa1831ae948b`.

Fixture review expiry is intentionally short. Retention preserves evidence; it
does not make those reviews permanent production authorization. No downloaded
PJS recording or retained SEAM song was silently substituted into these fixtures.

### 15.3 Imported continuation pitch now has explicit ownership

A new actual-sampler regression reproduced another 400-cent error: a touching
continuation began at 5800 cents instead of the independently specified 6200 cents.
The imported curve already described portamento, but SEAM applied its automatic
continuation glide a second time.

Successful linear USTX composition now persists the existing whole-region
`Pitch/Replace` ownership contract. The compiler suppresses only the duplicate
automatic glide while that ownership applies. No schema or import-only hidden flag
was introduced. Native unowned and additive manual glides remain unchanged;
continuation phonetics, reattack, articulation and dynamics remain independent.
Compiler revision 16 invalidates old cached renders.

Tests cover four sample rates, JSON save/reopen, actual `at()` and `inspectAt()`,
and exact scoped ownership boundaries. Nonlinear/polyphonic fallbacks remain
unowned and explicitly lossy. USTX export reports the ownership-metadata loss;
this is not a new lossless-round-trip claim.

### 15.4 Verification and remaining highest-value work

- Full Release rebuild: PASS, 377 build actions.
- Complete CTest: **172/172 PASS in 104.54 seconds**, including source closure.
- Optional neural-runtime Python discovery: **128/128 PASS in 38.436 seconds**.
- Agent-focused USTX 22 cases and compiler 20 cases pass. Runner/checkpoint-related
  focused tests: 31 pass; counts overlap the complete suites.
- Independent CLI review found a non-object receipt could raise `AttributeError`.
  Added failing array/null/string cases, then repaired the guard; they now reject
  through the controlled error path. Source and staged diff checks pass.
- The second-developer task reviewed scope, restore and write-budget handling
  read-only. Actual executions above are the integrator's evidence, not proof
  derived from that task's description. The parked coordinator race was untouched.

Lane A's persistent-runner subtask and lane B's duplicate-glide repair are complete.
Remaining priority: assemble useful, explicitly authorized original singing material,
retain a genuinely singer-trained acoustic/vocoder candidate, and run it through the
normal native deployment and installed-song workflow. A teacher corpus whose lyrics
are already unclear cannot be assumed to become intelligible through reconstruction.
Independent audio diagnostics, nonlinear interchange cases and named-host evidence
continue in parallel. No source-rights policy, human observation, full-product
denominator, R9 or Beta GO acceptance has been replaced by this fixture success.
