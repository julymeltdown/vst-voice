# Project SEAM Beta Voicebank Independent Expert Review Request

- **Prepared:** 2026-08-30
- **Repository snapshot:** `codex/external-beta-completion` at `970d159d06a2daa11932a9dbc22a337ecf9dbe25`
- **Decision under review:** Whether Project SEAM should create its non-official Beta Voicebank from project-owned procedural synthesis, a consented and transformed recording, or a rights-cleared third-party TTS source
- **Current release state:** Open Beta `HOLD / NO-GO`; the Beta Voicebank dossier is `BLOCKED`

## 0. Engagement fields to complete before sending

```text
Requester/name:
Contact method:
Reviewer's name:
Reviewer role: Developer / Music expert
Requested review mode: Pre-prototype / Prototype listening / Both
Repository access method:
Repository commit:
Candidate packet download location:
Candidate packet SHA-256:
Requested completion date:
Expected review time budget:
Compensation or volunteer basis:
Confidentiality restrictions:
Permission to quote the response publicly: Yes / No / Redacted only
Follow-up meeting window:
```

Do not send the request with a blank commit or candidate hash. If the prototype does not yet exist, write `NO AUDIO CANDIDATE - PRE-PROTOTYPE REVIEW ONLY` instead of asking for a musical-quality verdict.

## 1. Purpose of this request

Project SEAM needs one legally redistributable, technically compatible, and musically useful non-official voicebank before external beta testing can represent the real product.

We are asking two independent reviewers to answer different questions:

1. **Software developer / audio-DSP reviewer:** Can the proposed source and production pipeline produce a deterministic, secure, maintainable `.seambank` that works correctly in Project SEAM?
2. **Music / singing-synthesis expert:** Does the resulting bank communicate Japanese lyrics and musical intent well enough that beta users can evaluate Project SEAM rather than merely react to a poor placeholder voice?

This is not a request for general impressions. Each reviewer must return an evidence-based verdict using the response format in this document.

## 2. Exact decision to make

Evaluate these three approaches separately:

### Option A: Project-owned procedural synthetic voice

Generate all source units using Project SEAM-owned DSP code: glottal excitation, formant filtering, aspiration/noise, consonant envelopes, closures, releases, and deterministic transitions. The intended identity is deliberately fictional and robotic rather than similar to a real performer.

### Option B: Consented recording transformed into a fictional beta voice

Record the maintainer or another explicitly consenting performer against the exact Japanese inventory. Preserve the original recordings as immutable private sources, then create derived units through documented pitch, formant, spectral, timing, and timbre processing.

### Option C: Rights-cleared third-party offline TTS output

Generate source phrases or units from a particular offline TTS engine and specific voice model, then segment, edit, retune, process, mark, and package the derived units. This option is eligible only if the exact engine, weights, voice model, data provenance, output rights, transformation rights, voicebank redistribution rights, and end-user rendered-audio rights are documented.

### Decision labels

Each reviewer must choose exactly one label for each option:

- **GO:** Suitable for full Beta Voicebank production now; no unresolved blocker in the reviewer's domain.
- **TRIAL:** Suitable only for a bounded prototype; named evidence is still required before expansion.
- **HOLD:** Plausible, but a material prerequisite or unresolved risk prevents a prototype decision.
- **REJECT:** Fundamentally unsuitable for the Beta Voicebank goal.
- **NOT ASSESSED:** Outside the reviewer's competence or unavailable evidence; explain what specialist or artifact is required.

## 3. Current repository facts

These are facts to verify, not assumptions to debate:

