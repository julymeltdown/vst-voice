# SEAM Completion Review — how much is actually left

> **Correction (same day).** Section 3.3 originally claimed the vocoder was far too small and needed a
> redesign. That was wrong: it compared the 168 KB **32-channel smoke fixture** export against the wrong
> run. The architecture is already full-capacity NSF-HiFiGAN (13,936,386 parameters) and the live r3 run
> uses it. Section 3.3, the blocker table and the plan in section 6 are corrected in place. The error is
> kept explained rather than deleted, because it is the failure mode that has been costing this project
> time — measuring one artifact and drawing a conclusion about a different one.

> **Second update (same day, later).** The r3 run finished its epoch while this review was being written,
> and the result changes section 3.4 and the neural blocker. Held-out mean absolute pitch error is
> **46.676 cents** against the 1,506 cents previously quoted, the export's `pitchFollowsRequestedNote` is
> now true on all four reference notes, and the 187.5 Hz comb is gone (spectral peaks now sit at
> per-song fundamentals with under 0.002 of energy above 16 kHz). See section 3.5 and
> `docs/implementation/INTEGRATED_SINGER_EXECUTION.md` for the measurements. The review's structural
> conclusions stand: there is still no accepted evidence for any of the 20 requirements.

- **Date:** 2026-09-20
- **Branch:** `codex/production-readiness-completion` (HEAD `2d4125f6`, 24 commits ahead of `origin/master`, not merged)
- **Decision:** Beta GO **NO-GO**. Nothing in this document is acceptance evidence.
- **Method:** read the machine-enforced contract, the issue register, the plan, and the build wiring; ran spectrum, pitch and energy measurements on the retained reconstruction WAVs; polled the live training run.

## 1. The single number that matters

Beta GO is gated by `docs/product/full-product-beta-contract.json` (20 requirements, 83 cases, 18 work packages).

| Contract field | Value | Meaning |
|---|---|---|
| `evidenceStatus` | `NOT_RUN` | no case has run |
| `matrixStatus` | `UNRESOLVED` | resource/capability matrix never fixed |
| `releasedResources` | `[]` | zero admitted release resources |
| `evaluationProfile.status` | `UNRESOLVED` | 11 of 29 criteria have no threshold at all |
| `semanticValidation` | `UNAVAILABLE` / admission `BLOCKED` | semantic admission not open |

The 83 cases are **definitions**, not results. **0 of 20 requirements have accepted evidence. Formal Beta GO progress is 0%.**

That is not the same as "nothing works". It means the product has no *evidence* that any of its 20 promises hold.

## 2. Three honest percentages

These measure different things and should never be averaged into one feel-good number.

| Frame | Value | Basis |
|---|---|---|
| **Formal Beta GO** | **0%** | 0/20 requirements with accepted evidence; contract `NOT_RUN` |
| **Engineering vs specification** | **~50%** | project's own weighted milestone table (M1 17.0, M2 3.0, M3 13.0, M4 7.0, M5 9.8, M6 0.5) |
| **Combined realistic completion** | **~30%** | engineering half ~50%, human-gate half ~0% |

Per-milestone engineering, and where it is weak:

| Milestone | Engineering | Gates | Read |
|---|---|---|---|
| M1 usable original-singer session | 85% | 15% | strongest area |
| M2 musically usable first voice | 20% | 80% | **the wall** |
| M3 qualified neural singer | 65% | 35% | wiring done, quality not |
| M4 production, languages, style | 35% | 65% | dictionaries/styles absent |
| M5 supported standalone + DAW product | 65% | 35% | code done, host proof missing |
| M6 full-scope Beta GO evidence | 5% | 95% | essentially untouched |

## 3. What I verified today (new information)

### 3.1 Training is real and on the critical path

Run `/Users/lhs/seam-corpus-xl-2026-09-19/vocoder-512-segments-r3`:

