# Project SEAM completion plan

**Date:** 2026-10-18 (Asia/Seoul). **Branch:** `master`. **Supersedes:** every earlier root-level and `docs/plans/` plan as the guide for *what to do next*. It does not change any contract. The 2026-09-05 full-scope plan still defines units U1–U48, and the canonical contracts in `docs/product/` remain the definition of done.

**How to use this document.** Section 1 states where the project stands, with the checks run today. Section 2 explains why it is not finished. Section 3 sets the rules that decide what gets worked on. Section 4 lists the owner decisions that gate everything else, with a recommendation for each. Sections 5–7 are the work: milestones with code-level work packages, the engineering backlog, and the verification tiers. Sections 8–10 cover documentation, risks and progress reporting. Appendix A records the 2026-10-18 cleanup.

---

## 1. Starting point, verified 2026-10-18

### 1.1 Repository

| Measure | Value | How it was obtained |
|---|---|---|
| Product C++ (libs, apps, phase12c) | 194,568 lines | `git ls-files` + `wc -l`, before cleanup |
| Test code (C++ and Python under `tests/`) | 157,995 lines | same |
| Python tools and scripts | 76,525 lines | same |
| Tracked Markdown | 98,419 lines before cleanup | same |
| Static libraries / CLAP modules / test executables | 28 / 2 / 141 | `cmake --graphviz` on the release configuration |
| CTest entries | 240 | `ctest -N` |
| Near-full Release rebuild after a top-level CMake edit | 6 min 39 s, 1,101 steps, `-j6`, M3 Max | measured during cleanup unit 1 |
| Incremental rebuild after removing targets | 41 s, 146 steps | measured during cleanup unit 2 |
| Full serial Release CTest | 14 min 44 s (884 s) for 240 entries | measured during cleanup unit 2 |

The library graph is acyclic and layered (see [architecture overview](../architecture/OVERVIEW.md)). Tests exercise real journeys: installed-singer selection, editing, undo, save/reopen, export and decoding (`tests/test_sing_shell_journey.cpp`, `test_original_singer_song_journey.cpp`, `test_procedural_install_journey.cpp`, `test_studio_generation_bank_journey.cpp`). Nothing in this plan proposes a rewrite.

### 1.2 Gates

| Gate | State | Evidence |
|---|---|---|
| Usable Alpha UA-001–020 | `BLOCKED`, 0/20 | All `NOT_RUN`; `USABLE_ALPHA_CONTRACT=PASS` means only that the record is valid |
| Beta voicebank 01 | fails, 52 errors | `voicebank_gate` exit 3; all 12 categories blocked |
| External Beta EB-001–009 | `BLOCKED`, 0/9 | No candidate, no records; target platforms are macOS arm64 **and Windows x86_64** |
| Full product contract | `NOT_RUN` | 20 requirements, 18 work packages, 83 cases; `releasedResources: []`; matrix `UNRESOLVED` |
| Phase 13A G2–G5 | `passed: false` | No validator, signing, installer, soak or host execution |
| Public release | `BLOCKED` | PW-001–020 all `NOT_RUN` |

[STATUS.md](../STATUS.md) carries this table forward; update it, not this plan, when a gate changes.

### 1.3 What the code can already do

The SING shell edits notes, lyrics, phoneme boundaries, unit choices, seams, pitch points and expression curves with undo/redo. `AuthoringSession` and `ProductionProjectRenderer` render sample and procedural singers. Projects save, reopen and recover from autosave, and a missing bank relinks to its exact ID, version and hash. Export is transactional, with master, stems and receipts. Voicebank Studio and `seam_voicebank_cli` implement the full producer chain: workspace, source registration, takes, inspection, draft manifest, independent review, candidate publication, signing and installation. CLAP plug-ins build and load in a first-party test host.

### 1.4 What is missing

There is no production voicebank and no listening verdict on any song. No user has run the journey unaided. No signed or notarized artifact exists, nothing has run in a commercial host, and there is no Windows run. Every one of these needs a person, an account, a license or a machine; none can be closed by more engineering alone.

---

## 2. Diagnosis: why the project is not finished

### 2.1 The critical path runs through decisions and material, while engineering has been working off it

Usable Alpha's own text says: "The contract remains blocked until a rights-cleared production bank, target Apple Silicon runtime evidence, reopen/recovery/export listening evidence, and the real-song stability session are recorded through the UI" ([contract](../product/USABLE_ALPHA_ACCEPTANCE.md)). UA-004 requires Japanese lyrics and UA-010 sample-concatenative audio. The first user milestone therefore waits on the same voicebank question as Beta.

Engineering since 2026-10-12 has gone into producer-intake machinery for internet sources: prepared phone donors, source-assessment publication identity, pointer recovery and receipt boundaries. Those sources cannot become a Japanese bank. VocalSet f1 is sung vowel exercises (scales, arpeggios, long tones) plus three short lyric excerpts in English, Italian and Latin. CMU ARCTIC SLT is 16 kHz read English. Karoryfer Merry Orks is metal growls. None contains Japanese consonant–vowel syllables, so none can satisfy UA-004/UA-010 in Japanese, whatever their licenses allow. The machinery is sound and will be reused, but it did not move a gate.