- The public issue register is [BETA_READINESS_ISSUES.md](BETA_READINESS_ISSUES.md).
- The detailed readiness analysis is [docs/reviews/PROJECT_SEAM_OPEN_BETA_READINESS_2026-08-30.md](docs/reviews/PROJECT_SEAM_OPEN_BETA_READINESS_2026-08-30.md).
- The governing bank gate is [docs/voicebank/BETA_VOICEBANK_ACCEPTANCE.md](docs/voicebank/BETA_VOICEBANK_ACCEPTANCE.md).
- The checked-in dossier [docs/voicebank/beta-voicebank-01-dossier.json](docs/voicebank/beta-voicebank-01-dossier.json) is intentionally `BLOCKED` and contains no release package or rights approval.
- The planned bank is non-official, characterless, Japanese, and currently declares pitch layers MIDI 60 and 72.
- The checked-in inventory snapshot [docs/voicebank/BETA_JAPANESE_CVVC_INVENTORY.json](docs/voicebank/BETA_JAPANESE_CVVC_INVENTORY.json) contains 72 coverage keys and 144 takes but only the `k`, `s`, and `t` consonant families. It is not a complete Japanese bank.
- The inventory generator's default profile in [tools/voicebank-script-generator/main.py](tools/voicebank-script-generator/main.py) lists a much broader Japanese consonant set and supports vowels, CV, VC, VV, sustain, release, breath, glottal attack, and special units.
- Recording/session validation requires dry mono, 48 kHz, 24-bit sources and checks clipping, DC offset, silence, root pitch, marker order, and pitch marks in [tools/external_beta/voicebank_production.py](tools/external_beta/voicebank_production.py).
- Candidate bindings must cover every declared `(coverageKey, pitchLayer)` pair and provide ordered markers and pitch marks.
- The four required listening paths are **Raw**, **Classic PSOLA**, **SpectralClassic**, and **Stretch**, as recorded in [docs/voicebank/templates/RENDERER_LISTENING_QA_TEMPLATE.md](docs/voicebank/templates/RENDERER_LISTENING_QA_TEMPLATE.md).
- `.seambank` is a signed, deterministic, data-only container. See [docs/formats/SEAMBANK_V1.md](docs/formats/SEAMBANK_V1.md).
- A third-party voicebank may remain characterless. See [docs/formats/VOICEBANK_MANIFEST_V3.md](docs/formats/VOICEBANK_MANIFEST_V3.md).
- Required rights are independently scoped: source use, transformation, redistribution in a local singing voicebank, and end-user rendered audio.

If any item above is incorrect at the reviewed commit, report the discrepancy before continuing.

## 4. Evidence packet the project owner must provide

### 4.1 Pre-prototype review packet

Provide the developer and music expert with:

- This review request
- The root issue register
- The governing acceptance document and blocked dossier
- The current inventory JSON and generator source
- The production validator source
- The `.seambank` and current manifest specifications
- A one-page description of Options A, B, and C
- For Option C, the exact engine version, model/voice identifier, download URL, hashes, model card, license files, and terms snapshot

This packet permits an architecture and production-design review. It does **not** permit a musical-quality PASS because there is no candidate audio.

### 4.2 Prototype listening packet

Do not ask the music expert for a final verdict until the following immutable packet exists:

```text
beta-voicebank-review-<candidate-id>/
  README.md
  identity.json
  sha256sums.txt
  provenance/
    source-method.md
    generator-version.txt
    generator-config.json
    rights-summary.json
  inventory/
    inventory.json
    coverage-report.json
  isolated-units/
  transition-tests/
  musical-tests/
  renderer-comparison/
    raw/
    classic-psola/
    spectral-classic/
    stretch/
  reference-song/
    project.seam-project
    mix.wav
    stems/
  measurements/
    pitch-report.json
    loudness-report.json
    discontinuity-report.json
    clipping-dc-silence-report.json
  score-sheet.md
```

`identity.json` must contain:

- Repository commit
- Generator version and source hash
- Generator configuration hash
- Inventory hash
- Voicebank ID and version
- `.seambank` content hash
- Render configuration and sample rate
- Creation timestamp
- Whether the source is procedural, recorded, or TTS-derived

Every rendered file must be named consistently and listed in `sha256sums.txt`. If any audio or marker changes, create a new candidate identity rather than silently replacing the packet.

### 4.3 Material that must not be shared casually

- Private performer contracts
- Government identifiers or contact information
- Raw recordings unless the performer explicitly approved this reviewer and transfer method
- Account credentials or paid-service API keys
- Unredacted legal correspondence

Use redacted approval hashes and scope summaries in the public packet. Store private evidence outside the repository.

## 5. Rules for both reviewers

1. Identify your name or reviewer ID, relevant experience, review date, and any conflict of interest.
2. Record the exact commit and candidate hashes reviewed.
3. Separate three evidence states:
   - **INSPECTED:** personally opened, read, listened to, or ran.
   - **INFERRED:** reasoned from inspected evidence but not directly executed.
   - **NOT VERIFIED:** unavailable or outside scope.