- `completedUpdates` 1000 / 2804, epoch 1, `validSamples` ~30.4M
- `meanGeneratorLoss` 45.83 (resume) → **39.59**; discriminator loss 2.20 → 2.09
- rate **~2.58 s/update** → about **77 minutes** to finish the epoch
- recovery checkpoints publish every 50 updates with distinct receipts (~720 MB each, newest only)

This is genuine, monotonic progress. It is also **one run of a stage that is currently far too small to be the answer** — see 3.3.

### 3.2 The plan document is stale, in the good direction

`SEAM_DETAILED_DEVELOPMENT_PLAN_2026-09-19.md` tells a developer to *create* Phase 2 and Phase 3 files. Most already exist and are wired into the build:

| Plan says "create" | Actual | Lines |
|---|---|---|
| `phoneme_timing_plan` (V02) | `libs/seam-synthesis/src/phoneme_timing_plan.cpp` | 259 |
| `performance_compiler` (V03) | `libs/seam-synthesis/src/performance_compiler.cpp` | 537 |
| `renderer_capabilities` (V05) | `libs/seam-synthesis/src/renderer_capabilities.cpp` | 193 |
| voice-design package (V15) | `libs/seam-voice-design/` — 12 sources, 3,694 lines | `CMakeLists.txt:456-470` |
| designer workflow (V16) | `libs/seam-native-ui/src/voice_designer_*.cpp` + `seam_voice_designer_tests` | `CMakeLists.txt:1375-1383` |
| note vibrato / dynamics (V04) | `note_vibrato.hpp`, `dynamics_automation.hpp`, 13 expression automations | `libs/seam-domain/include/seam/domain/` |

Registered tests already include `test_voice_designer_workflow.cpp`, `test_procedural_install_journey.cpp`, `test_procedural_review.cpp`, `test_performance_compiler.cpp`, `test_phoneme_timing.cpp`, `test_renderer_capabilities.cpp`.

**Consequence:** the engineering half is further along than the plan's phase list implies. The next plan revision should be written from the source tree, not from the previous plan. A development plan whose first instruction is to create a 537-line file that already exists will waste days.

### 3.3 Capacity is NOT the blocker — the previously evaluated model was a smoke fixture

An earlier draft of this review claimed the vocoder is 300-1000x too small. **That was wrong, and the
error is instructive: it compared an artifact against the wrong run.**

The architecture is already **NSF-HiFiGAN** — the same family OpenUtau and DiffSinger ship, vendored at
`build/neural-runtime/singing-vocoders-source/models/nsf_HiFigan/`. Only its *profile* was chosen small.

| Profile | `upsample_initial_channel` | resblocks | Generator parameters |
|---|---|---|---|
| `mini-nsf-32-smoke-v1` | 32 | [3] | **34,986** (~0.14 MB) |
| `mini-nsf-512-mrf-v1` | 512 | [3, 7, 11] | **13,936,386** (~55.75 MB) |

(Counted by instantiating both profiles with the vendored `Generator`.)

The 168,336-byte export (`vocoder-export-e6/vocoder.onnx`) can only be the **32-channel smoke fixture**.
It has no `architectureProfile` field (export schema 1), and 168 KB is consistent with 34,986 parameters
and inconsistent with 13.9M.

To be fair to the project: `tools/voice_model_training/README.md:987-995` already documents both profiles,
states that schema 1 "preserves the original 32-channel, single-residual-kernel smoke model" and "must not
be confused with the capacity of the pinned upstream singing-vocoder configuration", and says plainly
that "No claim is made that capacity alone fixes the measured pitch failure." So the two-profile split
is known and documented. What is **not** recorded is which profile produced the numbers the register
quotes, which is the part that misleads a reader of `BETA_READINESS_ISSUES.md`.

**The live r3 run is not that model.** `vocoder-training-512-segments.json` declares
`architectureProfile: mini-nsf-512-mrf-v1`, so r3 is training the full 512-channel / 13.9M-parameter
architecture on the real corpus.