### 2.2 The contracted scope is a multi-team program

The full Beta contract covers three languages (ja, en, ko), two platforms, nine host tuples (four on Windows), six resource kinds and four backends. It has 83 cases and requires 60 phrases per language, three songs, five independent creators, native-language reviewers, and rights, security and release reviewers. External Beta also lists Windows x86_64 as a target platform. With Windows deferred, Beta GO is unreachable by definition, and the macOS-only parts alone need an Apple Developer ID, three commercial DAWs and several independent people.

This is not an argument for lowering the bar. It is an argument for **sequencing**: a smaller, honest first Beta, then the full contract.

### 2.3 Process overhead is now larger than capability work

| Signal | Value |
|---|---|
| Commits since 2026-10-12 | 154: 64 docs-only, 90 mixed, **0 code-only** |
| Commits titled "Record…" or "Retain…" (review and approval bookkeeping) since 2026-10-12 | 41 |
| Lines added in the week of 2026-10-12 | 23,693 in docs/evidence vs 15,888 in code |
| Root ledger `FULL_SCOPE_BETA_EXECUTION.md` | 9,642 lines |
| `docs/implementation/` | 481 files (254 reports, 142 evidence JSON, 79 logs) |
| Contract and schema JSON under `docs/product/` | 44 files |

Each small unit now produces a report, an evidence JSON, a ledger entry, an independent review request and a "record the review" commit. The [2026-10-10 economics diagnosis](../reviews/DEVELOPMENT_ECONOMICS_DIAGNOSIS_2026-10-10_KO.md) reached the same conclusion: verification and integrity work keeps being chosen as the next task because it is always available, while the product question waits on people.

### 2.4 Six gate systems overlap

Usable Alpha (UA), the Beta bank gate, External Beta (EB), the full product contract (R/V/cases), Phase 13A G1–G5 and public release (`PUBLIC_ACTIVE`, PW) each have their own vocabulary and verifier. Their host lists disagree. Phase 13A still names Ableton Live, Cubase, FL Studio, GarageBand and Linux targets, none of which is among the full contract's nine mandatory tuples. Every gate change must be explained in several places, and a reader cannot tell which list governs.

### 2.5 Structural code costs (measured from the target graph)

1. `seam_platform` links `seam_voicebank` only to reach `seam/voicebank/wav.hpp` from `recording_session`. That lifts the platform library from layer 3 to layer 4 and makes every platform consumer link `seam_voicebank` and `seam_phonemizer`. `seam_formats` and `seam_domain` stay below the platform layer either way: `crash_capture.cpp` uses the JSON codec and the audio devices include `seam/domain/routing.hpp`.
2. `seam_native_ui` (about 55k lines) links the authoring runtime, the producer packaging stack and character packages at once.
3. 141 test executables each statically link large parts of the tree, so link time dominates many rebuilds. The libraries with the highest fan-in are `seam_core` (27), `seam_domain` (17), `seam_formats` (15) and `seam_voicebank` (13).
4. Current code lives under historical names (`phase12c/`, `phase13a/`, `phase13b/`, `seam_phaseN_demo`), and two C++ test files read project fixtures from `docs/phase2/evidence` and `docs/phase3/evidence`.

These raise change cost. None of them blocks a gate.

### 2.6 What is not the problem

The architecture is not wrong. The admission, snapshot and cache boundaries are deliberate and tested, and the 2026-10-11 staleness audit (RS-01–RS-11) found no case for a rewrite. Sound quality is still **unknown** rather than known to be bad: nobody has judged a rendered song.

---

## 3. Rules for the remaining work

1. **Every task names the gate row it unblocks** (UA-xxx, a bank-gate category, EB-xxx, R-x) or the measured cost it removes. A task that can name neither is not scheduled.
2. **Ask the product question before building more proof.** No new verification infrastructure, evidence format or review layer unless a gate's own verifier requires it.
3. **Owner decisions first.** Engineering prepares a concrete decision packet: options, consequences and the exact files that change. It does not proceed past a pending decision by building speculative alternatives.
4. **Evidence proportional to risk.** Signing, trust, rights, installation, recovery and audio-callback changes keep the full review and sanitizer treatment. UI wording, docs and test-only changes do not.
5. **One review per milestone exit**, plus targeted review for the high-risk areas in rule 4. No "record the review" commit per unit; the review verdict goes into the milestone's single review file.
6. **One status page.** [STATUS.md](../STATUS.md) changes when a gate changes; dated status copies are not created.
7. **Stop rule.** If two consecutive working sessions land no gate movement and no measured cost reduction, stop and re-plan with the owner instead of continuing.


---

## 4. Owner decisions — make these first

Each decision lists a recommendation, the realistic alternatives, what changes in the repository once it is made, and what the owner has to supply. Engineering can prepare every packet in advance, but it cannot make these choices.

### D1 — Voicebank for Usable Alpha (blocks M1)

**Recommendation: record a small, song-scoped Japanese cohort with a performer you can get written permission from today: yourself, or someone who agrees to internal use.** Usable Alpha needs only the units that the chosen 30–40 second song actually uses, at its pitch range, not a full Beta inventory. For a typical 30–40 second song that is an estimated 40–100 short prompts, or one to two hours of recording.