4. Do not use “looks fine,” “probably,” or “industry standard” without naming the supporting evidence.
5. The developer must cite file paths and line numbers or command output for code findings.
6. The music expert must cite audio filenames and timestamps for audible findings.
7. Classify each finding:
   - **BLOCKER:** prevents any external beta use.
   - **CRITICAL:** materially corrupts lyrics, pitch, timing, stability, or evaluation validity.
   - **MAJOR:** recurring defect that users will notice but can work around.
   - **MINOR:** polish issue that does not invalidate beta evaluation.
8. State whether the problem belongs primarily to the source audio, inventory, marker data, pitch marks, unit selection, renderer, mixing, or test design.
9. A reviewer may not approve rights outside their legal competence. Flag the required legal question instead.
10. A final `GO` requires direct evidence. Document-only review can produce at most `TRIAL`.

### 5.1 Written rights confirmation that neither reviewer can waive

The developer and music expert cannot close the rights gate unless they are separately qualified and retained to do so. For any recorded or TTS-derived source, send the following questions to the performer, model publisher, service provider, or legal reviewer and preserve the written answer:

```text
Please answer each item YES, NO, or NOT APPLICABLE and identify the agreement,
section, version, date, and signing party that supports the answer.

1. Do you control or have authority to grant the relevant rights in the source
   recording, voice, model, and generated output?
2. May Project SEAM use the source audio/output commercially?
3. May Project SEAM edit, segment, denoise, pitch-shift, formant-shift, retime,
   loop, combine, and otherwise create derivative unit samples?
4. May Project SEAM distribute those processed samples as a reusable local
   singing voicebank or sample-based software instrument, rather than only as
   part of a finished song, video, or narration?
5. May Project SEAM sublicense the voicebank to end users through its beta and
   later commercial product?
6. May end users create, publish, monetize, sell, synchronize, broadcast, and
   otherwise commercially exploit songs rendered with the voicebank?
7. Are those permissions worldwide, perpetual for already distributed copies,
   and valid after a subscription or service relationship ends?
8. Are attribution, naming, notices, royalties, reporting, share-alike,
   source-offer, field-of-use, or distribution-channel restrictions required?
9. May Project SEAM describe the bank as synthetic, transformed, fictional,
   non-official, and characterless?
10. Does any performer or other person retain voice, publicity, privacy,
    personality, moral, neighboring, union, or contractual rights that require
    separate consent for this use?
11. Is the voice designed to imitate or be confusingly similar to an identifiable
    person? If yes, identify the consent covering the proposed use.
12. Do training data, embedded speaker representations, third-party datasets, or
    upstream model terms impose additional restrictions?
13. May Project SEAM retain immutable source/output copies and hashes for audit,
    reproducibility, security response, and already-shipped-version support?
14. Can the permission be revoked for already distributed versions? If yes,
    describe notice, takedown, cure, and end-user obligations.
15. Who bears responsibility if the rights representation is inaccurate, and is
    any warranty, indemnity, liability limit, or required insurance applicable?
```

Any unanswered item that affects the planned use remains `UNRESOLVED`. A generic statement such as “commercial use is allowed” does not answer items 3-7.

## 6. Developer / audio-DSP review assignment

### 6.1 Reviewer profile

The preferred reviewer has practical experience in at least two of the following:

- Audio DSP or speech/singing synthesis
- Sample-based instruments or voicebanks
- Deterministic media pipelines
- C++ real-time audio systems
- Content packaging, signatures, and supply-chain validation
- TTS model deployment and licensing boundaries

State which areas apply. Do not imply expertise in the others.

### 6.2 Developer tasks

#### Task D1: Trace the full data path

Trace one Japanese unit from inventory declaration through source/derived WAV, markers, manifest, `.seambank`, installation, unit selection, renderer dispatch, phrase composition, and final export.

Return:

- A path-level flow diagram or numbered sequence
- Every code/file boundary involved
- Any format conversion or resampling
- Any nondeterministic operation
- Every point where identity or provenance can be lost
- Every point where malformed data can pass validation but fail at render time

#### Task D2: Evaluate Option A, procedural synthesis

Answer:

