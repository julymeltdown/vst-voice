# Project SEAM — functional readiness

**Living document.** It answers one question: *what still has to happen before Project SEAM functions for a person who sits down to sing a song with it?* A scheduled scan refreshes it every seven hours (see [§9](#9-how-this-page-is-maintained)). Each scan rewrites the current-state sections in place and appends one entry to the [direction log](#10-direction-log). Gate states remain owned by [STATUS](../STATUS.md), and the order of milestone work by the [completion plan](2026-10-18-project-completion-plan.md). This page translates both into "can it function yet, and what is the next smallest step?"

Last full scan: **2026-10-20 05:38 KST**, dedicated clone at `origin/master` `6d070638`, clean tree before scan.

## 1. Verdict

Project SEAM builds and passes this scan's configured fast tier; the baseline run verified an editor launch. The host-local listening comparison remains excluded (see F0). **On a clean Mac it cannot make a sound**. No singer ships with the application and none is installed by default, so a new project opens with the singer card showing *MISSING — Voicebank unavailable* and every expression control showing *UNAVAILABLE*. Sound exists today only inside prepared engineering setups: the observation kit's procedural diagnostic singer, the synthetic fixture bank used by tests, and banks a developer installs by hand from the command line. No recorded Japanese voice exists in any of them.

The shortest route to a functioning product therefore runs through **getting one real Japanese singer onto the machine**. The editor, renderer, project lifecycle and export pipeline do not need to be built first; they are already built. Section 5 lists engineering work that can start today without waiting for owner decisions. The most valuable item is an engineering-only Japanese sample bank made from a corpus already on disk (PJS 1.1), which the completion plan's D1 analysis did not consider.

## 2. Functional ladder

Each rung needs the one before it. "Evidence" names this scan's results, a dated earlier run, or the code that decides the outcome.

| Rung | Meaning | State | Evidence |
|---|---|---|---|
| F0 Builds and tests | Release build and the scan's configured fast tier pass | **Yes, within scan scope** | `cmake --build build/release -j6` exit 0 in 28.4 s; `ctest --preset release -LE '^slow$' -E '^seam_listening_packet_comparison$' -j6` exit 0, 195/195 passed in 35.28 s; 227 registered, with native GUI tests off and optional ONNX SDK unset in `build/release/CMakeCache.txt`; the excluded comparison needs host-local Downloads audio |
| F1 Launches and edits | The standalone opens a responsive editor | **Yes** | Baseline launch at `8500aec7` (2026-10-19 08:05 KST): `build/release/Project SEAM.app` against a fresh support root with `--evidence-dir`; the SING shell rendered with the empty-score prompt |
| F2 Sound on a clean Mac | A new user hears anything at all | **No** | Same baseline run reported `voicebank_resolved=false`, `render_state=idle`, `transport_available=false`; the bundle contains no voicebank; `~/Library/Application Support/ProjectSEAM/Voicebanks` and `Singers` do not exist on the development Mac |
| F3 Japanese song in a recorded voice | Japanese lyrics sung by a sample-concatenative bank made from human recordings | **No** | No Japanese human-recorded bank exists; the 1,469 female downloads contain no Japanese CV syllables ([STATUS](../STATUS.md)); PJS 1.1 is on disk but not converted |
| F4 Musically acceptable to the owner | The owner hears a render and judges it usable | **Unknown** | No verdict recorded on any song; [observation form](../implementation/NATIVE_OBSERVATION_FORM_2026-10-12.json) still `NOT_OBSERVED` in every step, `largerObstacle: UNDECIDED` |
| F5 Unaided Usable Alpha journey | UA-001–UA-020 from Finder | **No** | 0/20 recorded, all `NOT_RUN` ([record](../product/usable-alpha-acceptance.json)) |
| F6 Works in a DAW | CLAP/VST3/AUv2 in REAPER, Bitwig, Logic | **No** | Only the first-party `seam_clap_host` has loaded the plug-ins; G2–G5 all `passed: false` |
| F7 Shareable | Signed, notarized, installable by someone else | **No** | No Developer ID run; External Beta 0/9 |

F0 passes within the configured scan scope; the excluded listening comparison remains unverified here. F2 remains the first missing product capability, and F3 and F4 follow directly from it. The work that matters most now sits at F2–F4. F6 and F7 are real but later.

## 3. What a person can do today, journey by journey

**Launch and edit.** Works. `NativeEditorApp::initialize` shows the EULA modal on a first launch (`platform::requestEulaAcceptance`), then opens an Untitled project with one track. Notes, Japanese lyrics, phoneme/unit/seam lanes, pitch points, expression curves, vibrato, undo/redo and the accessibility tree are implemented in `libs/seam-native-ui` and covered by journey tests such as `tests/test_sing_shell_journey.cpp`.

**Hear the song.** Blocked on a clean machine. `AuthoringSession::bindInitialVoicebank` (`libs/seam-standalone/src/authoring_session.cpp`) finds no candidate and returns *No installed or development voicebank is available*. In release mode `bindFirstAvailableVoicebank` is off anyway (`production_configuration.cpp`), so the user must choose a singer, and the chooser has nothing to offer. The diagnostic card then shows `BANK_MISSING` with relink wording. This is very probably the earlier "BANK_MISSING came back" observation: it is the default state whenever no trusted bank is installed, not a regression.

**Get a singer onto the machine.** There are three working paths, none of them a Finder-only first-run flow:

| Path | Works from Finder? | What it produces | Code |
|---|---|---|---|
| Voicebank Studio → Designer → publish → install | Yes | A procedural (source-filter) singer, installed under `Singers/`, labelled *NOT QUALITY-APPROVED* | `apps/seam-voicebank-studio-native/studio_app.cpp` `installPublishedSinger` |
| `seam_bank_tool install PACKAGE ROOT --public-key KEY` or `seam_voicebank_cli install-candidate` | Install is CLI; the result is then selectable from Finder | A sample bank whose receipt records `signerTrusted: true`; the catalog turns that into `TrustedInstalled` | `libs/seam-distribution/src/installer.cpp`, `libs/seam-voicebank/src/catalog.cpp` |
| In-app *Install voicebank* | **No** for a self-signed bank | Refused | `voicebank_installer_service.cpp` sets `requireTrustedSigner = true`; the trusted-key list comes only from `--trusted-voicebank-key` (`apps/seam-editor-native/main.cpp`), which a Finder launch never passes |

The procedural path makes sound, but its quality is unqualified: the dry procedural listening packet fails its Q1 check ([entrypoints](../implementation/CURRENT_ENGINEERING_ENTRYPOINTS.md)). UA-010 also explicitly requires *production sample-concatenative audio*, so a procedural singer cannot satisfy Usable Alpha.

**Make a recorded bank.** The producer chain exists end to end: `new-producer-workspace` → `register-source` → takes → inspection → `create-sample-draft` → `prepare-sample-review` → `review-sample` → `publish-sample` → `package-candidate` → `install-candidate`. Fixtures cover it (`tests/test_studio_generation_bank_journey.cpp`), but it has never run on real human Japanese takes. Review enforces a distinct reviewer identity (`review_transition.cpp`), so the formal chain cannot finish without a second person (D5).

**Save, reopen, recover, relink, export.** Implemented and tested with fixture banks: project writer v21, autosave recovery, exact ID/version/hash relink, transactional master and stem export with receipts. None of it has been exercised by a person with a real bank.

**Use it in a DAW.** CLAP player and editor load only in `seam_clap_host`. VST3 and AUv2 have no validator or host run.

**Give it to someone else.** Not possible yet: no signing identity, no notarization, no clean-machine install, no released bank.

## 4. Gaps to "functions", in order

### G1 — No singer on a clean install (blocks F2)

*Evidence:* the fresh-root launch above; the app bundle's `Contents/Resources` holds UI art, fonts, manual and SBOM but no `.seambank` or `.seamsinger`; `defaultVoicebankSearchRoots` and `defaultProceduralSearchRoots` point only at the user's Application Support folders.

*Why it matters:* every later rung, including the owner's own listening verdict, needs a singer. The empty state is also misleading: a never-bound new project reads *MISSING* and *Choose or relink a trusted voicebank*, which suggests something broke. In fact nothing has been installed yet.

### G2 — No Japanese recorded voice (blocks F3, UA-004/005/010)

*Evidence:* [STATUS](../STATUS.md) blocker 1. PJS 1.1, however, is a Japanese singing corpus with a CC BY-SA 4.0 licence (one male singer, 100 songs, 48 kHz mono PCM24). It is already downloaded with its pinned manual relabel at `out/evidence/internet-vocal-sources-2026-10-12/` (`PJS_corpus_ver1.1.zip`, `pjs-manual-labels-cc08bead.zip`), and the 2026-10-12 [intake](../implementation/INTERNET_SAMPLE_SOURCE_INTAKE_2026-10-12.md) already designed its conversion (step S2, `tools/voicebank_source_intake/pjs_cohort.py`, not implemented). The completion plan's D1 table rejects public-licence material because the female sources lack Japanese syllables; that reason does not apply to PJS. The WAV reader already accepts 24-bit PCM (`libs/seam-voicebank/src/wav.cpp`), and `seam_bank_tool pack` packages any valid bank directory.

*Limits:* male voice; share-alike licence, which conflicts with the EULA's ban on bank redistribution, so a PJS bank must never enter a release package. Whether it may count as Usable Alpha's "rights-cleared production bank" is the owner's D2 call. As an engineering bank it closes F3 for engineering purposes and lets the owner reach F4 weeks earlier.

### G3 — Trust onboarding for a Finder user (affects F2 and UA-002)

*Evidence:* section 3 table. A user who builds a bank in Studio and double-clicks the package, or uses the in-app installer, is refused, because the Finder-launched app trusts no key. The working route (CLI install with the public key) is invisible from the app and absent from the [quick start](../manual/QUICK_START.md).

*Why it matters:* the M1 recording route ends with `install-candidate` on the command line. UA-001 says "from Finder", so the install step must either stay a documented pre-session step or gain an in-app, user-confirmed way to trust a local key. The second option is a product security decision; this page only flags it.

### G4 — No listening verdict and no observation (blocks F4)

*Evidence:* the observation form is unfilled; an observation-kit instance of the app (`out/observation/native-song-2026-10-12/apps/Project SEAM.app`) is still running, started on 2026-10-12; the 08:05 baseline counted six rendered PCM files in its cache. The kit sings with the procedural diagnostic singer `seam-pilot-01-voiced-stop-diagnostic` (`NOT_QUALIFIED`).

*Why it matters:* the project has no musical feedback at all. D9 costs about thirty minutes and needs no other decision. It decides whether the first repair block goes to the editor workflow or to sound.

### G5 — Unaided journey, DAW hosts, signing, Windows (block F5–F7)

Unchanged from [STATUS](../STATUS.md) and the [plan](2026-10-18-project-completion-plan.md) M1–M3. They need G1–G4 first, or a person, licence, account or machine (D8).

## 5. Next actions

Ranked by how much they move the ladder per day of effort. "Owner" means the owner must act; everything else is engineering work that needs no decision.

| # | Action | Rung / gate | Who | Estimate | Done when |
|---|---|---|---|---|---|
| 1 | **Run the D9 observation session** with the already-open kit and fill in the form | F4, plan D9/M1-1 | Owner | 30 min | Form has three step results, three ranked problems and `largerObstacle` set |
| 2 | **Build an engineering Japanese sample bank from PJS 1.1.** Implement `pjs_cohort.py` (intake S2: parse the HTK labels in 100 ns units, check them against WAV frame counts, map phones explicitly, cut CV/VC/VV/sustain units with source and label hashes). Emit a manifest v3 bank, then `seam_bank_tool keygen` / `pack` / `install --public-key` into a scratch support root. Keep it out of every release path and keep the attribution. | F2, F3 (engineering), UA-005/007/010 pre-check, G-M1 early signal | Engineering | 2–4 days | The 30-second observation song, opened in the standalone from Finder, renders and plays with `voicebank_resolved=true`; an exported WAV and its receipt exist under `out/` |
| 3 | **Owner listens to item 2's render** and records *usable for evaluation / not usable* plus the top three problems | F4, G-M1 | Owner | 15 min | Verdict recorded in the plan's M1-4 slot as an *engineering* listening note, not a UA row |
| 4 | **Honest empty state.** When the catalog has no selectable singer, show *No singer installed* with two actions: *Open Voicebank Studio* and *Install voicebank…*. Drop *MISSING* and the relink wording for a project that was never bound. | F2 friction, UA-001/002 | Engineering | 0.5–1 day | A fresh-root capture shows the new state; journey tests still pass |
| 5 | **Document the trusted install step** in the quick start and user manual (CLI install with public key, then select from Finder) | G3, UA-002 | Engineering | 2 h | Manual text matches the code path above |
| 6 | Decide D1, D2 and D5 using items 1–3 as input; the plan's D1 table should add PJS as option E (engineering-only, male, CC BY-SA) | M1 entry | Owner | — | Decisions written in plan Appendix C |
| 7 | Decide whether the app should let a user trust a local signing key from Settings (G3) | UA journey from Finder | Owner | — | Yes → small engineering unit; no → item 5 is final |

Items 2, 4 and 5 can run in parallel with the owner's items 1, 3 and 6. Item 2 is the one that changes the project's situation: it is the first time a human-recorded Japanese voice would sing through the shipped renderer.

## 6. What engineering should not do yet

Do not start M2 release engineering (signing scripts, host matrix, soak runs) before F4 has a verdict. Do not expand the Japanese inventory toward the 2,052-prompt Beta set before a small bank has been heard. Do not tune the procedural singer as a substitute for a sample bank; UA-010 excludes it. Do not flip U45 or edit gate records outside their verifiers.

## 7. Owner decisions and what each unblocks

| Decision | Unblocks | Status on 2026-10-19 |
|---|---|---|
| D1 Alpha bank route | M1 recording; item 2 does not need it | Open |
| D2 Meaning of "rights-cleared" for Alpha | Whether a PJS or self-recorded bank can back UA rows | Open |
| D5 Producer and distinct reviewer | Formal `review-sample` step | Open |
| D6 macOS-only External Beta 1 | M2 planning, E-05 | Open, packet measured |
| D7 Working protocol and branch policy | E-15 (draft `7d10b021` on `codex/d7-branch-policy`, unmerged) | Open |
| D3, D4, D8 | M2 | Open |
| D9 Observation | Repair ranking | Not done |

## 8. Risks

- **The first real render may disappoint.** That is the point of doing it early: G-M1 exists to stop Beta spending if sample-concatenative singing in this engine is not good enough. Item 2 surfaces it in days rather than after a contract recording.
- **A PJS bank could leak into a release.** Mitigate by installing only into a scratch support root, keeping it out of `docs/voicebank` dossiers and package inventories, and naming it `engineering.pjs.*`.
- **Male voice versus the intended character.** It tests the engine and workflow, not the product voice. The product voice still comes from D1/D3.
- **Scheduled scans could churn documents.** Each scan changes this page only when evidence changes, and appends at most one log entry.

## 9. How this page is maintained

A macOS launch agent (`~/Library/LaunchAgents/com.projectseam.readiness-scan.plist`) runs every seven hours. It starts an installed copy of [tools/readiness_scan/bootstrap.sh](../../tools/readiness_scan/bootstrap.sh) at `~/.codex/readiness-scan/bootstrap.sh`. The bootstrap works in a separate clone at `~/.codex/worktrees/readiness-scan`, kept outside `~/Downloads` because macOS privacy protection blocks launch agents from reading that folder, so the main checkout is never touched. It fast-forwards the clone to `origin/master` and hands over to [tools/readiness_scan/run_scan.sh](../../tools/readiness_scan/run_scan.sh), which runs `codex exec` with the contract in [tools/readiness_scan/PROMPT.md](../../tools/readiness_scan/PROMPT.md) under a two-hour limit. Each scan:

1. fast-forwards to `origin/master`;
2. reads the commits since the last log entry;
3. rebuilds, runs the fast tier and the gate verifiers listed in [STATUS](../STATUS.md#how-to-refresh-this-page);
4. updates sections 1–8 only where evidence changed and appends one direction-log entry with at most one ranking change;
5. commits this page alone and pushes it, then verifies the remote hash. If anything fails it writes a log and stops without pushing.

Logs and each run's short summary go to `~/Library/Logs/ProjectSEAM/readiness-scan/`, and the summary's first line appears as a macOS notification. To recreate the clone, run `git clone <main checkout> ~/.codex/worktrees/readiness-scan` and then `git -C ~/.codex/worktrees/readiness-scan remote set-url origin https://github.com/julymeltdown/vst-voice.git`. To stop the schedule: `launchctl bootout gui/$(id -u)/com.projectseam.readiness-scan`.

## 10. Direction log

Newest last. One entry per scan: date, commit, what changed in evidence, and the direction adjustment, if any.

- **2026-10-19 08:05 KST, `8500aec7` — baseline.** First functional scan. Evidence: build exit 0, fast tier 209/209, gate verifiers unchanged (UA contract valid with 0/20 rows, voicebank gate exit 3 with 12 categories, G2–G5 exit 3). New findings: a clean install has no singer (F2 fails), in-app install refuses self-signed banks from a Finder launch, PJS 1.1 is a usable Japanese engineering source the plan overlooked, and D9 is still unrecorded although the kit has been open since 2026-10-12. Direction: put an engineering Japanese bank (item 2) and the owner's observation (item 1) ahead of all remaining M0 cleanup and every M2 task.

- **2026-10-19 08:26 KST, `ee35b905` — scan limitation; no product change.** New commits only add readiness documentation/automation. Build exit 0 (230.5 s); fast tier exit 8, 195/196 in 145.66 s, with `seam_listening_packet_comparison` timing out at 120 s. Timestamps and source hashes checked: its Python files match HEAD; the retained WAV predates this build. A 25 s diagnostic also stalled in `compare_artifacts` (`scripts/compare_listening_packets.py:310`), called by `test_the_retained_d1_packet_compares_to_its_own_rerender`, opening audio under `~/Downloads/seam-listening-artifacts/2026-09-15-d1-repeat/`; no stale linked binary explains it. This clone registers 227 tests with native GUI tests off and no optional ONNX SDK, so STATUS/plan's historical 241-test runs and the baseline's 209-test fast-tier pass do not describe this scan. Gates unchanged: UA contract exit 0/PASS, acceptance BLOCKED 0/20; voicebank exit 3/`passed: false`, 52 errors/12 categories; G2, G3, G4 and G5 each exit 3/`passed: false`. D9 remains unobserved, Appendix C unfilled, and `pjs_cohort.py` and the default installed bank/singer roots absent. Fresh-root launch not repeated: none of the trigger paths changed. Direction: **no change** to section 5; resolve the scan's host-local audio dependency before claiming a passing fast tier, while D9 and the engineering Japanese bank remain first.

- **2026-10-19 15:30 KST, `1e25a512` — configured scan passes; no product change.** New commits record the previous scan and exclude `seam_listening_packet_comparison` from unattended runs because macOS privacy protection blocks its fixed Downloads audio. Build exit 0 (28.4 s); configured fast tier exit 0, 195/195 in 35.28 s, versus the previous 195/196 with that comparison timing out. Registration remains 227, native GUI tests off and optional ONNX SDK unset; this is narrower engineering evidence than STATUS/plan's historical 241/209 counts, not acceptance or a fix for the excluded test. Gates unchanged: UA contract exit 0/PASS, acceptance BLOCKED 0/20; voicebank exit 3/`passed: false`, 52 errors/12 categories; G2, G3, G4 and G5 each exit 3/`passed: false`. D9 remains unobserved, Appendix C unfilled, and `pjs_cohort.py` and the default installed bank/singer roots absent. Fresh-root launch not repeated: none of the trigger paths changed. Direction: **no change** to section 5; no product capability or owner evidence changed, so D9 and the engineering Japanese bank remain first.

- **2026-10-19 22:34 KST, `5d385dc2` — no change.** Only the previous scan's page update landed. Build exit 0 (25.78 s); configured fast tier exit 0, 195/195 in 32.16 s (same count as the previous scan; 227 registered, GUI off, ONNX unset, host-local listening comparison excluded). Gates unchanged: UA contract exit 0/PASS, acceptance BLOCKED 0/20; voicebank exit 3/`passed: false`, 52 errors/12 categories; G2, G3, G4 and G5 each exit 3/`passed: false`. D9 remains unobserved, Appendix C unfilled, and `pjs_cohort.py` and default installed bank/singer roots absent. No launch-trigger path changed. Direction: **no change**; D9 and the engineering Japanese bank remain first. These checks remain engineering evidence, not acceptance.

- **2026-10-20 05:38 KST, `6d070638` — no change.** Only the previous scan's page update landed. Build exit 0 (25.44 s); configured fast tier exit 0, 195/195 in 33.90 s (same count as the previous scan; 227 registered, GUI off, ONNX unset, host-local listening comparison excluded). Gates unchanged: UA contract exit 0/PASS, acceptance BLOCKED 0/20; voicebank exit 3/`passed: false`, 52 errors/12 categories; G2, G3, G4 and G5 each exit 3/`passed: false`. D9 remains unobserved, Appendix C unfilled, and `pjs_cohort.py` and default installed bank/singer roots absent. No launch-trigger path changed. Direction: **no change**; D9 and the engineering Japanese bank remain first. These checks remain engineering evidence, not acceptance.
