# Project SEAM completion plan

Updated 25 November 2026. This is the working milestone plan. The original full-scope plan defines U1–U48. The Alpha, External Beta and full-product records define their own pass conditions. [Full-scope plan](2026-09-05-1718-feat-full-scope-beta-go-plan.md) · [Gate status](../STATUS.md)

## 1. Engineering baseline

The standalone and first-party CLAP editor share the Cerulean faceplate. The PJS engineering bank renders the Japanese observation song. Save, restore and export have engineering coverage. The integrating checkout passed 241/241 Release tests. A new installation still needs an installed singer. [Latest engineering evidence](../implementation/FULL_SCOPE_BETA_EXECUTION.md)

## 2. Critical path

Compare the two voice auditions on the same phrase, record the preferred take or reject both, and run the owner observation. Use that evidence to choose the performer, Japanese source inventory, rights scope, producer and independent reviewer. Then test the same song with the selected production bank from clean install through edit, reopen and export. The release gates still require independent target-host, platform and signing evidence. [Audition guide](../reviews/PRODUCT_VOICE_AND_PLUGIN_REVIEW_2026-11-25.md) · [Ranked next actions](FUNCTIONAL_READINESS.md)

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
| C. Public-license internet recordings (VocalSet f1, CMU ARCTIC SLT, Karoryfer) | **Not viable for Japanese.** None contains Japanese CV syllables (see the source intake records). Keep them as engineering fixtures only. |
| D. An existing third-party Japanese voicebank whose terms allow internal use in other software | Possible for internal evaluation only. Terms vary bank by bank and usually forbid redistribution, so it can never become the Beta bank. |
| E. Installed PJS engineering bank | Available for the listening and workflow rehearsal. It has a male timbre and no production admission. A more feminine product voice still needs a suitable source and rights review. |

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

What a yes changes, measured on 2026-10-19. This is engineering work beyond the contract edit, so it is not prepared ahead of the decision:

- `docs/product/external-beta-acceptance.json` gets `contractVersion` 3.0.0 and a change log. `targetPlatforms` and `closure.requiredPlatforms` become macOS only, EB-006 names `HOST-001`, `-002`, `-005`, `-006` and `-009`, and EB-009 moves to the Beta GO set. The `targetPlatforms` of `external-beta-host-matrix.json` and `external-beta-install-matrix.json` follow.
- Six tools hard-code both platforms and have to take them from the contract: `tools/external_beta/candidate_root.py`, `cohort_gate.py`, `freeze_candidate.py`, `host_evidence.py` (which also fixes the nine tuples as `HOST-001`–`HOST-009`), `predecessor_release.py` and `standalone_evidence.py`. `release_gate.py` fixes EB-001–EB-009 as the required set.
- 28 test and fixture files under `tests/external_beta/` and `tests/production/` mention these platforms or rows; the Windows public-release tests (PW) among them stay as they are.
- Estimate: one to two working days, plus the contract review that rule 4 requires.

### D7 — Working protocol (affects every milestone)

**Recommendation:** adopt the rules in §3. One independent review per milestone exit and for the high-risk areas; no per-unit dated report; ledger entries of at most six lines; evidence JSON only for gate rows. This replaces the per-commit review-and-record cycle used since September (§2.3). AGENTS.md does not require that cycle, so no repository rule has to be overridden, but it is the owner's working practice and the owner should confirm the change.

D7 also settles the branch policy. [ADR 0005](../adr/0005-master-only.md) forbids every branch except `master`, while AGENTS.md sends isolated work to `codex/*` worktree branches; the main checkout has 45 other local branches. The hooks that would enforce ADR 0005 are not installed, so nothing blocks those branches. But `scripts/verify_master_branch.py` fails on them, and a default `seam_license_audit` run fails with that branch check as its only error, so a real license finding would be reported behind a policy error. **Recommendation:** amend ADR 0005 so that `master` is the only integration and release branch and short-lived `codex/*` branches are allowed, then implement E-15.

Prepared on 2026-10-19: the E-15 change for this recommendation is commit `7d10b021` on the local branch `codex/d7-branch-policy` in the integrating checkout, unmerged. It amends ADR 0005, lets `scripts/verify_master_branch.py` accept `master` and `codex/*` (with `--require-master` for integration and release), and drops the branch check from the license audit, which then passes. A yes merges it. Afterwards the only local branch outside the policy is `optimize/native-paint-p95`, which you can delete or rename to `codex/…`.

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

M0-1 through M0-4 are complete. Historical project fixtures now live under `tests/fixtures/projects/`, the phase verifiers check active contracts, and CTest distinguishes fixture producers, smoke journeys and test tiers. D6 and D7 still govern the two policy changes below.