| Option | Assessment |
|---|---|
| **A. Song-scoped self/acquaintance recording (recommended)** | Fastest lawful route. The rights are simple because the performer is present and signs. Quality is good enough to judge the editor and renderer, and the session also tests the real producer chain end to end. |
| B. Wait for the contract singer of D3 | Correct for Beta, but it puts Usable Alpha behind contracting and scheduling. |
| C. Public-license internet recordings (VocalSet f1, CMU ARCTIC SLT, Karoryfer) | **Not viable for Japanese.** None contains Japanese CV syllables (see §2.1). Keep them as engineering fixtures only. |
| D. An existing third-party Japanese voicebank whose terms allow internal use in other software | Possible for internal evaluation only. Terms vary bank by bank and usually forbid redistribution, so it can never become the Beta bank. |

**Owner supplies:** the performer, a signed internal-use permission (start from `docs/legal/templates/VOICE_PROVIDER_CONTRACT_EVIDENCE_TEMPLATE.md`), the song (lyrics in kana plus melody, typed in the editor or as MIDI), a microphone on the existing audio interface, and the recording time.

### D2 — What "rights-cleared production bank" means for Usable Alpha (blocks M1)

**Recommendation:** the performer has granted in writing the use of the recordings for internal evaluation on the owner's machine, and the bank was produced, reviewed, signed and installed through the production path. Distribution rights belong to External Beta (EB-003). Record this as a dated clarification in [USABLE_ALPHA_ACCEPTANCE.md](../product/USABLE_ALPHA_ACCEPTANCE.md) and its Korean mirror. The verifier and the 20 requirements do not change.

### D3 — Voicebank for External Beta (blocks M2)

**Recommendation: contract recording with one Japanese-fluent female singer comfortable at C4–C5 (MIDI 60–72),** under [VOICE_PROVIDER_CONTRACT_REQUIREMENTS.md](../legal/VOICE_PROVIDER_CONTRACT_REQUIREMENTS.md). The grant must cover recording source use, transformation, redistribution inside the bank and end-user rendered audio; these are the four permissions the bank gate checks. Start with a pilot session of 100–150 prompts covering sustained vowels, CV onsets, VC releases, VV continuity and rest/re-entry. Commission the full inventory only after the pilot passes listening.

Inventory size, measured today with `seam_voicebank_cli new-producer-workspace`: the default Japanese draft profile has 342 coverage keys × 3 pitch layers × 2 takes = **2,052 prompts**. The Beta dossier's two layers (60, 72) give **1,368**. At about ten seconds per take this is 4–6 hours of singing before retakes, or two to three sessions.

**Alternative:** the owner's own voice, if the owner is willing to distribute it. Rights become trivial and timing is under the owner's control; the trade is vocal quality and range. Public-license material is rejected for the reason in D1-C.

### D4 — Rights approver and EULA (blocks M2)

**Recommendation:** the owner signs as final rights approver, and an external copyright reviewer checks the grant (R19 requires a rights reviewer). With a contract-recorded bank (D3), `docs/manual/EULA.md` can keep forbidding bank redistribution: the conflict noted on 2026-10-15 exists only with CC BY sources, which D3 avoids. Freeze `publicLicenseSummary` and `providerIdentityDisclosure` (`DISCLOSED` or `WITHHELD`) in the dossier.

### D5 — Producer and reviewer identities (blocks M1 review step)

The code enforces separation: whoever imported, processed, annotated or inspected a take cannot review it (`libs/seam-voicebank-production/src/review_transition.cpp`). Identity strings are 1–128 bytes, permanent, and limited to one role each (`reviewer_registration.cpp`).

**Recommendation:** the producer is the owner. For **M1**, the reviewer can be the performer if someone else sings; if the owner sings, a second person must review. For **M2**, the reviewer is a distinct Japanese-fluent musician, neither producer nor singer, listening on closed headphones. Choose the permanent ID strings once.

### D6 — Scope of the first External Beta (blocks M2 planning)

**Recommendation: amend External Beta into a macOS-only "External Beta 1"** and leave the full product contract untouched as the later Beta GO target. Concretely:

- `docs/product/external-beta-acceptance.json`: `targetPlatforms` becomes macOS arm64 only;
- EB-006 becomes the five macOS tuples: REAPER CLAP and VST3, Bitwig Studio CLAP and VST3, Logic Pro AUv2;
- EB-009 (complete full-scope semantic acceptance) moves from EB1's mandatory set to Beta GO.

Without this amendment External Beta cannot start until a Windows host exists. The amendment needs a contract revision with an explicit change log; it must not edit history or reinterpret existing rows.

### D7 — Working protocol (affects every milestone)

**Recommendation:** adopt the rules in §3. One independent review per milestone exit and for the high-risk areas; no per-unit dated report; ledger entries of at most six lines; evidence JSON only for gate rows. This replaces the per-commit review-and-record cycle used since September (§2.3). AGENTS.md does not require that cycle, so no repository rule has to be overridden, but it is the owner's working practice and the owner should confirm the change.

### D8 — Accounts, licenses and machines (blocks parts of M2)

The owner supplies:

- Apple Developer Program membership: Developer ID Application and Installer certificates, plus notarization credentials;
- licenses for REAPER, Bitwig Studio and Logic Pro;
- a clean macOS environment, either a separate user account or a VM;
- an offline machine or hardware token for the bank-trust root key ceremony;
- for M3, a Windows x64 host.

### D9 — Observation session (independent; can happen today)

Run `python3 tools/observation/open_native_song.py` (frozen kit, 95 files verified) and fill in `docs/implementation/NATIVE_OBSERVATION_FORM_2026-10-12.json`. Use `UNAIDED`, `HELP_NEEDED`, `BLOCKED` or `NOT_OBSERVED`, list three ranked problems, and say whether sound quality or workflow is the larger obstacle. This needs no bank and no other decision.

---

## 5. Milestones

Durations below are **estimates** for one developer working with coding agents. They assume prompt owner decisions and are not commitments. Re-estimate at each milestone exit from measured velocity.

| Milestone | Outcome | Depends on | Estimate after dependencies clear |
|---|---|---|---|
| M0 Repository reset | Clean entry points, fixtures out of `docs/`, labeled tests, one gate vocabulary | — | 2–4 working days |
| M1 Usable Alpha | UA-001–020 `PASS` on the M3 Max | D1, D2, D5, D9 | 2–4 weeks, dominated by recording and repair |
| G-M1 Musical viability decision | The owner judges the rendered song | M1 | one listening session |
| M2 External Beta 1 (macOS) | EB-001–008 recorded under the amended contract | D3, D4, D6, D8, G-M1 | 8–14 weeks, dominated by bank recording/QC, signing and host runs |
| M3 Full Beta GO and public release | Full contract, Windows, ja/en/ko, neural, nine tuples, PW-001–020 | M2 | Re-plan after M2 |

### M0 — Repository reset

Already done on 2026-10-18: see Appendix A. Remaining work packages:

**M0-1 Move test fixtures out of `docs/`.**

- `tests/test_performance_contracts.cpp` (lines 72 and 174–175) and `tests/test_schema8_performance.cpp` (lines 234–235) read `docs/phase2/evidence/phase2-demo.seam.json` and `docs/phase3/evidence/phase3-demo.seam.json`. Copy both files into `tests/fixtures/projects/`, point the tests there, and leave the historical evidence untouched.
- Verify with the affected CTest entries.

**Gate link:** a cost item; it decouples evidence history from test inputs.

**M0-2 Audit the phase-era verifiers.**

- For each of `scripts/verify_phase11_contracts.py`, `verify_phase11_sources.py`, `verify_phase12a_contracts.py`, `verify_phase12b_contracts.py`, `verify_phase12c_*`, `verify_phase13a_contracts.py` and `verify_phase13b_contracts.py`, list every assertion and classify it as *guards current code or contract* or *guards the wording of a historical document*.
- Retire the second kind along with `docs/remaining-tasks.json` once nothing reads it. Keep the first kind, renamed by what it guards.

Expected result: fewer contract tests that fail when history is tidied, with no loss of product guarding.

**M0-3 Rename fixture producers and declare fixtures.**

- Rename `seam_phase2_demo` … `seam_phase12b_demo` to `seam_fixture_*` by what they produce, for example `seam_fixture_synthetic_bank` for phase 2 and `seam_fixture_clap_state_wav` for phase 10.
- Replace CTest `DEPENDS` ordering with `FIXTURES_SETUP`/`FIXTURES_REQUIRED`, so that `ctest -R <consumer>` runs its producer automatically. Today `ctest -R seam_voicebank_cli_validate` on its own either fails in a fresh build directory or silently reuses an earlier run's output.

**M0-4 Label the tests.**

- The top-level `CMakeLists.txt` has only ten `LABELS` uses today, and the subdirectories have none. Add `unit`, `journey`, `contract`, `gui`, `slow` and `process-lifetime` labels in `CMakeLists.txt` and the three subdirectory files, so the tiers in §7 can be run with `ctest -L`.
- Record the measured duration of each tier in [STATUS.md](../STATUS.md).

**M0-5 One gate vocabulary (after D6).**

- Make `tools/external_beta/release_status.py` the single status source.
- Reclassify Phase 13A matrix rows that are outside the full contract's nine tuples (Ableton Live, Cubase, FL Studio, GarageBand, Linux) as optional. This edits `docs/phase13a/mandatory-validation-matrix.json` and the matching `tests/phase13a/` expectations.
- Treat G1–G5 as an implementation checklist that feeds EB-004/EB-006, not as a separate release status.

**M0-6 Ledger format (after D7).**

- Close the current body of `docs/implementation/FULL_SCOPE_BETA_EXECUTION.md` with a dated marker, and move the 9,600 historical lines in one mechanical commit to `docs/implementation/archive/FULL_SCOPE_BETA_EXECUTION_2026-08-10_to_2026-10-18.md`.
- Continue the ledger with six-line entries: *change, gate row or cost, verification, commit, open item*.

### M1 — Usable Alpha on the target Mac

**Entry:** D1, D2 and D5 decided; D9 ideally done.

**M1-1 Observation (D9).** Use the outcome to rank repair work in M1-5. If workflow, not sound, is the larger obstacle, M1-5 starts with the editor.

**M1-2 Song and prompt list.**