1. Can Project SEAM's current renderers accept procedurally generated dry unit WAVs without architectural changes?
2. Which source model is technically appropriate: source-filter/formant synthesis, concatenated primitives, differentiable synthesis, or another method?
3. How should voiced excitation, unvoiced noise, aspiration, plosive closure/burst, nasal resonance, taps, affricates, palatalization, and moraic consonants be represented?
4. Can each required CV/VC/VV transition be generated directly, or will crossfaded isolated phones produce unacceptable coarticulation?
5. Which units need unique synthesis and which can be derived safely?
6. Can the generator emit stable pitch periods and deterministic pitch marks at MIDI 60 and 72?
7. How should loop regions be generated so Raw and Stretch do not expose periodic seams?
8. What deterministic random-number strategy is needed for aspiration and fricative noise?
9. Will generated units remain bit-identical across macOS and Windows? If not, what reproducibility boundary should be claimed?
10. Which generated parameters must become part of the content hash?
11. What audible quality ceiling should be expected without a human source?
12. Which existing modules can be reused unchanged, and which require code changes?

#### Task D3: Evaluate Option B, consented recording plus transformation

Answer:

1. Does recording exact inventory units provide better joins than generating TTS sentences and cutting them?
2. Which transformations can create a fictional timbre without destroying consonant intelligibility or pitch-mark stability?
3. Should transformation occur before or after segmentation and marker placement?
4. How should raw immutable recordings and transformed assets be separated and hash-bound?
5. What processing must be nondestructive and reproducible?
6. How should retakes be selected without introducing identity drift across units and pitch layers?
7. What minimum recording environment and microphone consistency are necessary for a beta-quality bank?
8. Does the current 48 kHz, 24-bit, mono contract remain appropriate?

#### Task D4: Evaluate Option C, third-party TTS

For one specifically named engine and voice model, answer:

1. Are engine code, model weights, speaker/voice assets, and generated output governed by separate licenses?
2. Does the reviewed material explicitly permit commercial output?
3. Does it explicitly permit modification and segmentation?
4. Does it explicitly permit redistributing processed output as a reusable sample or singing voicebank?
5. Does it explicitly permit end users to commercially distribute songs rendered from that bank?
6. Are attribution, share-alike, source-offer, or notice obligations compatible with `.seambank`?
7. Is training-data provenance documented, and is performer/voice consent documented separately from copyright licensing?
8. Can the exact model and voice be archived or reacquired deterministically?
9. Can synthesis be run offline with a fixed version and seed?
10. Does the voice support Japanese mora timing, devoicing, gemination, long vowels, moraic nasal, and pitch behavior sufficiently for unit extraction?
11. Do vocoder artifacts become materially worse after segmentation, pitch shifting, sustain looping, or concatenation?
12. Is there any restriction on voice cloning, voice extraction, dataset creation, model training, OEM use, or standalone redistribution relevant to this pipeline?

For each answer, quote no more than the minimum necessary license text and provide the authoritative URL or archived terms identifier. If the permission is not explicit, mark it `UNRESOLVED`; do not infer permission from “commercial use allowed.”

#### Task D5: Audit inventory adequacy

Compare:

- The checked-in 72-key, `k/s/t` snapshot
- The generator's broader default consonant profile
- The actual Japanese phonetic/musical requirements recommended by the music expert

Report:

- Missing phonemes and transitions
- Redundant aliases
- Required allophones
- Whether CV/VC/VV is sufficient or VCV/CC cases are required
- Handling for `N`, `R`, `pau`, `br`, `cl`, glottal attack, long vowels, devoiced vowels, palatalized consonants, and loanword phones
- Minimum and preferred pitch-layer count
- Estimated unit and take counts for prototype and full beta
- Expected generation, marker-review, and listening-review effort

#### Task D6: Audit marker and pitch automation

For each unit kind, specify how to determine:

- Audio offset/start
- Consonant end
- Vowel onset
- Stable-region start
- Loop start and end
- Release start/end
- Audio end
- Root pitch
- Pitch-mark sequence

State which values can be generated analytically, which can be detected automatically, and which require human correction. Define failure conditions for invalid order, insufficient stable region, octave error, noise-only regions, and discontinuities.

#### Task D7: Define objective QA

Propose measurable thresholds for:

- Clipping and true peak
- DC offset
- leading/trailing silence
- root-pitch median and percentile error
- pitch-mark monotonicity and period deviation
- loop-boundary discontinuity
- spectral discontinuity at unit joins
- loudness consistency across units and layers
- noise-floor consistency
- deterministic repeatability
- missing inventory pairs
- render success across Raw, Classic PSOLA, SpectralClassic, and Stretch

For every threshold, explain why it is appropriate and whether it is a hard gate or diagnostic warning.

#### Task D8: Produce a minimal implementation plan

Provide a code-level plan for the preferred option:

- Exact new or modified files
- Public types or schemas affected
- CLI inputs and outputs
- Determinism strategy
- Error handling and fail-closed conditions
- Test additions
- Artifact and evidence generation
- Estimated engineering days for prototype and full beta
- Critical dependencies
- Risks that require a different specialist

Do not propose a new framework if a repository utility already provides the boundary.

### 6.3 Required developer questions

Answer every question directly:

1. Which option do you recommend and why?
2. Which option do you reject and why?
3. Is the recommended option technically compatible with the current renderer architecture?
4. What is the smallest spike that can falsify the approach?
5. What result would make you stop the spike?
6. What source-code changes are blocking versus merely desirable?
7. What quality limitation will remain even after correct implementation?
8. What part of the current bank contract is unnecessarily strict, if any? Do not recommend weakening it solely to obtain PASS.
9. What part of the current contract is too weak or underspecified?
10. Can the candidate be reproduced from source without network access?
11. Can the same exact bank be generated and verified on another machine?
12. Can the package be characterless and still provide a coherent tester experience?
13. Which claims require legal counsel rather than engineering judgment?
14. What is your confidence from 0-100%, and what missing evidence dominates the uncertainty?

## 7. Music / singing-synthesis expert review assignment

### 7.1 Reviewer profile

The preferred reviewer has practical experience in Japanese vocal production, phonetics, singing synthesis, UTAU-style voicebank production, Vocaloid/Synthesizer V editing, vocal comping, or music-production QA.

State:

- Japanese-language competence
- Voicebank or singing-synthesis experience
- DAW and monitoring equipment used
- Hearing or monitoring limitations relevant to the review
- Familiarity with Project SEAM, if any

### 7.2 Phase M0: Pre-audio design review

Before listening, review the proposed inventory and answer:

1. Is a CV/VC/VV inventory appropriate for a minimal Japanese singing beta bank?
2. Which consonants, palatalized consonants, affricates, loanword phones, and special units are mandatory?
3. How should moraic nasal `N`, geminate closure `cl`, long-vowel timing, tapped/flapped `r`, devoiced vowels, breath, and glottal attacks be handled?
4. Are MIDI 60 and 72 sufficient source layers for the declared 60-72 range?
5. Should the bank use an additional middle layer to reduce timbre discontinuity?
6. How many alternate takes are needed for attacks, sustains, releases, and consonant-heavy units?
7. Which units require recorded/generated coarticulation rather than synthetic crossfading?
8. What is the minimum musically useful dynamic and articulation set for a beta instrument?
9. Is a deliberately robotic but stable voice acceptable for evaluating editor, renderer, timing, automation, and export workflows?
10. At what point would synthetic timbre be so poor that it invalidates product feedback?

Return a corrected minimum inventory proposal, not only prose comments.

### 7.3 Phase M1: Listening environment

Record:

- Headphones and/or monitors
- Audio interface
- Listening level or calibration method
- Room limitations
- DAW/player and version
- Whether files were level-matched
- Whether the review was blind to source option and renderer

Use lossless WAV files. Do not evaluate final quality from messaging-app transcoding or browser-streamed previews.

### 7.4 Required listening set

The project owner should supply the following tests at minimum:

| ID | Material | Purpose |
| --- | --- | --- |
| M01 | Five isolated vowels at each source pitch, 1 s and 6 s versions | Timbre, pitch, sustain, loop stability |
| M02 | Every CV unit in a steady rhythmic grid | Consonant identity and attack consistency |
| M03 | Every VC unit followed by silence | Coda transition and release behavior |
| M04 | Every VV transition in both directions | Legato vowel transition quality |
| M05 | `N`, `cl`, `R`, breath, pause, and glottal units in context | Japanese special-unit correctness |
| M06 | Palatalized and affricate minimal pairs | `k/ky`, `s/sh`, `t/ch/ts`, and related distinctions |
| M07 | One slow legato phrase around 70-80 BPM | Vowel continuity and expressive baseline |
| M08 | One staccato phrase around 120 BPM | Attack/release consistency |
| M09 | One fast lyric phrase around 150-170 BPM | Timing limits and consonant intelligibility |
| M10 | Repeated-note phrase with identical lyrics | Robotic repetition and alternate-take need |
| M11 | Octave and fifth leaps across the supported range | Layer switching and formant/timbre discontinuity |
| M12 | Eight-second sustain with pitch bend and vibrato | Looping, modulation, and renderer stability |
| M13 | Phrase crossing every pitch-layer boundary | Audible layer transition |
| M14 | Identical phrase from all four renderers | Renderer-specific artifacts |
| M15 | Canonical reference song mix and dry vocal stem | Whole-song usability and masking |
| M16 | Twenty-minute editing/listening session | Fatigue and recurring artifact assessment |