**M0-5 One gate vocabulary (after D6).**

- Make `tools/external_beta/release_status.py` the single status source.
- Reclassify the Phase 13A matrix rows that are outside the full contract's nine tuples as optional by emptying their `mandatoryFor`: the Cubase, Ableton Live, Studio One, FL Studio and GarageBand rows, and the four Linux rows (CLAP validator, 7,200-second soak, VST3 build and VST3 validator). The REAPER and Bitwig Studio rows stay, with Linux dropped from their `platform`. This edits `docs/phase13a/mandatory-validation-matrix.json` and the matching `tests/phase13a/` expectations; `tools/phase13a/release_gate.py` already treats a row as required only for the gates in its `mandatoryFor`.
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

- Done on 2026-10-19 (§6, E-01): pointer reconciliation refuses a path without a `generations` directory before it takes the writer lock, and says *Not a producer workspace*.

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
| E-01 | Producer workspace check before the writer lock (`59002e91` review of 2026-10-15, optional item 1) | Done on 2026-10-19. `reconcileCurrentPointer` checks for a real `generations` directory before it opens the lock file. `reconcile-producer-pointer` on a missing path or an unrelated directory now exits 2 with *Not a producer workspace* and leaves the path untouched; before, it left an empty `.writer.lock` and blamed newer work, or reported a busy lock. The other CLI commands that reach a writer lock (`prepare-sample-review`, `publish-sample`, `create-sample-draft`) already recover the producer, or a stated generation, first. `save` still creates missing workspace directories, which `initialize` relies on | M1-3 usability | Done |
| E-02 | Move test fixtures out of `docs/` and declare CTest fixtures | See M0-1 (done) and M0-3 | Cost: filtered runs and history cleanup | M0 |
| E-03 | CTest labels and measured tiers | Done on 2026-10-18 (M0-4) | Cost: lets §7 tiers run | Done |
| E-04 | Audit and retire phase-era document-locking verifiers; then retire `docs/remaining-tasks.json` | Done on 2026-10-18 (M0-2) | Cost: contract tests that lock history | Done |
| E-05 | One gate vocabulary; Phase 13A extra hosts optional | `tools/external_beta/release_status.py`, `docs/phase13a/mandatory-validation-matrix.json`, `tests/phase13a/` | Removes conflicting host lists | After D6 |
| E-06 | Remaining optional review items from `59002e91` | Studio pointer reconciliation and tests | Item 1 is E-01, done. Item 2: since E-01 a missing workspace and a newer generation held only by the journal are tested; the CLI's signal exits and Studio cancellation before the write are not. Item 9 (`F_FULLFSYNC` on durable writes) belongs to M2 durability; items 3, 4 and 7 when the code is next touched; items 5–6 when Studio reconciliation UX is revisited; item 8 is done | As scheduled |
| E-07 | Move the WAV codec below the platform layer | `libs/seam-voicebank/include/seam/voicebank/wav.hpp` and `src/wav.cpp` depend only on `seam_core`; move them to a new layer-1 `seam_audio_io` (or into `seam_core`), keep a forwarding header so the 100 including files migrate gradually, drop `seam_voicebank` from `seam_platform`'s links, and link it directly from any consumer that relied on reaching it through the platform library | Cost: layering. `seam_platform` drops from layer 4 to 3, and targets that need only the platform layer stop linking `seam_voicebank` and `seam_phonemizer`. Recompilation does not shrink: a `wav.hpp` change recompiles its includers wherever the header lives | When M1/M2 touches recording, or after M2 |
| E-08 | Measure, then consolidate, test executables | Measured on 2026-10-19 (M3 Max, `-j6`; outputs taken from `build/release/.ninja_log` by modification time, because Ninja compacts the log). Touching `libs/seam-voicebank-production/include/seam/voicebank_production/repository.hpp` rebuilt in 50.7 s: 87 compiles (267 s CPU) and 106 links (14.8 s CPU, 5%), with about 0.1 s of linking after the last compile. Touching `repository.cpp` rebuilt in 5.1 s, 3.4 s of it linking after its one compile. The test executables stay as they are. The largest single compile was `tests/test_export_service.cpp`, built into both `seam_tests` and `seam_export_tests` (18.0 s and 15.2 s); the `seam_tests` copy is the only one that runs its in-process recovery branches | Cost: link time. Linking stays far below the 30% threshold on the header loop and costs about 3 s on the source loop | Done: measured, no action |
| E-09 | Split producer UI out of `seam_native_ui` | Studio code that needs `seam_candidate_packaging` moves to a `seam_studio_ui` library; the editor stops linking packaging. Measured on 2026-10-19: the `repository.hpp` touch recompiled nine `voicebank_studio_*.cpp` files (32.5 s CPU) and no editor file, so the split would save relinks and layering, not editor compiles | Cost: editor rebuild isolation | After M2 |
| E-10 | Retire the legacy campaign API | Done on 2026-10-19. `advanceGenerationCampaign` and `CampaignAdvanceResult` are gone, and the shared advancement always has a request registry. The SIGKILL probe and the preflight and export tests call `advanceGenerationRequest`. The export test's over-budget refusal had to move, because under the new API an exhausted budget ends the request: the first-batch case is covered through the CLI by `tests/production/test_generation_request_mirror.py`, and the later-batch case by `test_voice_generation_workflow.cpp`. A new test covers campaigns collected before requests were recorded, which keep advancing without a record | Cost: two lifecycles for one concept | Done |
| E-11 | Decide on the protocol-1 neural launcher | `runNeuralWorker` serves legacy probes; protocol-1 packages must stay parseable | Decision with the M3 neural plan | M3 |
| E-12 | Consolidate user-document channels | `docs/beta/` pointer stubs, `docs/manual/`, `docs/public/`, `docs/support/`, `docs/product/external-beta-documentation.json`, `public-release-acceptance.json` | One source per document with per-channel manifests | After D4 |
| E-13 | Rename historical directories and targets | `phase12c/`, `phase13a/`, `phase13b/`, `tools/phase13a`, `tools/phase13b` | Cost: naming confusion only | Last, after E-04, because verifiers hard-code these paths |
| E-14 | ADR number collision | Done on 2026-10-18: the later 0020 is now `0023-usable-alpha-rendering-architecture.md`, and file 0010's heading, which said 0009, now says 0010 | Hygiene | Done |
| E-15 | Reconcile the branch policy with practice | `docs/adr/0005-master-only.md`; `scripts/verify_master_branch.py` (rejects any local branch except `master`); `.githooks/pre-commit` and `pre-push`; `tools/license-auditor/audit.py`, which adds the branch check to its errors unless `--allow-non-master` is passed. Keep the branch check as its own target and drop it from the license audit, or allow `codex/*` in it | A license audit that passes in the integrating checkout; hooks that can be installed without blocking AGENTS.md's worktree flow | After D7 |

