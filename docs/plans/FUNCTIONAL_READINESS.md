# Project SEAM — functional readiness

**Living document.** It answers one question: *what still has to happen before Project SEAM functions for a person who sits down to sing a song with it?* A scheduled scan refreshes it every seven hours (see [§9](#9-how-this-page-is-maintained)). Each scan rewrites the current-state sections in place and appends one entry to the [direction log](#10-direction-log). Gate states remain owned by [STATUS](../STATUS.md), and the order of milestone work by the [completion plan](2026-10-18-project-completion-plan.md). This page translates both into "can it function yet, and what is the next smallest step?"

Last full scan: **2026-10-20 20:03 KST**, dedicated clone at `origin/master` `066cda5d`, clean tree before scan. PJS render measurements below are retained from the manual updates; this scan rechecked the installed manifest and receipt.

## 1. Verdict

Project SEAM now sings Japanese in a human-recorded voice, but only with an engineering bank that can never ship. On 2026-10-20 the PJS 1.1 corpus became an installed, signed sample bank (`engineering.pjs.male.01`, 1,696 units, male voice, CC BY-SA 4.0). With it installed, `Project SEAM.app` opens the 30-second observation song with the singer card at *READY* and reports `voicebank_resolved=true`, `render_state=ready`, `transport_available=true`. The same project exports a 30-second master in which every note is voiced and 29 of 30 notes sit within 50 cents of the score. No owner listening verdict is recorded.

**A clean install still cannot make a sound,** because no singer ships with the application; since 2026-10-20 it says so honestly (*No singer selected*, with install guidance) instead of reporting a missing bank. The next step that changes the project's direction is the owner's listening verdict on the PJS render (item 1 in section 5), because it is the first evidence of whether sample-concatenative singing in this engine is good enough. The owner's observation session follows.

## 2. Functional ladder

Each rung needs the one before it. "Evidence" names this scan's results, a dated earlier run, or the code that decides the outcome.

| Rung | Meaning | State | Evidence |
|---|---|---|---|
| F0 Builds and tests | Release build and the scan's configured fast tier pass | **Yes, within scan scope** | `cmake --build build/release -j6` exit 0 in 871.0 s; `ctest --preset release -LE '^slow$' -E '^seam_listening_packet_comparison$' -j6` exit 0, 195/195 passed in 137.21 s; 227 registered, with native GUI tests off and optional ONNX SDK unset in `build/release/CMakeCache.txt`; the excluded comparison needs host-local Downloads audio |
| F1 Launches and edits | The standalone opens a responsive editor | **Yes** | Baseline launch at `8500aec7` (2026-10-19 08:05 KST): `build/release/Project SEAM.app` against a fresh support root with `--evidence-dir`; the SING shell rendered with the empty-score prompt |
| F2 Sound on a clean Mac | A new user hears anything at all | **No on a clean install; yes once a bank is installed** | Baseline fresh-root run reported `voicebank_resolved=false`, `render_state=idle`; the bundle contains no voicebank. With the PJS engineering bank installed under a fresh support root, the standalone opened the observation project and reported `voicebank_resolved=true`, `render_state=ready`, `render_diagnostic=Production multi-track routing render completed`, `transport_available=true` (2026-10-20) |
| F3 Japanese song in a recorded voice | Japanese lyrics sung by a sample-concatenative bank made from human recordings | **Engineering yes; product no** | `tools/voicebank_source_intake/pjs_engineering_bank.py` built `engineering.pjs.male.01` (1,696 units, 111 aliases, MIDI 44–79; `seam_voicebank_cli validate` 0 errors, 34 warnings). `seam_voicebank_cli export-installed-bank-project` rendered the observation song: 30.0 s, peak 0.348, not clipped, every note voiced, 29/30 notes within 50 cents of the score by the engine's pitch extractor, byte-identical on re-export. Male and CC BY-SA: not a product bank |
| F4 Musically acceptable to the owner | The owner hears a render and judges it usable | **Unknown** | No verdict recorded on any song; [observation form](../implementation/NATIVE_OBSERVATION_FORM_2026-10-12.json) still `NOT_OBSERVED` in every step, `largerObstacle: UNDECIDED` |
| F5 Unaided Usable Alpha journey | UA-001–UA-020 from Finder | **No** | 0/20 recorded, all `NOT_RUN` ([record](../product/usable-alpha-acceptance.json)) |
| F6 Works in a DAW | CLAP/VST3/AUv2 in REAPER, Bitwig, Logic | **No** | Only the first-party `seam_clap_host` has loaded the plug-ins; G2–G5 all `passed: false` |
| F7 Shareable | Signed, notarized, installable by someone else | **No** | No Developer ID run; External Beta 0/9 |

F0 passes within the configured scan scope; the excluded listening comparison remains unverified here. F4 is now the rung that decides direction: the engine can sing, and the question is whether it sings well enough. F2 for a clean install still needs a shippable bank (D1/D3); the honest empty state is implemented. F6 and F7 are real but later.

## 3. What a person can do today, journey by journey

**Launch and edit.** Works. `NativeEditorApp::initialize` shows the EULA modal on a first launch (`platform::requestEulaAcceptance`), then opens an Untitled project with one track. Notes, Japanese lyrics, phoneme/unit/seam lanes, pitch points, expression curves, vibrato, undo/redo and the accessibility tree are implemented in `libs/seam-native-ui` and covered by journey tests such as `tests/test_sing_shell_journey.cpp`.

**Hear the song.** Blocked on a clean machine: release configuration disables `bindFirstAvailableVoicebank` (`libs/seam-standalone/src/production_configuration.cpp`), and the user must install and choose a singer. An unbound track now reports `SINGER_NOT_SELECTED` (`renderDiagnosticCode` in `libs/seam-authoring-runtime/src/authoring_runtime.cpp`); `resolveVoiceIdentity` and `presentDiagnostic` in `libs/seam-native-ui/src/` show *No singer selected* with install/create guidance. `BANK_MISSING` still applies to an unavailable or mismatched saved bank pin. The installed PJS bank enables the engineering route in section 2.

**Get a singer onto the machine.** There are three routes; sample-bank trust still needs setup before a Finder-only session:

| Path | Works from Finder? | What it produces | Code |
|---|---|---|---|
| Voicebank Studio → Designer → publish → install | Yes | A procedural (source-filter) singer, installed under `Singers/`, labelled *NOT QUALITY-APPROVED* | `apps/seam-voicebank-studio-native/studio_app.cpp` `installPublishedSinger` |
| `seam_bank_tool install PACKAGE ROOT --public-key KEY` or `seam_voicebank_cli install-candidate` | Install is CLI; the result is then selectable from Finder | A sample bank whose receipt records `signerTrusted: true`; the catalog turns that into `TrustedInstalled` | `libs/seam-distribution/src/installer.cpp`, `libs/seam-voicebank/src/catalog.cpp` |
| In-app *Install voicebank* | **No** for a self-signed bank | Refused | `voicebank_installer_service.cpp` sets `requireTrustedSigner = true`; the trusted-key list comes only from `--trusted-voicebank-key` (`apps/seam-editor-native/main.cpp`), which a Finder launch never passes |

The procedural path makes sound, but its quality is unqualified: the dry procedural listening packet fails its Q1 check ([entrypoints](../implementation/CURRENT_ENGINEERING_ENTRYPOINTS.md)). UA-010 also explicitly requires *production sample-concatenative audio*, so a procedural singer cannot satisfy Usable Alpha.

**Make a recorded bank.** The producer chain exists end to end: `new-producer-workspace` → `register-source` → takes → inspection → `create-sample-draft` → `prepare-sample-review` → `review-sample` → `publish-sample` → `package-candidate` → `install-candidate`. Fixtures cover it (`tests/test_studio_generation_bank_journey.cpp`), but it has never run on real human Japanese takes. Review enforces a distinct reviewer identity (`review_transition.cpp`), so the formal chain cannot finish without a second person (D5).

**Save, reopen, recover, relink, export.** Implemented and tested with fixture banks: project writer v21, autosave recovery, exact ID/version/hash relink, transactional master and stem export with receipts. The PJS result in section 2 also exercises installed-bank master export. An unaided owner journey remains unrecorded.

**Use it in a DAW.** CLAP player and editor load only in `seam_clap_host`. VST3 and AUv2 have no validator or host run.

**Give it to someone else.** Not possible yet: no signing identity, no notarization, no clean-machine install, no released bank.

## 4. Gaps to "functions", in order

### G1 — No singer on a clean install (blocks F2)

*Evidence:* the fresh-root launch above; the app bundle's `Contents/Resources` holds UI art, fonts, manual and SBOM but no `.seambank` or `.seamsinger`; `defaultVoicebankSearchRoots` and `defaultProceduralSearchRoots` point only at the user's Application Support folders.

*Why it matters:* every later rung, including the owner's own listening verdict, needs a singer. The misleading empty state is fixed: `resolveVoiceIdentity` distinguishes *No singer* from a missing saved bank, and `presentDiagnostic` provides install/create guidance (covered by `tests/test_native_ui.cpp` and `tests/test_native_runtime.cpp`). Singer availability on a clean install remains open.

### G2 — No production Japanese recorded voice (blocks product F3, UA-004/005/010)

*Evidence:* [STATUS](../STATUS.md) blocker 1. PJS 1.1, however, is a Japanese singing corpus with a CC BY-SA 4.0 licence (one male singer, 100 songs, 48 kHz mono PCM24). It is already downloaded with its pinned manual relabel at `out/evidence/internet-vocal-sources-2026-10-12/` (`PJS_corpus_ver1.1.zip`, `pjs-manual-labels-cc08bead.zip`), and the 2026-10-12 [intake](../implementation/INTERNET_SAMPLE_SOURCE_INTAKE_2026-10-12.md) already designed its conversion (step S2, `tools/voicebank_source_intake/pjs_cohort.py`, not implemented). The completion plan's D1 table rejects public-licence material because the female sources lack Japanese syllables; that reason does not apply to PJS. The WAV reader already accepts 24-bit PCM (`libs/seam-voicebank/src/wav.cpp`), and `seam_bank_tool pack` packages any valid bank directory.

*Done on 2026-10-20:* the conversion is `tools/voicebank_source_intake/pjs_engineering_bank.py`; `pjs_cohort.py` itself remains absent. This scan read `~/Library/Application Support/ProjectSEAM/Voicebanks/engineering.pjs.male.01/0.1.0-engineering/manifest.json` (1,696 units, `language: ja`) and `install-receipt.json` (`signerTrusted: true`). The earlier manual render measurements remain engineering evidence (see the direction log).

*Limits:* male voice; share-alike licence, which conflicts with the EULA's ban on bank redistribution, so a PJS bank must never enter a release package. Whether it may count as Usable Alpha's "rights-cleared production bank" is the owner's D2 call. As an engineering bank it closes F3 for engineering purposes and lets the owner reach F4 weeks earlier.

### G3 — Trust onboarding for a Finder user (affects F2 and UA-002)

*Evidence:* section 3 table. A user who builds a bank in Studio and double-clicks the package, or uses the in-app installer, is refused, because the Finder-launched app trusts no key. The working route (CLI install with the public key) is documented in [README, Giving the editor a singer](../../README.md#giving-the-editor-a-singer), but remains absent from the app and the [quick start](../manual/QUICK_START.md).

*Why it matters:* the M1 recording route ends with `install-candidate` on the command line. UA-001 says "from Finder", so the install step must either stay a documented pre-session step or gain an in-app, user-confirmed way to trust a local key. The second option is a product security decision; this page only flags it.

### G4 — No listening verdict and no observation (blocks F4)

*Evidence:* the observation form still has seven `NOT_OBSERVED` steps, no ranked problems and `largerObstacle: UNDECIDED`. The 08:05 baseline observed a running kit instance (`out/observation/native-song-2026-10-12/apps/Project SEAM.app`) and six rendered PCM files in its cache. The kit uses the procedural diagnostic singer `seam-pilot-01-voiced-stop-diagnostic` (`NOT_QUALIFIED`); the PJS render has no recorded owner verdict either.

*Why it matters:* the project has no musical feedback at all. D9 costs about thirty minutes and needs no other decision. It decides whether the first repair block goes to the editor workflow or to sound.

### G5 — Unaided journey, DAW hosts, signing, Windows (block F5–F7)

Unchanged from [STATUS](../STATUS.md) and the [plan](2026-10-18-project-completion-plan.md) M1–M3. They need G1–G4 first, or a person, licence, account or machine (D8).

## 5. Next actions

Ranked by how much they move the ladder per day of effort. "Owner" means the owner must act; everything else is engineering work that needs no decision.

| # | Action | Rung / gate | Who | Estimate | Done when |
|---|---|---|---|---|---|
| 1 | **Listen to the PJS render** — `out/engineering-banks/exports/observation-30s-pjs.wav`, or open `out/engineering-banks/projects/observation-30s-pjs.seam` in `Project SEAM.app` from Finder (the bank is installed in the default Voicebanks folder) — and record *usable for evaluation / not usable* plus the top three problems | F4, G-M1 | Owner | 15 min | Verdict recorded in the plan's M1-4 slot as an *engineering* listening note, not a UA row |
| 2 | **Run the D9 observation session** with the kit and fill in the form | F4, plan D9/M1-1 | Owner | 30 min | Form has three step results, three ranked problems and `largerObstacle` set |
| 3 | **Rehearse UA-001–UA-019 with the PJS bank** (first pass 2026-10-20: four defects fixed; relink, audible transport, UA-020 and a crash-recovery offer remain; see the direction log) in the standalone (new project, Japanese lyrics, phoneme/unit/pitch/seam edits, save, reopen, autosave recovery, relink, master and stem export) and fix the defects found before the owner's unaided run | F5 pre-check, UA-001–019 | Engineering | 1–3 days | Each step either works or has a filed fix; recorded as engineering rehearsal, never as UA rows |
| 4 | **Repair loop on the PJS render**, driven by item 1's top three problems (timing, seams, pitch, unit choice), checked on the same song | F4, G-M1 | Engineering | ≤ 2 weeks | Each fix has a before/after criterion on the same song; the owner re-listens |
| 5 | **Cold voicebank scan cost for plug-in hosts.** Repeat scans reuse digests (380 → 72 ms for the PJS bank), and SHA-256 now uses the ARMv8 instructions (about 2.4 GB/s; the first scan of the PJS bank fell from ~0.8 s to ~0.56 s). The first scan per process still hashes every audio file, and every scan parses the whole manifest, so the remaining cost is unmeasured for a Beta-size bank | F6 | Engineering | 1 day | Measured first-scan and repeat-scan times for a Beta-size bank, each within a stated budget |
| 6 | Decide D1, D2 and D5 using items 1–2 as input; the plan's D1 table should add PJS as option E (engineering-only, male, CC BY-SA) | M1 entry | Owner | — | Decisions written in plan Appendix C |
| 7 | Decide whether the app should let a user trust a local signing key from Settings (G3) | UA journey from Finder | Owner | — | Yes → small engineering unit; no → the README's developer route stays the only one |

Items 3 and 5 can run in parallel with the owner's items 1, 2 and 6; item 4 starts when item 1 names problems. Done and retired on 2026-10-20: the PJS bank (former item 2), the honest empty state (former item 3, `9b71c5ab`) and the developer install route (former item 4, README *Giving the editor a singer*).

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

- **The first real render may disappoint.** That is the point of doing it early: G-M1 exists to stop Beta spending if sample-concatenative singing in this engine is not good enough. Item 1 surfaces it before a contract recording.
- **A PJS bank could leak into a release.** It is installed in the default user Voicebanks folder. Keep `engineering.pjs.*` out of `docs/voicebank` dossiers and release package inventories; use scratch support roots for further experiments.
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

- **2026-10-20 12:42 KST, `7ca841f3` — no change.** Only the previous scan's page update landed. Build exit 0 (29.42 s); configured fast tier exit 0, 195/195 in 37.10 s (same count as the previous scan; 227 registered, GUI off, ONNX unset, host-local listening comparison excluded). Gates unchanged: UA contract exit 0/PASS, acceptance BLOCKED 0/20; voicebank exit 3/`passed: false`, 52 errors/12 categories; G2, G3, G4 and G5 each exit 3/`passed: false`. D9 remains unobserved, Appendix C unfilled, and `pjs_cohort.py` and default installed bank/singer roots absent. No launch-trigger path changed. Direction: **no change**; no product capability or owner evidence changed, so D9 and the engineering Japanese bank remain first. These checks remain engineering evidence, not acceptance; STATUS/plan's historical 241/209 test counts describe the main checkout's broader scope.

- **2026-10-20 19:10 KST, manual update after the PJS engineering bank — direction change.** Engineering item 2 is done. `tools/voicebank_source_intake/pjs_engineering_bank.py` converted the full PJS 1.1 corpus (1,696 units; 928 spans too short to loop, 283 unstable in pitch and 1 clipped rejected); `seam_voicebank_cli validate` reports 0 errors and 34 warnings. It was packed (1,700 entries) and installed with a local key; the receipt records `signerTrusted: true`. The new `seam_voicebank_cli export-installed-bank-project` exported the observation song (30.0 s, peak 0.348, every note voiced, 29/30 within 50 cents, byte-identical on re-export), and the standalone opened it with `voicebank_resolved=true` and `render_state=ready`. Found on the way: the manifest format documents showed `"language": "japanese"` while the codec accepts only `"ja"` (fixed), and Python's `wave` module and several PJS files omit the RIFF pad byte that the strict native reader requires (the tool writes canonical RIFF). Direction: the owner's listening verdict on this render moves to item 1, ahead of D9; a bounded repair loop on the same song becomes item 5. A clean install still has no singer.

- **2026-10-20 21:40 KST, manual update — two items retired, one added.** `bf5b3ce0`: catalogue scans reuse unchanged audio digests (fingerprint includes the status-change time); with the PJS bank installed the CLAP matrix admission test went from a 120 s timeout to 36 s. `9b71c5ab`: a track with no singer now reports `SINGER_NOT_SELECTED` (warning, *Install voicebank* first) and a *No singer* card instead of `BANK_MISSING`; full suite 241/241 and a fresh-root capture confirm it. README now documents how a developer gives the editor a singer; the user manual stays aimed at Beta testers with a product-signed bank. Direction: an engineering rehearsal of UA-001–019 with the PJS bank becomes item 3, so the owner's unaided run meets fewer defects; cold scan cost for plug-in hosts is item 5.

- **2026-10-20 22:10 KST, `97eaf4f3` — item 5 narrowed.** SHA-256 uses the ARMv8 instructions on Apple Silicon: a 64 MiB buffer hashes in 27.6 ms instead of 195 ms, and the first PJS bank scan fell from ~0.8 s to ~0.56 s. Full suite 241/241. Manifest parsing on every scan and a Beta-size measurement remain in item 5; no change to order.

- **2026-10-20 20:03 KST, `066cda5d` — evidence reconciled; no ranking change.** Only the preceding page update landed after `97eaf4f3`; no launch-trigger path changed, so no fresh-root launch was run. Full build exit 0 (871.0 s); configured fast tier exit 0, 195/195 in 137.21 s (same count as the previous automated scan; 227 registered, GUI off, ONNX unset, host-local listening comparison excluded). The latest manual entry's 241/241 and STATUS/plan's 241/209 counts describe the main checkout, not this scan. Gates unchanged: UA contract exit 0/PASS, acceptance BLOCKED 0/20; voicebank exit 3/`passed: false`, 52 errors/12 categories; G2, G3, G4 and G5 each exit 3/`passed: false`. D9 remains 7/7 `NOT_OBSERVED` and Appendix C unfilled. The installed PJS manifest/receipt now independently confirm 1,696 Japanese units and `signerTrusted: true`; `pjs_cohort.py` remains absent, while `pjs_engineering_bank.py` exists. Reconciled stale empty-state and scratch-only wording against `renderDiagnosticCode`, `resolveVoiceIdentity` and the default installed bank; no new render or human acceptance is claimed. Plan D1 still omits the PJS engineering route; its production-bank blocker remains valid. The preceding manual entries labelled 21:40/22:10 were already present at this scan's earlier wall clock and are retained as authored. Direction: **no change** to section 5; owner listening and D9 remain first, and no remaining item has new completion evidence.

- **2026-10-20 20:45 KST, manual update after the engineering rehearsal of UA-001–UA-019 — item 3 partly done, five defects fixed.** The standalone was driven with the PJS bank through new project, notes, Japanese lyrics, phoneme/unit/seam/pitch edits, save, quit and reopen, master and stem export, forced kill and autosave recovery. Recorded as engineering rehearsal only; no UA row changes. Fixed and pushed: `914f3b1d` Command-A fell through to the plain A key and silently slurred notes (Command-C/V/X/Q/L/I likewise ran single-key commands); a lyric now opens selected; a click narrows a multi-selection and Return edits the earliest note. `691c72a7` the unit lane showed only overrides, so the units a render chose (UA-005) were invisible. `2ebba29a` New Project and Recover built a new editor controller that never received the voicebank cards, so a correct sample singer read *Voicebank unavailable* until relaunch (signing trust was not involved). `961de9e1` Recover Autosave listed "Project 00000000000003eb — revision N" rows; they now name the file and time, newest first. Each fix has a regression test and the full suite stayed 241/241. Passed without change: export (48 kHz 24-bit stereo master and stem, SHA-256 receipt, `afinfo` agrees) and reopen parity (bit-identical master after quit and reopen). Open findings, not yet fixed: no recovery offer after a crash (the user must find File > Recover Autosave); new projects in a session share one internal project id; the phoneme/unit/seam lanes are about 20 px each, so seam rails are hard to see and hit; the lyric-distribution review and stem file names show raw ids; cycling a unit variant can pick a unit two octaves from the note (m47 for MIDI 71), which belongs to item 4; an export from a dirty working tree records the last commit as its build without saying so. Not rehearsed: audible transport and seek/loop (UA-010/011), voicebank relink (UA-016), the 30-minute session (UA-020). Correction: the two manual entries above labelled 21:40 and 22:10 were written at 19:17 and 19:33. Direction: item 3 continues with relink, transport and the crash-recovery offer; owner items 1, 2 and 6 still come first.