The exact same project, tempo, note timing, lyrics, automation, gain staging, and bank candidate must be used for renderer comparisons.

### 7.5 Blind transcription test

For M07-M10 and M15:

1. Do not show the Japanese lyrics on the first listen.
2. Ask the reviewer to transcribe the perceived morae or words.
3. Compare the transcription with the target.
4. Record substitutions, deletions, insertions, and ambiguous consonants.
5. Repeat with lyrics visible and record whether the defect remains acoustic or was merely contextual uncertainty.

Report intelligibility by test and identify the exact phone/transition responsible for each repeated error.

### 7.6 Scoring rubric

Score each dimension from 1 to 5:

- **1 — Unusable:** defect dominates the result or prevents correct perception.
- **2 — Poor:** recurrent severe defect; usable only for debugging.
- **3 — Beta-usable with limitations:** musical intent is recoverable, but defects are obvious and must be documented.
- **4 — Good:** occasional defect that does not prevent normal beta evaluation.
- **5 — Strong:** stable and convincing within the intended synthetic aesthetic.

Required dimensions:

| Dimension | What to judge |
| --- | --- |
| Japanese intelligibility | Correct mora and word perception without seeing lyrics |
| Consonant identity | Stops, fricatives, affricates, nasals, taps, and palatalized contrasts |
| Vowel identity | Stable `a/i/u/e/o` identity across pitch and duration |
| Attack quality | No smeared, late, doubled, or excessively hard onsets |
| Timing | Vowel onset aligns musically; consonants fit slow and fast contexts |
| Pitch accuracy | Stable center, correct bends, no octave or layer-tracking errors |
| Sustain quality | No obvious loop pulse, flutter, buzz change, or frozen spectrum |
| Release quality | Natural ending without click, truncation, or excess tail |
| Join quality | No clicks, gaps, phasing, duplicated phones, or spectral jumps |
| Layer consistency | No distracting timbre/loudness identity jump between MIDI layers |
| Dynamic response | Gain and expression changes remain usable rather than collapsing timbre |
| Renderer consistency | Differences are explainable and no renderer is catastrophically broken |
| Musical usefulness | Can a musician shape a phrase rather than fight every note? |
| Listening fatigue | Artifacts do not become intolerable during a realistic session |
| Beta representativeness | Bank quality is sufficient to judge Project SEAM's product behavior |

For every score below 4, provide at least one filename and timestamp.

### 7.7 Provisional hard-fail conditions

Mark the candidate `HOLD` or `REJECT` if any of these occur in ordinary intended use:

- Repeated Japanese phonemes cannot be identified even with lyrics visible.
- Common transitions regularly create clicks, gaps, doubled consonants, or missing vowels.
- Sustained notes reveal an obvious recurring loop pulse or unstable pitch.
- Pitch-layer changes sound like unrelated characters rather than one deliberate instrument.
- One or more production renderers fail to produce usable output.
- Fast phrases cannot remain intelligible at the declared beta tempo range.
- The bank causes listeners to attribute renderer defects to the source bank, making product feedback unreliable.
- The timbre closely resembles an identifiable real person without documented consent.

The reviewer may add hard-fail conditions, but must explain them.

### 7.8 Required music-expert questions

1. Is the bank musically usable as a beta evaluation instrument, even if intentionally synthetic?
2. Can a listener understand the Japanese lyrics without seeing them?
3. Which five phones or transitions cause the most errors?
4. Are problems caused primarily by source generation, marker placement, pitch marks, unit selection, or rendering?
5. Are two pitch layers sufficient? If not, where should the additional layer be placed?
6. Which renderer is most reliable, and which is least reliable?
7. Which test phrase exposes the candidate's worst realistic behavior?
8. Does a procedural source sound cleaner or less natural than the transformed-recording/TTS alternative?
9. Is the robotic quality coherent and intentional, or merely defective?
10. What minimum changes are required before inviting external musicians?
11. What improvements can wait until after private alpha?
12. Would you personally be able to evaluate Project SEAM's editor and renderer using this bank for a 30-minute session?
13. What is your confidence from 0-100%, and what missing listening evidence dominates the uncertainty?

## 8. Questions requiring both reviewers