- The owner provides the song.
- The developer phonemizes it through the editor (`libs/seam-phonemizer/src/japanese_phonemizer.cpp`), lists the required units at the song's pitch range using the coverage diagnostics (`libs/seam-authoring-runtime/include/seam/authoring/inventory_coverage.hpp`), and produces a prompt list in the producer workspace's `recording-script.csv` format.
- If the draft profile cannot be restricted to a subset, use the default draft and record only the needed prompts. The remaining rows stay `NOT_ASSESSED`, which is honest and sufficient for UA-010.

**M1-3 Record and produce the bank.** Record mono takes in Voicebank Studio at the producer profile's sample rate, then run the existing chain (CLI shown; Studio exposes the same steps):

```text
new-producer-workspace -> register-source (human; permissions per D2; license file = signed grant, hashed)
-> takes (Studio recording or import) -> record-current-take-inspection
-> create-sample-draft -> prepare-sample-review -> review-sample (D5 reviewer)
-> publish-sample -> package-candidate (local key from seam_bank_tool keygen)
-> verify-candidate-package -> install-candidate -> select the installed singer in Project SEAM.app
```

`tests/test_studio_generation_bank_journey.cpp` and the English candidate lifecycle tests already cover this chain with fixtures. The work here is running it with real takes.

- Fix optional review item 1 from the 2026-10-15 review first (see §6, E-01): the CLI must verify that `WORKSPACE` is a producer workspace before it takes the writer lock.

**M1-4 First render and verdict.** Render the song in the editor and export it. The owner listens and records a verdict: *acceptable for Alpha* or *not acceptable*, plus the top three problems. This is the first musical judgment the project has.

**M1-5 Bounded repair loop.**

- Fix the ranked problems from M1-1 and M1-4 on the same material. Each fix states its before/after criterion and is checked on the same song.
- Time box: two weeks. If the verdict is still *not acceptable* after that, go to the G-M1 decision rather than continuing.

**M1-6 Unaided Usable Alpha run.**

- The owner performs UA-001–UA-019 from Finder without a CLI. Store compact evidence (screenshots, exported WAV, logs) under `docs/product/evidence/usable-alpha/<date>/`, which is tracked because `scripts/verify_tracked_source_closure.py` requires files under `docs/` to be indexed.
- Record each file's SHA-256 in the matching row of `docs/product/usable-alpha-acceptance.json`.
- Measure the contract's quantitative targets where instrumentation exists: cold launch, edit-to-preview latency, underflow count, save time and memory growth. Use `footprint` or `vmmap` for memory.

**M1-7 Stability session (UA-020).** Work for 30 minutes on the song with zero underruns, zero data-loss defects and no memory growth above 100 MiB over the warmed baseline. Reuse the existing soak tooling (`tools/external_beta/product_soak.py`, `scripts/run_external_beta_product_soak.py`) where it fits an unsigned local build, and add only the missing sampling rather than a new framework.

**Exit:** `python3 -B scripts/verify_usable_alpha_contract.py --root .` passes with all 20 rows `PASS` and the gate `PASSED`. Update [STATUS.md](../STATUS.md) and hold one milestone review.

### G-M1 — Musical viability decision

With the rendered song in hand, the owner answers: *is sample-concatenative singing in this engine good enough to justify a full Beta recording?*

- **Yes:** proceed to M2.
- **Not yet:** spend the next block on the renderer (timing, seams, pitch, unit selection), using the same song as the yardstick, before commissioning the M2 recording.
- **No:** re-plan the backend strategy (procedural or neural primary) with the owner. Do not build Beta release infrastructure first.

### M2 — External Beta 1 on macOS

**Entry:** D3, D4, D6 and D8 decided; G-M1 = yes.

| Work package | Files and tools that already exist | What is new |
|---|---|---|
| M2-1 Beta bank recording | Producer chain from M1-3; `docs/voicebank/templates/*`; Beta inventory profile `beta-ja-cvvc-v1` | Contract, pilot session, full sessions, retakes |
| M2-2 Bank gate closure | `tools/external_beta/voicebank_gate.py`; `docs/voicebank/beta-voicebank-01-dossier.json` | Fill the 12 categories (table below) |
| M2-3 Bank trust | `seam_bank_tool keygen`, distribution trust code | Offline root key, delegated bank key, rotation authorized by root, trust epoch, compromise cutoff |
| M2-4 Signing and notarization | `scripts/sign_notarize_macos.sh`, `package_macos_installer.sh`, `notarize_macos_installer.sh`, `package_macos_plugins.sh`, `sign_macos_plugin_payload.sh` | First run with real Developer ID identities; `spctl`/`stapler` checks |
| M2-5 Clean install, update, uninstall (EB-004) | `scripts/test_macos_installer.sh`, `run_external_beta_install_evidence.py` | Clean macOS environment |
| M2-6 Host matrix (EB-006) | `scripts/run_external_beta_host_evidence.py`, `tools/external_beta/host_collector.py`, `run_vst3_validator.py`, `run_auval.py`, `run_clap_validator.sh` | Five macOS tuples in licensed DAWs |
| M2-7 Standalone soak (EB-005) | `scripts/run_external_beta_product_soak.py`, `docs/product/external-beta-performance-workloads.json` | 30/120-minute signed-installed runs |
| M2-8 Provenance archive (EB-007) | `scripts/run_external_beta_evidence_audit.py` | An immutable external store chosen by the owner |
| M2-9 Identity, defects, authorization (EB-001/002/008) | `scripts/run_external_beta_release_audit.py`, release identity tooling | Release-role reviewers |
| M2-10 Cohort | `scripts/run_external_beta_cohort_gate.py`, `docs/manual/`, `docs/support/` | Invite testers; operate support |