**Corrected consequence:** capacity is not the blocker and no architecture redesign is required. What is
missing is **a trained epoch of the correct model**. Every held-out pitch and spectral number currently
quoted against "the vocoder" — including P0-08 and the 1506-cent native-pitch sweep — was measured
through a 34,986-parameter smoke fixture. Those numbers say nothing about the architecture SEAM is
actually training.

That also makes the next step concrete rather than open-ended: finish r3, export the 512 channel
checkpoint, and re-run the qualification sweep. One epoch at 2,804 updates against a 60,000-update
budget will still be undertrained, but it separates "not enough training" from the remaining candidate
causes for the first time.

It also means the honest ceiling is already anticipated: even a well-trained vocoder reconstructing
`labelOrigin: com.project-seam.training-generated-teacher` mels can only reproduce what a procedural
teacher itself produced. Capacity and update count are necessary, not sufficient. **A real, rights-cleared
singing corpus is still the binding constraint on R9/R16**, and it is the item no amount of engineering
in this repository can substitute for.

### 3.3b Training throughput is leaving the machine idle

The host is an M3 Max with a 40-core GPU and 48 GB of memory. The live run is pinned to **one CPU core**:

- `vocoder-training-512-segments.json` sets `cpuThreads: 1`
- `train_vocoder.py:230` applies it with `torch.set_num_threads(settings['cpuThreads'])`
- Torch reports `mps.is_available() == True`; nothing in the pipeline ever selects a device

For scale, the run is doing 2,804 updates in about 2 hours and the configuration permits 60,000. Not
using the available GPU is the difference between a training campaign that can iterate and one that
cannot. This is a throughput defect, not a quality defect, and it is worth fixing before the next run —
but it must not be changed underneath the run that is currently in flight.

### 3.4 P0-08's headline measurement no longer reproduces, but its diagnosis now looks right

`BETA_READINESS_ISSUES.md` P0-08 opens with "the vocoder output has energy share **0.48 above 16 kHz**
and a spectral peak at **21000 Hz**", attributed to the three-update checkpoint. It labels that
"Initial evidence", and its closure condition 4 already requires "a repeated full-song render whose
spectrum is consistent with the source rather than with broadband noise **or the hop-rate artifact**".
So the hop-rate mechanism is not a discovery here — it is an already-named acceptance criterion.

I measured the retained outputs directly to see whether the initial evidence still holds:

| Artifact | Spectral peak | Energy > 16 kHz |
|---|---|---|
| `recon-tracked4/item-000001.wav` | **187.5 Hz** | **0.0085** |
| `recon-e6-native-pitch/item-00000{1..4}.wav` | **562.6 Hz** | 0.011–0.012 |

The ultrasonic-noise signature does **not** reproduce on either the older or the newest run. The retained
artifacts are quiet and low, not bright and noisy. Treat the 21 kHz / 0.48 figures as historical
measurements of a three-update checkpoint rather than as a current property of the pipeline.

The confirmed failure is different, and the numbers sharpen P0-08's closure condition 4 rather than
contradicting it:

- Pitch: 908 measurable voiced frames, **0 within 50 cents**, median error **1276.6 cents**, mean **1525.4 cents**, max 3377 cents.
- Level: `renderedRms` **0.001872** vs `sourceRms` **0.025662** — the output is about **13.8 dB too quiet**.
- Spectrum: a harmonic comb at **exactly 187.5 Hz** = 48000/256, i.e. the **analysis hop rate**, with harmonics at 187.5 / 375 / 562.4 Hz and near-zero energy at the intended 220/440/880 Hz.

187.5 Hz is the mel-spectrogram frame rate. Its presence as the dominant audio fundamental is the hop-rate
artifact P0-08 already names, now confirmed quantitatively: the artifact is real, and it is the dominant
one.