The developer and music expert should answer these independently before discussing their conclusions:

1. Does this candidate isolate Project SEAM product quality from voicebank-source quality well enough for beta feedback?
2. Is the proposed inventory the smallest honest beta inventory, or merely the smallest one that passes a schema?
3. Is the supported MIDI range narrow but coherent, or misleadingly incomplete?
4. Can the source be replaced later without invalidating projects, presets, and tester expectations?
5. What user-facing limitations must be disclosed in the installer, bank metadata, and beta guide?
6. Should the bank be bundled, downloaded separately, or installed by a candidate-bound setup flow?
7. Is a characterless bank acceptable, or does the tester experience require a minimal non-person character/identity presentation?
8. What single experiment has the highest probability of disproving the recommended approach quickly?

After answering independently, reconcile disagreements in a short joint note. Do not average incompatible judgments; state the disagreement and the evidence needed to resolve it.

## 9. Decision rules

The project owner should use the following decision sequence:

| Gate | Owner | PASS requirement | Failure result |
| --- | --- | --- | --- |
| Rights and provenance | Legal/rightsholder review | All four permission scopes explicit; identity and source provenance accepted | Reject source; do not process further |
| Technical feasibility | Developer | Deterministic pipeline, valid units/markers, all renderers operate, package installs | Stop or revise implementation |
| Musical adequacy | Music expert | No hard fail; beta representativeness at least 3/5; named remediation bounded | Revise source/inventory/markers |
| Cross-domain acceptance | Both | No unresolved Blocker/Critical and same candidate hash reviewed | Keep dossier `BLOCKED` |
| Release evidence | Release owner | Signed package, clean install, reference song, receipts, completed dossier | No external distribution |

Recommended option selection:

- Choose **Option A** if it passes musical adequacy. It has the cleanest provenance and reproducibility story.
- Choose **Option B** if Option A cannot reach intelligibility or musical representativeness, and explicit performer rights are available.
- Choose **Option C** only if a specific voice's redistribution and downstream-render rights are explicit and its audio materially outperforms A/B after the same processing and tests.
- Do not combine several uncertain sources to dilute provenance. Every source must independently pass the rights gate, and every derived unit must retain source lineage.

## 10. Developer response template

```markdown
# Project SEAM Beta Voicebank Developer Review

Reviewer:
Review date:
Repository commit:
Candidate ID/hash:
Relevant expertise:
Conflict of interest:

## Evidence inspected
- [INSPECTED] ...
- [INFERRED] ...
- [NOT VERIFIED] ...

## Option verdicts
| Option | GO/TRIAL/HOLD/REJECT/NOT ASSESSED | Confidence | Primary reason |
| --- | --- | ---: | --- |
| A: Procedural | | | |
| B: Consented transformed recording | | | |
| C: Named TTS engine/voice | | | |

## Data-path trace
1. ...

## Findings
| ID | Severity | Evidence | Consequence | Required change | Estimated effort |
| --- | --- | --- | --- | --- | --- |
| D-001 | | | | | |

## Inventory recommendation
- Prototype inventory:
- Full Beta inventory:
- Pitch layers:
- Alternate takes:
- Missing phones/allophones:

## Objective QA thresholds
| Metric | Hard gate/warning | Threshold | Rationale |
| --- | --- | --- | --- |

## Smallest falsifying spike
- Scope:
- Expected artifacts:
- Stop conditions:
- Estimated engineering days:

## Blocking code changes
1. File/symbol:
   Change:
   Verification:

## Direct answers D1-D14
1. ...

## Final recommendation
Preferred option:
Verdict:
Confidence 0-100%:
Top three blockers:
Evidence required to reverse the verdict:
```

## 11. Music-expert response template

```markdown
# Project SEAM Beta Voicebank Music Review

Reviewer:
Review date:
Candidate ID/hash:
Japanese competence:
Singing-synthesis experience:
Monitoring chain:
Listening method:
Conflict of interest:

## Evidence inspected
- [INSPECTED] ...
- [INFERRED] ...
- [NOT VERIFIED] ...

## Inventory design verdict
GO/TRIAL/HOLD/REJECT:
Required missing units:
Recommended pitch layers:
Recommended alternate takes:

## Blind transcription results
| Test ID | Target | Perceived | Error phones/transitions | Notes |
| --- | --- | --- | --- | --- |

## Listening scores
| Test ID | Renderer | Dimension | Score 1-5 | File and timestamp | Cause classification |
| --- | --- | --- | ---: | --- | --- |

## Findings
| ID | Severity | File/timestamp | Audible problem | Likely layer | Required change |
| --- | --- | --- | --- | --- | --- |
| M-001 | | | | | |

## Five worst phones or transitions
1. ...

## Renderer comparison
- Most reliable:
- Least reliable:
- Material differences:

## Direct answers M1-M13
1. ...

## Final recommendation
Beta evaluation instrument: YES / NO
GO/TRIAL/HOLD/REJECT:
Confidence 0-100%:
Minimum changes before external musicians:
Improvements that may wait:
Evidence required to reverse the verdict:
```