Not on the list on purpose: a rewrite of any library, a new evidence format, a new review layer, additional gate tooling. §3 rule 2 applies.

---

## 7. Verification tiers

| Tier | What runs | When it is required | Measured or expected time |
|---|---|---|---|
| T0 Focused | Build the touched targets and run their tests | Leaf-only changes: one app, one test file, Python tooling | Seconds to 2 minutes |
| T1 Standard | Full `cmake --build build/release -j6`, then the CTest entries for touched areas: `-R` selects by name, `-L` a whole tier, and `-LE '^slow$'` runs the 209 entries under 5 s in about 2 minutes | Any change under `libs/` or to shared headers | About 1–7 minutes to build, depending on fan-in |
| T2 Full | Full serial `ctest --test-dir build/release` | CMake target changes, cross-library refactors, before a milestone review, at least daily during active work | About 12 minutes: 726.7 s for 241 entries at `0bbf3997` on 2026-10-19, and 745.64 s for 240 entries at `a51735c5` on 2026-10-18; the first run on 2026-10-18 took 884 s |
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
- **Retirement:** when a document stops being true, delete it and add one row to [RETIRED_DOCUMENTS.md](../history/RETIRED_DOCUMENTS.md). Do not leave it in place with a "historical" banner. ADRs are the exception because they are append-only decision records: a decision that no longer holds keeps its file and gains a dated status note naming what replaced it.

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

## Historical revisions

Git retains the October diagnosis, completed M0 work and earlier schedules. Current gate counts and verification commands live in the status page. [Status](../STATUS.md)

## Appendix C — Owner decision form

Copy, fill in, and return. Every line maps to §4.

```text
D1 Alpha bank performer and route:            ______________________
D2 Alpha "rights-cleared" wording accepted:    yes / edit: __________
D3 Beta bank route (contract singer / own voice / other): ___________
D4 Rights approver + external reviewer:       ______________________
D5 Producer ID / Alpha reviewer ID / Beta reviewer ID: ______ / ______ / ______
D6 macOS-only External Beta 1 amendment:      yes / no
D7 Review protocol (§3) and branch policy (E-15): yes / edit: __________
D8 Apple Developer, DAW licenses, clean Mac, root-key device: ______
D9 Observation session date (or results):     ______________________
```