**Action:** keep P0-08 open and keep closure condition 4, but replace the "Initial evidence" block with
these current numbers and mark the 21 kHz figure as unreproduced. As written, the entry opens on a symptom
that is no longer present, which risks sending the next engineer looking for broadband noise when the
actual defect is a frame-rate comb plus a level error.

+### 3.5 The r3 epoch completed, and it changes this review's central conclusions

While this document was being written, `vocoder-512-segments-r3` finished its epoch (2,804/2,804 updates)
and exported. Measured now, not inferred:

| Measure | Before (32-channel smoke) | **This epoch (512-channel)** |
|---|---|---|
| Held-out mean absolute pitch error | 1,506.56 cents | **46.676 cents** |
| Frames within 50 cents | 0 of 908 (0%) | **11,376 of 12,186 (93.35%)** |
| Per-item median absolute error | -- | **0.46-0.69 cents** |
| Export pitch conditioning, 440 Hz | 461.405 Hz (82.24 cents off) | **440.787 Hz (3.09 cents off)** |
| Export `pitchFollowsRequestedNote` | false at 440 Hz | **true on all four notes** |
| Export size / parameters | 168,336 bytes | **55,762,986 bytes / 13,936,386** |
| Spectral peak | 187.5 Hz (the hop rate) | **293.8-494.0 Hz (real fundamentals)** |
| Energy above 16 kHz | 0.0085-0.012 | **0.0002-0.0018** |

So 3.4's "hop-rate comb" defect **is resolved for held-out reconstruction**, and the pitch failure that has
anchored P0-08 since 2026-09-19 is largely gone once the correct architecture is trained. The previously
measured numbers were not wrong, but they were about a 34,986-parameter fixture at three updates.

One finding does not resolve, and it is a specification problem rather than a model problem: the
`allReconstructionsSatisfied` conjunct cannot be satisfied by **any** audio, including bit-identical audio.
`pitch_comparison.py:155` requires `MATCH_ON_MEASURABLE_FRAMES`, and 45 unmeasurable frames in a real
held-out render keep the status at `UNRESOLVED` even when all 958 measurable frames agree exactly. The
contract's own `pitch-within-50` criterion asks for "minimum 90 percent"; this epoch measures 93.35%. That
gap between a zero-tolerance conjunct and a 90% release criterion is the owner decision P0-08 closure item 2
describes.

**Revised neural assessment.** The blocker is no longer pitch tracking or vocoder capacity. It is:

1. **Training depth** — one epoch against a 60,000-update budget.
2. **Corpus quality** — labels are `com.project-seam.training-generated-teacher`, so this proves the
   pipeline works, not that the voice is good. R9 still needs a rights-cleared human corpus.
3. **Level restoration** — renders are 5.7-7.6 dB below source RMS.
4. **Listening** — still `NOT_REVIEWED`. Nobody has heard it.

The 187.5 Hz artifact is gone but the level error is not, so recommendation 3 in section 6 narrows to the
level term.


## 4. Remaining work, ranked by what actually blocks Beta GO

| # | Blocker | Owner | Nature | Estimate |
|---|---|---|---|---|
| 1 | **Neural voice quality** (R1, R9, R16) | engineering + owner | build a real singing corpus, then train the correct architecture to convergence (no redesign needed) | **weeks**, not hours |
| 2 | **No rights-cleared voicebank** (R4, R7, R19) | owner (+ external) | rights review, recording or generated source | weeks, human-bound |
| 3 | **Human gates at zero** (listening, creator sessions, native-speaker, Windows host, 5 independent creators) | owner + external | cannot be parallelised by agents | weeks |
| 4 | **Language coverage** (R7) | engineering + native speakers | no dictionary assets found; ja/en/ko phonemizers are code only | weeks |
| 5 | **Style coverage** (R6): 2 reviewed styles minimum | engineering + musicians | reviewed styles needed | days–weeks |
| 6 | **Release governance** (R17, R18) | engineering + owner | stale candidate, empty `releasedResources`, evidence schema | days |
| 7 | **9 host tuples** mostly unexecuted | engineering + hardware | REAPER/Bitwig × CLAP/VST3 × 2 platforms + Logic AUv2 | days–weeks |
| 8 | **Windows CI red** | engineering | 1 job: `api-ms-win-core-synch-l1-2-0.dll` rejected by the dependency gate | **hours** |
| 9 | **Merge to master** | engineering | branch 24 ahead, not merged | hours |
| 10 | **Plan revision** | engineering | current plan is stale (3.2) | hours |