## 12. Copy-paste message for the developer

> I am evaluating how to produce Project SEAM's non-official Japanese Beta Voicebank. Please review the attached `BETA_VOICEBANK_EXPERT_REVIEW_REQUEST.md` and the repository at commit `970d159d06a2daa11932a9dbc22a337ecf9dbe25`.
>
> I need a code- and evidence-based comparison of three options: a project-owned procedural synthetic bank, a consented and transformed recording, and one specifically named rights-cleared offline TTS voice. Please trace the actual inventory-to-render path, inspect the existing validators and package format, identify blocking code changes, propose objective QA thresholds, and define the smallest spike capable of disproving your preferred approach.
>
> Do not treat “commercial use allowed” as proof that processed samples may be redistributed as a reusable singing voicebank. Mark permissions that are not explicit as unresolved. Cite repository paths/lines, commands, candidate hashes, and authoritative license sources. Use the developer response template and assign GO, TRIAL, HOLD, REJECT, or NOT ASSESSED to each option.
>
> A document-only review can produce at most TRIAL. Please state exactly what you personally inspected, inferred, and could not verify.

## 13. Copy-paste message for the music expert

> I am evaluating a deliberately non-official Japanese Beta Voicebank for Project SEAM, a singing-voice editor and renderer. The goal is not final commercial-character quality. The bank must nevertheless be intelligible, stable, and musically representative enough that external testers can judge the product rather than be distracted by a defective placeholder voice.
>
> Please follow `BETA_VOICEBANK_EXPERT_REVIEW_REQUEST.md`. First review the proposed Japanese inventory and pitch-layer design. Do not give a final quality PASS until I provide the hash-bound lossless listening packet described in the document. When audio is available, perform the blind transcription, isolated-unit, transition, pitch-layer, four-renderer, reference-song, and fatigue tests. Cite filenames and timestamps for every score below 4/5.
>
> I need you to distinguish problems in the source voice, inventory, markers, pitch marks, unit selection, renderer, and mix. Please use the music-expert response template and return a single GO, TRIAL, HOLD, REJECT, or NOT ASSESSED verdict, confidence from 0-100%, the five worst phones/transitions, and the minimum changes required before external musicians are invited.

## 14. Questions the project owner must not ask vaguely

Avoid:

- “Does this voice sound good?”
- “Can I use free TTS commercially?”
- “Is this enough for beta?”
- “Does the code look production-ready?”
- “Which voice do you like?”

Ask instead:

- “In `M09-fast-lyrics-raw.wav`, which target morae are misheard, at what timestamps, and is the likely cause the source unit, marker timing, selection, or renderer?”
- “Does the exact license for `<engine version>/<voice model hash>` explicitly permit redistributing edited output clips as a reusable singing voicebank and permit commercial end-user song renders?”
- “Which `(coverageKey, pitchLayer)` pairs are missing from the candidate, and which missing pair causes a reproducible lyric failure?”
- “Can another machine reproduce the candidate's WAV and `.seambank` hashes from the same generator commit and configuration? If not, where does nondeterminism enter?”
- “Which P0 acceptance row remains unproven after this review, and what exact artifact would close it?”

## 15. Final deliverable expected from the reviewers

The review is complete only when the project owner receives:

- One completed developer response
- One completed music-expert response
- Independent option verdicts and confidence scores
- An explicit list of inspected and missing evidence
- Code references for technical findings
- Audio timestamps for musical findings
- A corrected prototype/full inventory recommendation
- Objective QA thresholds
- A smallest falsifying spike
- A short joint disagreement note, if conclusions differ
- A final recommendation that remains bound to one exact repository commit and voicebank candidate hash

Until both reviews address the same immutable candidate, the Beta Voicebank dossier must remain `BLOCKED`.