Bank-gate categories and where each comes from:

| Category | Produced by |
|---|---|
| rights-approval | D3/D4 grant; redacted approval hash in the dossier |
| recording-session-logs | One `beta-recording-session-log.json` per session |
| source-derived-hash-inventory | Source and derived asset hashes from the producer workspace |
| inventory-coverage | Coverage report against `beta-ja-cvvc-v1`; must be `PASS` |
| retake-closure | Retake records closing every rejected prompt |
| marker-pitch-qa | Reviewer QC of markers, pitch marks and loops |
| renderer-listening-qa | Listening QA template per renderer |
| hostile-package-validation | Hostile-input checks on the signed package |
| signed-package | `package-candidate` with the delegated bank key |
| clean-install | Install and revalidation on the clean environment |
| reference-song | Reference project, render receipt and reviewer verdict |
| evidence-root | The archive root for all of the above |

**Exit:** EB-001–008 recorded under the amended contract, STATUS updated, milestone review, and the owner's release decision.

### M3 — Full Beta GO and public release

M3 covers:

- Windows: the five README TODO items, including a vector editor backend.
- English and Korean: phonemizer qualification and banks.
- An original neural singer with installed model lineage (`candidate_package.cpp` currently refuses model installation).
- Character 01 production assets and clearance.
- All nine host tuples and all 83 cases, with the evaluation profile frozen (13 pending entries).
- Five independent creators, and public release PW-001–020.

Plan M3 after M2, with measured velocity. Starting it earlier repeats the pattern described in §2.1.


---

## 6. Engineering backlog

Ordered by when the item is worth doing, not by size. Each item names its gate link or the cost it removes.

| ID | Item | Code | Gate or cost | When |
|---|---|---|---|---|
| E-01 | Producer commands check that `WORKSPACE/generations` is a directory before they take the writer lock (`59002e91` review of 2026-10-15, optional item 1) | `apps/seam-voicebank-cli/` producer commands, or `ProductionProjectRepository` so that every repository-backed command shares the check; the lock file is opened with `O_CREAT` in `libs/seam-voicebank-production/src/repository.cpp:58` | M1-3 usability: today an unrelated directory gets an empty `.writer.lock` and a refusal that blames newer work, and a missing path reports a busy lock | M1-3 |
| E-02 | Move test fixtures out of `docs/` and declare CTest fixtures | See M0-1, M0-3 | Cost: filtered runs and history cleanup | M0 |
| E-03 | CTest labels and measured tiers | `CMakeLists.txt`, `phase12c/`, `phase13a/`, `phase13b/` | Cost: lets §7 tiers run | M0 |
| E-04 | Audit and retire phase-era document-locking verifiers; then retire `docs/remaining-tasks.json` | `scripts/verify_phase1{1,2a,2b,3a,3b}_*.py`, `tests/phase13b/test_source_contract.py` | Cost: contract tests that lock history | M0 |
| E-05 | One gate vocabulary; Phase 13A extra hosts optional | `tools/external_beta/release_status.py`, `docs/phase13a/mandatory-validation-matrix.json`, `tests/phase13a/` | Removes conflicting host lists | After D6 |
| E-06 | Remaining optional review items from `59002e91` | Studio pointer reconciliation and tests | Item 9 (`F_FULLFSYNC` on durable writes) belongs to M2 durability; items 2–4 and 7 when the code is next touched; items 5–6 when Studio reconciliation UX is revisited; item 8 is done | As scheduled |
| E-07 | Move the WAV codec below the platform layer | `libs/seam-voicebank/include/seam/voicebank/wav.hpp` and `src/wav.cpp` depend only on `seam_core`; move them to a new layer-1 `seam_audio_io` (or into `seam_core`), keep a forwarding header so the 100 including files migrate gradually, drop `seam_voicebank` from `seam_platform`'s links, and link it directly from any consumer that relied on reaching it through the platform library | Cost: layering. `seam_platform` drops from layer 4 to 3, and targets that need only the platform layer stop linking `seam_voicebank` and `seam_phonemizer`. Recompilation does not shrink: a `wav.hpp` change recompiles its includers wherever the header lives | When M1/M2 touches recording, or after M2 |
| E-08 | Measure, then consolidate, test executables | 141 test executables; link time from `build/release/.ninja_log` after a shared-header touch | Cost: link time. Act only if linking exceeds about 30% of the focused loop; group doctest suites per layer and keep CTest granularity with `--test-suite` filters | After measurement |
| E-09 | Split producer UI out of `seam_native_ui` | Studio code that needs `seam_candidate_packaging` moves to a `seam_studio_ui` library; the editor stops linking packaging | Cost: editor rebuild isolation | After M2 unless measured earlier |
| E-10 | Retire the legacy campaign API | Remove `advanceGenerationCampaign` (`libs/seam-authoring-runtime/include/seam/authoring/generation_campaign.hpp:83`, `src/generation_campaign_advance.cpp`, 345 lines) after migrating `tests/helpers/generation_job_probe.cpp:11`, `tests/test_inventory_preflight.cpp:188/208/224` and `tests/test_export_service.cpp:1050/1070/1084` to `submitGenerationCampaign` / `advanceGenerationRequest` | Cost: two lifecycles for one concept; the production path already uses the new one | M0 or any quiet day |
| E-11 | Decide on the protocol-1 neural launcher | `runNeuralWorker` serves legacy probes; protocol-1 packages must stay parseable | Decision with the M3 neural plan | M3 |
| E-12 | Consolidate user-document channels | `docs/beta/` pointer stubs, `docs/manual/`, `docs/public/`, `docs/support/`, `docs/product/external-beta-documentation.json`, `public-release-acceptance.json` | One source per document with per-channel manifests | After D4 |
| E-13 | Rename historical directories and targets | `phase12c/`, `phase13a/`, `phase13b/`, `tools/phase13a`, `tools/phase13b` | Cost: naming confusion only | Last, after E-04, because verifiers hard-code these paths |
| E-14 | ADR number collision | Two `docs/adr/0020-*` files; renumber the later one to 0023 and update links | Hygiene | Any time |