### Engineering-only estimate

The project's own figure is **320–520 h** of automatable engineering (14–22 days at 24/7). That figure is now optimistic in one direction and pessimistic in another:

- **Down:** much of Phase 2/3 already exists (3.2), so those phases shrink.
- **Up:** the neural stage is not an hours problem, but it is a *training* problem rather than a design problem. The architecture is already correct (3.3); a real singing corpus, GPU throughput (3.3b) and several training/evaluation cycles will dominate, and listening acceptance is gated on a human.

### Realistic calendar estimate

- **Engineering-complete, CI green, merged:** a few days, if the 512-channel training campaign is deferred.
- **Full Beta GO, all 20 requirements:** **2–4 months minimum**, dominated by gate 1 and gate 3.

## 5. What is genuinely close

To be fair to the work: the *product-shaped* half is in good condition.

- Native release build: exit 0 (125 targets).
- Local suite: **173/173 PASS**.
- CI: **6 of 7 jobs pass**, including the macOS installer lifecycle, which passes for the first time.
- M1 at 85% engineering: notes, syllables, phoneme timing, articulation, cross-note pitch, a compiled song journey, and a song fixture with a drift guard.
- Renderer capability truthfulness, routing, cancellation and stale-publication handling are thoroughly tested.

**A musician could sit down and do a session today. What nobody has is a voice worth using, or proof that any of it holds on a shipped build.**

## 6. Recommended next step

Stop expanding breadth. The repository has strong, well-tested infrastructure and one unsolved core: **the voice**.

1. ~~**Let r3 finish, then export and re-qualify the 512-channel model.**~~ **Done** (3.5). It finished,
   exported, and re-qualified: 46.676 cents, pitch follows all four notes, comb gone. The next step on this
   path is a longer campaign (this was 1 epoch of a 60,000-update budget) and a real corpus.
2. **Fix the GPU/thread throughput defect before the next run** (3.3b). This is what makes a 60,000-update
   campaign, or several of them, affordable.
3. **Fix the level error** — the 187.5 Hz comb resolved with the trained 512-channel model (3.5), but
   renders still sit 5.7-7.6 dB below source RMS. That term is separate and remains open.
4. **Refresh P0-08's evidence block** (3.4) so it opens on the confirmed frame-rate comb and level error
   instead of the unreproduced 21 kHz figure, and record that the headline numbers came from a
   34,986-parameter smoke fixture.
5. **Run the first real listening session.** It is the only thing that converts "self-consistent" into
   "musical", and it is free.
6. **Land the small wins** (Windows CI gate, merge to master) to stop paying interest on them.
7. **Rewrite the plan from the source tree.** The next developer should not be told to create files that exist.

## 7. Evidence provenance

- Contract: `docs/product/full-product-beta-contract.json`
- Issue register: `BETA_READINESS_ISSUES.md`
- Stale plan: `SEAM_DETAILED_DEVELOPMENT_PLAN_2026-09-19.md`
- Live training: `/Users/lhs/seam-corpus-xl-2026-09-19/vocoder-512-segments-r3`
- Measurements: `recon-tracked4/reconstruction_receipt.json`, `recon-tracked4/item-000001.wav`, `recon-e6-native-pitch/*.wav`, `vocoder-export-e6/export.json`
- Build wiring: `CMakeLists.txt:456-470`, `:1375-1383`
- Listening status: `docs/implementation/listening/2026-09-15-d1-02/decision.md` (`NOT_REVIEWED`)
