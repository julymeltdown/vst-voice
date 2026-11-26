# Project SEAM — current status

Reviewed 26 November 2026 against `a062db2e` on macOS arm64. The development instrument edits and renders a Japanese song with a separately installed engineering singer. A new user still needs a singer before the same score can produce audio. The 25 November documentation consolidation changed no release acceptance rows.

## Overall progress

**Two of eight functional steps are met for a new user (25%).** The existing F0–F7 ladder gives one count to each fully met step. F0 builds and passes local tests. F1 opens the standalone editor and supports edits. F2 (sound on a fresh install) is the first unmet step. With PJS installed separately, engineering can reach F3 and export a complete song. F4 needs a listening decision, F5 an unaided Alpha session, F6 target DAW evidence and F7 a distributable installation. The steps differ in effort. The repository has no measured percentage for total engineering work. [Functional ladder](plans/FUNCTIONAL_READINESS.md#2-functional-ladder)

The installed-bank export on 26 November produced the 30.0-second PJS WAV with peak 0.3481 and no clipping. Its SHA-256, `3731729caf95d68696711e17453726d135471d52c8f35436ef45f0072fe74fc6`, matched the October reference. With an empty bank directory the same command exited 1, identified the missing singer and wrote no master. The built app's resource inventory lists zero bundled banks. These two runs used the CLI and involved no human playback. [Export implementation](../apps/seam-voicebank-cli/main.cpp) · [Bundle inventory](../packaging/release-resource-inventory.json)

## Gates

The 26 November check ran the Alpha and Windows record validators, the production bank and G2–G5 gates, and the full-product registry and profile checks. External Beta and public-release counts come from their canonical requirement lists and evidence arrays.

| Gate | Source of truth | State | What the state rests on |
|---|---|---|---|
| Usable Alpha (UA-001–UA-020) | [contract](product/USABLE_ALPHA_ACCEPTANCE.md), [record](product/usable-alpha-acceptance.json) | `BLOCKED`, **0/20** recorded | All 20 rows `NOT_RUN` with no evidence. `scripts/verify_usable_alpha_contract.py` prints `USABLE_ALPHA_CONTRACT=PASS`, which only means the record is well formed. The contract itself requires a rights-cleared production bank, Japanese lyrics and sample-concatenative audio. |
| Beta voicebank 01 | [dossier](voicebank/beta-voicebank-01-dossier.json), `tools/external_beta/voicebank_gate.py` | **0/12** categories cleared, exit 3, 52 errors | 12 blocked categories: rights-approval, recording-session-logs, source-derived-hash-inventory, inventory-coverage, retake-closure, marker-pitch-qa, renderer-listening-qa, hostile-package-validation, signed-package, clean-install, reference-song, evidence-root. |
| External Beta (EB-001–EB-009) | [contract](product/external-beta-acceptance.json) | `BLOCKED`, **0/9** recorded | No approved candidate or evidence records. Five host tuples target macOS and four target Windows. EB-006 needs REAPER, Bitwig Studio and Logic Pro installs; EB-009 needs the complete full-product contract. |
| Full product contract (R1–R20, V01–V18) | [contract](product/full-product-beta-contract.json) | Registry valid, 13 qualification items pending, evidence `NOT_RUN` | 20 requirements, 18 work packages, 83 cases. The evaluation profile still needs criteria such as pronunciation, acoustic boundaries, reference machines and resource coverage. Scope: ja/en/ko, macOS arm64 and Windows x86_64, nine host tuples, six resource kinds, four backends. `releasedResources` is empty and the resource matrix is `UNRESOLVED`. |
| Phase 13A release gates G2–G5 | [matrix](phase13a/mandatory-validation-matrix.json) | **0/4** gates passed | The gate outputs list 2, 7, 24 and 25 blocked IDs respectively. They overlap across gates. Outstanding target evidence includes validators, signing, clean-OS installs, long soaks and commercial hosts. This matrix is older and broader than the nine full-product host tuples. |
| Public release | [contract](product/public-release-acceptance.json) | `BLOCKED`, **0/14** requirements evidenced | Depends on External Beta closure. The Windows standalone contract PW-001–PW-020 is entirely `NOT_RUN` ([record](product/public-windows-standalone-acceptance.json)). |
| U45 semantic reconciliation | [hold](implementation/U45_TYPED_AUDIT_RECONCILIATION.md) | Held (`UNAVAILABLE`) | Intentional admission hold; do not flip during cleanup. |

## What works today (engineering evidence, not acceptance)

These paths exist in code and are exercised by automated tests on macOS arm64. None of them is user, listening, host or release acceptance.

- **Editing:** the shared SING shell (Cerulean, EMO and SCENE) with notes, lyrics, phoneme/unit/seam lanes, pitch points, expression curves, vibrato, undo/redo and accessibility tree generation (`libs/seam-native-ui`).
- **Project lifecycle:** save, Save As, reopen, autosave recovery and exact voicebank relink without substitution (`libs/seam-authoring-runtime`). Project format writer v21; readers 1–21.
- **Rendering:** sample-concatenative and procedural singer rendering through the shared `ProductionProjectRenderer`; installed-singer song journeys, PCM cache and snapshot continuity; a protocol-3 worker with arithmetic fixtures. Qualifying the learned singer still requires an independent acoustic/vocoder evaluation and listening evidence. [Neural qualification issue](../BETA_READINESS_ISSUES.md#seam-beta-p0-08-neural-singer-qualification)
- **Voicebanks:** signed `.seambank` packaging, verification and transactional installation; installed-singer selection; Studio producer workspace with take review, prepared-donor intake and exact pointer recovery.
- **Export and interchange:** transactional master and stem export with receipts; USTX and Standard MIDI import/export, with an optional external-reader oracle in `tools/openutau_oracle/` (not part of CTest).
- **Plug-ins:** CLAP render player and embedded editor, exercised by the first-party hosts. The Cocoa `seam_clap_editor_host` displayed the editor, received note input and restored 12,239 matching state bytes on 25 November. VST3/AUv2 builds come from the Phase 13A pipeline; no validator or commercial host has run them.

## What blocks the next gate

In priority order. The [completion plan](plans/2026-10-18-project-completion-plan.md) turns these into work, and [functional readiness](plans/FUNCTIONAL_READINESS.md) tracks what a user can actually do today and the next smallest steps.

1. **Choose the voice direction.** Compare takes 1b and 2b with their same-key references. Record the preferred take, or reject both, and give a timestamp for the most important problem. The frozen observation kit verified all 95 files on 26 November. Its seven human steps are still unobserved. The listening comparison and editor observation can run now. [Audition guide](reviews/PRODUCT_VOICE_AND_PLUGIN_REVIEW_2026-11-25.md) · [Observation form](implementation/NATIVE_OBSERVATION_FORM_2026-10-12.json)
2. **Qualify one Japanese production singer.** Internet samples are the selected sourcing route. The exact recordings, Japanese coverage, performer rights, EULA terms, producer and distinct reviewer still need to be established. The October intake counted 1,469 female recordings across three cohorts. None has been admitted. Produce and review a small song-specific candidate, then install it on a fresh Mac and render the same score. [Source decision](implementation/W05A_BANK_ROUTE_DECISION_2026-10-12.md)
3. **Record the unaided Alpha journey.** Use that cleared bank on the target Mac, retain the save/reopen and external playback evidence, and complete the 30-minute stability session. Record the results against UA-001–UA-020. [Alpha protocol](product/USABLE_ALPHA_ACCEPTANCE.md)
4. **Qualify the Beta candidate.** Bind the signed, notarized installation, target DAW runs, full-product evaluation, independent review and archive to the same candidate. The current contract covers both platforms and all nine host tuples. A macOS-only first Beta still requires an approved scope change. [Beta runbook](product/EXTERNAL_BETA_RUNBOOK.md)

## Test tiers

Every CTest entry has one kind label, `unit`, `journey` or `contract`, and may also carry the tags `gui`, `slow` (5 s or more) and `process-lifetime`. [cmake/SeamTestLabels.cmake](../cmake/SeamTestLabels.cmake) defines the labels, and `seam_ctest_label_coverage` fails when an entry has no kind, two kinds or an unknown label. CTest also runs the fixture setups that a selected entry requires, so a tier can include an entry from outside its label.

On 26 November the full Release rebuild and `ctest --test-dir build/release -j6 --output-on-failure` passed **241/241** in 297.57 seconds in the integrating checkout with pre-existing local CoreAudio diagnostics. The suite included the standalone production-path contract, CLAP editor host smoke and editor input checks. The dedicated scan clone disables native GUI tests and has a different optional SDK configuration. Its latest count and exact filters are on the readiness page. [Integration evidence](implementation/FULL_SCOPE_BETA_EXECUTION.md) · [Scan results](plans/FUNCTIONAL_READINESS.md)

## How to refresh this page

```sh
git fetch origin && git pull --ff-only
python3 -B scripts/verify_usable_alpha_contract.py --root .
python3 -B -m tools.external_beta.voicebank_gate --dossier docs/voicebank/beta-voicebank-01-dossier.json --root .
for gate in G2 G3 G4 G5; do python3 -B tools/phase13a/release_gate.py check --matrix docs/phase13a/mandatory-validation-matrix.json --gate "$gate"; done
```

Read the linked canonical records for each count. Use the same project with the installed engineering bank and an empty bank directory to reproduce the export comparison.