Not on the list on purpose: a rewrite of any library, a new evidence format, a new review layer, additional gate tooling. §3 rule 2 applies.

---

## 7. Verification tiers

| Tier | What runs | When it is required | Measured or expected time |
|---|---|---|---|
| T0 Focused | Build the touched targets and run their tests | Leaf-only changes: one app, one test file, Python tooling | Seconds to 2 minutes |
| T1 Standard | Full `cmake --build build/release -j6`, then the CTest entries for touched areas (by label once M0-4 lands) | Any change under `libs/` or to shared headers | About 1–7 minutes to build, depending on fan-in |
| T2 Full | Full serial `ctest --test-dir build/release` | CMake target changes, cross-library refactors, before a milestone review, at least daily during active work | 14 min 44 s (884 s) for 240 entries measured on 2026-10-18 |
| T3 Instrumented | ASan/UBSan (`build/sanitize`) and TSan (`build/thread-sanitize`) on the affected entries | Concurrency, memory ownership, process lifetime, audio callback, durability code | Scoped; minutes |
| T4 Human and target | Listening, unaided journeys, signed installs, DAWs, clean environments | Milestone exits only | Owner time |

Keep the hygiene rules in [AGENTS.md](../../AGENTS.md): a full dependent rebuild after `libs/` changes, a timestamp check before believing a failure, and no claim of human, DAW, VoiceOver, signing, Windows or reviewer acceptance without its evidence.

---

## 8. Documentation and review protocol

- **Status:** [STATUS.md](../STATUS.md) is the only status page. Change a row only when that gate's verifier changes state.
- **Ledger:** one entry of at most six lines per landed unit (change, gate row or cost, verification, commit, open item). The integrating developer writes it.
- **Reports:** no dated per-unit report. Write one report per milestone exit, plus a decision record when an owner decision changes a contract.
- **Evidence:** hash-bound files only for gate rows (UA, bank gate, EB) and milestone reviews. Local engineering logs stay under ignored `out/`.
- **Review:** one independent review per milestone exit, plus targeted review of signing, trust, rights, installation, recovery and audio-callback changes. A verdict goes into that milestone's review file; it does not get its own commit.
- **Retirement:** when a document stops being true, delete it and add one row to [RETIRED_DOCUMENTS.md](../history/RETIRED_DOCUMENTS.md). Do not leave it in place with a "historical" banner.

---

## 9. Risks

| Risk | Signal | Response |
|---|---|---|
| R1 Rights path stalls (no singer, no grant) | D3 open after the M1 exit | M1 stays valid internally; consider the owner-voice alternative in D3; do not start M2 infrastructure work that assumes a bank |
| R2 Sound is not good enough | G-M1 verdict *not yet* or *no* | Renderer work on the same song before any bank investment; if still *no*, re-plan backend strategy |
| R3 Process overhead returns | Commits without gate movement; growing records | §3 rule 7 stop rule; check the ratio of code-only commits weekly |
| R4 Windows stays unavailable | No Windows host by M2 exit | D6 keeps External Beta 1 moving; full Beta GO waits, explicitly |
| R5 Owner availability | Decisions or sessions slip | Prepare packets in advance; batch owner sessions (observation, recording, review, UA run) |
| R6 Locked or unattended Mac blocks GUI checks | AppKit automation unavailable | Schedule GUI evidence during owner sessions; headless journeys cover engineering regressions |
| R7 Hidden coupling exposed by cleanup | A test fails after a retirement | Retirements are git-recoverable (Appendix A); restore the one file and fix its consumer |

---

## 10. Reporting progress honestly

Report counts against each gate's own denominator: UA *x*/20, bank-gate categories *y*/12, EB *z*/8 for External Beta 1. Do not compute a weighted overall percentage. Earlier weighted indices (54–56%) had judgment-based weights and denominators that do not match these gates, and they invite the wrong conclusion. Report a milestone as complete only when its exit check passes, never by summing partial work.

