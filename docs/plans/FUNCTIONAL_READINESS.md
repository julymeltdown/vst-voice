# Project SEAM — functional readiness

**Living document.** It answers one question: *what still has to happen before Project SEAM functions for a person who sits down to sing a song with it?* A scheduled scan refreshes it every seven hours (see [§9](#9-how-this-page-is-maintained)). Each scan rewrites the current-state sections in place and replaces the previous entry in the [direction log](#10-direction-log). Gate states remain owned by [STATUS](../STATUS.md), and the order of milestone work by the [completion plan](2026-10-18-project-completion-plan.md). This page translates both into "can it function yet, and what is the next smallest step?"

Last full scan: **2026-11-25 09:55 KST**, dedicated clone at `origin/master` `27c338f3`, clean tree before scan. Fresh build/test results are in the latest direction-log entry; earlier measurements below are retained. Only the previous scan's page update landed since `f1393319`; no launch-trigger path changed.

Integration update, 25 November at `38238dad`. The shared standalone and plug-in editor now defaults to Cerulean. The main Release configuration passed 241/241 tests, including localized expression editing. Its populated standalone and embedded plug-in captures matched across the painter refactor. [Execution evidence](../implementation/FULL_SCOPE_BETA_EXECUTION.md)

## 1. Verdict

Project SEAM now sings Japanese in a human-recorded voice, but only with an engineering bank that can never ship. On 2026-10-20 the PJS 1.1 corpus became an installed, signed sample bank (`engineering.pjs.male.01`, 1,696 units, male voice, CC BY-SA 4.0). With it installed, `Project SEAM.app` opens the 30-second observation song with the singer card at *READY* and reports `voicebank_resolved=true`, `render_state=ready`, `transport_available=true`. The same project exports a 30-second master in which every note is voiced and 29 of 30 notes sit within 50 cents of the score. No owner listening verdict is recorded.

**A clean install still cannot make a sound,** because no singer ships with the application; since 2026-10-20 it says so honestly (*No singer selected*, with install guidance) instead of reporting a missing bank. The owner requested a more feminine timbre. The [audition guide](../reviews/PRODUCT_VOICE_AND_PLUGIN_REVIEW_2026-11-25.md) identifies the comparison files and which takes retain the written key. The next step that changes the project's direction is the owner's listening verdict on the PJS render (item 1 in section 5), because it is the first evidence of whether sample-concatenative singing in this engine is good enough. The owner's observation session follows.

## 2. Functional ladder

Each rung needs the one before it. "Evidence" names this scan's results, a dated earlier run, or the code that decides the outcome.

| Rung | Meaning | State | Evidence |
|---|---|---|---|
| F0 Builds and tests | Release build and the scan's configured fast tier pass | **Yes, within scan scope; first run clean** | `cmake --build build/release -j6` exit 0 in 27.2 s, 142 targets, no reconfigure; `ctest --preset release -LE '^slow$' -E '^seam_listening_packet_comparison$' -j6` exited 0 at 195/195 in 38.83 s on its first run, including `seam_authoring_transport_tests` and the pair that flaked on 2026-11-21; 227 registered, with native GUI tests off (`SEAM_RUN_NATIVE_GUI_TESTS:BOOL=OFF`) and optional ONNX SDK unset in `build/release/CMakeCache.txt`; the excluded comparison needs host-local Downloads audio |
| F1 Launches and edits | The standalone opens a responsive editor | **Yes** | Fresh-root launch at `f906cf1d` (2026-11-02 01:01 KST): `build/release/Project SEAM.app` with copied EULA acceptance, `--paused --auto-close-ms 4000`, screenshot and `--evidence-dir`; exit 0, AppKit backend, capture and geometry/semantic evidence written |
| F2 Sound on a clean Mac | A new user hears anything at all | **No on a clean install; yes once a bank is installed** | Fresh-root launch at `f906cf1d` (2026-11-02) reported `voicebank_resolved=false`, `render_state=idle`, `transport_available=false`; the bundle contains no voicebank. With the PJS engineering bank installed under a fresh support root, the standalone opened the observation project and reported `voicebank_resolved=true`, `render_state=ready`, `render_diagnostic=Production multi-track routing render completed`, `transport_available=true` (2026-10-20) |
| F3 Japanese song in a recorded voice | Japanese lyrics sung by a sample-concatenative bank made from human recordings | **Engineering yes; product no** | `tools/voicebank_source_intake/pjs_engineering_bank.py` built `engineering.pjs.male.01` (1,696 units, 111 aliases, MIDI 44–79; `seam_voicebank_cli validate` 0 errors, 34 warnings). `seam_voicebank_cli export-installed-bank-project` rendered the observation song: 30.0 s, peak 0.348, not clipped, every note voiced, 29/30 notes within 50 cents of the score by the engine's pitch extractor, byte-identical on re-export. Male and CC BY-SA: not a product bank |
| F4 Musically acceptable to the owner | The owner hears a render and judges it usable | **Unknown** | No verdict recorded on any song; [observation form](../implementation/NATIVE_OBSERVATION_FORM_2026-10-12.json) still `NOT_OBSERVED` in every step, `largerObstacle: UNDECIDED` |
| F5 Unaided Usable Alpha journey | UA-001–UA-020 from Finder | **No** | 0/20 recorded, all `NOT_RUN` ([record](../product/usable-alpha-acceptance.json)) |
| F6 Works in a DAW | CLAP/VST3/AUv2 in REAPER, Bitwig, Logic | **No** | The first-party CLAP hosts load the player and embedded editor; G2–G5 all `passed: false` |
| F7 Shareable | Signed, notarized, installable by someone else | **No** | No Developer ID run; External Beta 0/9 |

F0 passes within the configured scan scope; the excluded listening comparison remains unverified here. F4 is now the rung that decides direction: the engine can sing, and the question is whether it sings well enough. F2 for a clean install still needs a shippable bank (D1/D3); the honest empty state is implemented. F6 and F7 are real but later.

## 3. What a person can do today, journey by journey

**Launch and edit.** Works. `NativeEditorApp::initialize` shows the EULA modal on a first launch (`platform::requestEulaAcceptance`), then opens an Untitled project with one track. Notes, Japanese lyrics, phoneme/unit/seam lanes, pitch points, expression curves, vibrato, undo/redo and the accessibility tree are implemented in `libs/seam-native-ui` and covered by journey tests such as `tests/test_sing_shell_journey.cpp`. Saved audio settings now reload from `settingsRoot/audio-settings.json`; a missing file preserves explicit launch settings, and the legacy `userDataRoot/Settings/audio-settings.json` store migrates only when the current store is absent (`NativeEditorApp::initialize`, `libs/seam-standalone/src/native_editor_app.cpp`). Three filtered cases in `tests/test_native_runtime.cpp` freshly passed for restart persistence, explicit startup and migration/current-store precedence, including malformed current data.

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
| 3 | **Rehearse UA-001–UA-019 with the PJS bank** (first pass 2026-10-20: twelve defects fixed, including the startup recovery offer, cross-session autosave pruning and raw ids in reviews, stems and unit cards; relink (now remembered across launches) and metered transport rehearsed on 2026-10-20; UA-020 remains; see the direction log) in the standalone (new project, Japanese lyrics, phoneme/unit/pitch/seam edits, save, reopen, autosave recovery, relink, master and stem export) and fix the defects found before the owner's unaided run | F5 pre-check, UA-001–019 | Engineering | 1–3 days | Each step either works or has a filed fix; recorded as engineering rehearsal, never as UA rows |
| 4 | **Repair loop on the PJS render**, driven by item 1's top three problems (timing, seams, pitch, unit choice), checked on the same song | F4, G-M1 | Engineering | ≤ 2 weeks | Each fix has a before/after criterion on the same song; the owner re-listens |
| 5 | **Voicebank scan cost for plug-in hosts — measured; budget to confirm.** `seam_voicebank_scan_benchmark` times the scan. The repo defines no Beta bank size, so the stand-in is four clones of the PJS bank (6,784 units, 337 MB). After `f20a20d8` (bank root canonicalized once per scan): installed PJS repeat 27 ms, first in process ~0.5 s; stand-in repeat ~100–110 ms (worst 148 ms), first in process ~0.47 s with a warm file cache and 1.9–2.6 s from a cold disk. Proposed budget for the stand-in: repeat ≤ 150 ms, warm first ≤ 1 s; the cold-disk first scan exceeds 1 s and would need a digest cache that persists across processes. Every scan still parses and re-encodes each manifest (the next cost) | F6 | Engineering (budget: owner) | ½ day | Budget accepted or changed; if cold first scans matter for DAW project load, a persistent digest cache lands within it |
| 6 | Decide D1, D2 and D5 using items 1–2 as input; the plan lists PJS as engineering-only option E | M1 entry | Owner | — | Decisions written in plan Appendix C |
| 7 | Decide whether the app should let a user trust a local signing key from Settings (G3) | UA journey from Finder | Owner | — | Yes → small engineering unit; no → the README's developer route stays the only one |

Items 3 and 5 can run in parallel with the owner's items 1, 2 and 6; item 4 starts when item 1 names problems. Done and retired on 2026-10-20: the PJS bank (former item 2), the honest empty state (former item 3, `9b71c5ab`) and the developer install route (former item 4, README *Giving the editor a singer*).

## 6. What engineering should not do yet

Do not start M2 release engineering (signing scripts, host matrix, soak runs) before F4 has a verdict. Do not expand the Japanese inventory toward the 2,052-prompt Beta set before a small bank has been heard. Do not tune the procedural singer as a substitute for a sample bank; UA-010 excludes it. Do not flip U45 or edit gate records outside their verifiers.

## 7. Owner decisions and what each unblocks

| Decision | Unblocks | Recorded owner decision |
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
4. updates sections 1–8 only where evidence changed and replaces the previous direction-log entry with at most one ranking change;
5. commits this page alone and pushes it, then verifies the remote hash. If anything fails it writes a log and stops without pushing.

Logs and each run's short summary go to `~/Library/Logs/ProjectSEAM/readiness-scan/`, and the summary's first line appears as a macOS notification. To recreate the clone, run `git clone <main checkout> ~/.codex/worktrees/readiness-scan` and then `git -C ~/.codex/worktrees/readiness-scan remote set-url origin https://github.com/julymeltdown/vst-voice.git`. To stop the schedule: `launchctl bootout gui/$(id -u)/com.projectseam.readiness-scan`.

## 10. Direction log

The latest scheduled scan is below. Earlier runs are available in the history of this file. The integration update above records work since that scan.

- **2026-11-25 09:55 KST, `27c338f3` — no change.** Only the previous readiness-page update landed; no launch-trigger path changed, so launch skipped. Build exit 0 (27.2 s, 142 targets, no reconfigure). The configured fast tier exited 0 at 195/195 in 38.83 s on its first run, back in the usual band and including `seam_authoring_transport_tests` and the 2026-11-21 flake pair (227 registered, GUI off, ONNX unset, host-local listening comparison excluded). Gates unchanged: UA contract exit 0/PASS, acceptance BLOCKED 0/20; voicebank exit 3/`passed: false`, 12 blocked categories/52 errors; G2, G3, G4 and G5 each exit 3/`passed: false` (blocked 2/7/24/25, 0 errors). D9 remains 7/7 `NOT_OBSERVED` with `largerObstacle: UNDECIDED` and an empty observer name; Appendix C unfilled and no M1-4 owner verdict; the `out/engineering-banks` PJS render and project named in item 1 still exist only in the main checkout, not this clone; installed PJS still 1,696 `ja` units with `signatureValid: true` and `signerTrusted: true`, `pjs_engineering_bank.py` present and `pjs_cohort.py` absent. STATUS/plan's 241/209 counts remain main-checkout receipts; plan D1 still omits the PJS engineering route, while its production-bank blocker remains valid. Direction: **no change**; an unchanged clean tier and gate state add no capability or owner evidence, so owner listening and D9 remain first and no item meets its retirement condition. Engineering evidence only, not acceptance.