---

## Appendix A — Cleanup performed on 2026-10-18

Each unit was verified before commit and pushed to `origin/master`. Local `HEAD`, `origin/master` and `git ls-remote` agreed after every push. Every removed file is listed with its last containing commit in [RETIRED_DOCUMENTS.md](../history/RETIRED_DOCUMENTS.md); restore any of them with `git show <commit>:<path>`.

| Commit | Unit | Verification |
|---|---|---|
| `fd0893da` | Removed four never-included public headers (`seam/core/hash.hpp`, `seam/distribution/signer_provider.hpp`, `seam/live_voice/expression.hpp`, `seam/platform/desktop_backend.hpp`), the inert `SEAM_ENABLE_IPLUG2_SKIA` option and its architecture note | Reconfigure; full Release build, 1,101 planned steps, no errors; source-closure and neural-direction CTest 3/3 |
| `c52517f9`, `41dc63fd` | Retired nine `scripts/generate_phaseN_evidence.py` generators and their `seam_phaseN_evidence` targets (they overwrote tracked historical evidence), the Phase 1 demo, six benchmarks used only by those generators, and `SvgEditorRenderer`, which only the Phase 1 demo used | Reconfigure; Release build; full serial CTest 239/240; the one failure was the signer-provider presence check, fixed in `c52517f9` and passing on rerun |
| `bdb04d82` | Retired 82 files: an unrelated project's guide committed by mistake, 21 root phase reports, 23 root dated plans, reviews and logs, 6 stale status/backlog/readiness pages, 17 superseded plans and agent planning files, 8 dated product reports, and 4 superseded architecture/UX notes and placeholders; untracked 2 agent artifacts (local copies kept). Rewrote README, STATUS and the architecture overview from the target graph; added the documentation map, the retirement index and this plan; updated product principles 1 and 7 to the three resource families | Documentation, link and contract checks listed in the commit |
| The commit after `bdb04d82` | Root ledger entry for the cleanup and this table | Link and diff checks |

One slip is recorded here rather than hidden: `c52517f9` was meant to carry only the test fix, but the already-staged deletions of the generator unit went with it while that unit's `CMakeLists.txt` edit stayed unstaged. `c52517f9` alone therefore does not configure. `41dc63fd` followed immediately and restored the verified tree; history was not rewritten. Bisecting across this range should skip `c52517f9`.

The `bdb04d82` commit message also opens with "47 dated plans, phase reports, reviews and logs". Its own list (21 + 23) and the tree diff give 44, plus the unrelated guide, for 45 retired root files. The retirement index is correct.

Not changed: `.github`, the Windows TODO, the U45 hold, every contract JSON, every gate verifier's semantics, `stash@{0}` and all untracked local evidence under `out/`.

## Appendix B — Retained although stale, and why

| Item | Why it stays | Exit |
|---|---|---|
| `seam_phase2_demo` … `seam_phase12b_demo` | Smoke tests consume their outputs as fixtures | M0-3 renames them |
| `docs/phase1`–`docs/phase13b` | Some files are test fixtures or verifier inputs | M0-1, M0-2 |
| `docs/remaining-tasks.json` | Read by the Phase 12B/13B verifiers | E-04 |
| `docs/beta/*` stubs | Required entry points in the External Beta source contract | E-12 |
| `BETA_READINESS_ISSUES.md` (last substantive update 2026-10-08) | `tools/external_beta/release_status.py` and `tests/external_beta/test_readiness_register_citations.py` read it | Fold its open items into this plan's backlog, then move it with its two consumers |
| `VIRTUAL_SINGER_FEASIBILITY_AND_CODE_ROADMAP_2026-09-05.md` | Named as `origin` by the full product contract | Stays as long as that contract revision |
| `docs/plans/2026-09-05-…`, `2026-10-08-…` | U1–U48 definitions and the previous goal record | Records |
| `docs/implementation/`, `docs/reviews/` | Dated records; many are hash-bound in evidence JSON | Records |
| Phase 13A Linux and extra-host rows; `scripts/package_linux_development.sh` (no consumer) | Linux is not a contract platform, but G2 still names Linux validation | E-05 |
| `advanceGenerationCampaign`, `runNeuralWorker` | Tests and legacy probes still call them | E-10, E-11 |
| DESIGN.md "retained raster" section | Voicebank Studio and raster utilities still use those tokens | Until Studio moves to the SING design system |

## Appendix C — Owner decision form

Copy, fill in, and return. Every line maps to §4.

```text
D1 Alpha bank performer and route:            ______________________
D2 Alpha "rights-cleared" wording accepted:    yes / edit: __________
D3 Beta bank route (contract singer / own voice / other): ___________
D4 Rights approver + external reviewer:       ______________________
D5 Producer ID / Alpha reviewer ID / Beta reviewer ID: ______ / ______ / ______
D6 macOS-only External Beta 1 amendment:      yes / no
D7 Milestone review protocol (§3):            yes / edit: __________
D8 Apple Developer, DAW licenses, clean Mac, root-key device: ______
D9 Observation session date (or results):     ______________________
```
