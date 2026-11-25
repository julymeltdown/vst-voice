# Project SEAM — current status

Reviewed 25 November 2026 against `38238dad` on macOS arm64. The development app can sing the observation score with the installed Japanese engineering bank. A fresh install ships without a singer. The owner wants a more feminine timbre. The two audition routes still need a listening decision. [Voice and plug-in review](reviews/PRODUCT_VOICE_AND_PLUGIN_REVIEW_2026-11-25.md)

## Gates

The Alpha verifier, voicebank gate and G2–G5 checks were run on 25 November. The other rows summarize the linked records. "Recorded" means a hash-bound evidence record accepted by the gate's own verifier.

| Gate | Source of truth | State | What the state rests on |
|---|---|---|---|
| Usable Alpha (UA-001–UA-020) | [contract](product/USABLE_ALPHA_ACCEPTANCE.md), [record](product/usable-alpha-acceptance.json) | `BLOCKED`, **0/20** recorded | All 20 rows `NOT_RUN` with no evidence. `scripts/verify_usable_alpha_contract.py` prints `USABLE_ALPHA_CONTRACT=PASS`, which only means the record is well formed. The contract itself requires a rights-cleared production bank, Japanese lyrics and sample-concatenative audio. |
| Beta voicebank 01 | [dossier](voicebank/beta-voicebank-01-dossier.json), `tools/external_beta/voicebank_gate.py` | Fails: exit 3, `passed: false`, 52 errors | 12 blocked categories: rights-approval, recording-session-logs, source-derived-hash-inventory, inventory-coverage, retake-closure, marker-pitch-qa, renderer-listening-qa, hostile-package-validation, signed-package, clean-install, reference-song, evidence-root. |
| External Beta (EB-001–EB-009) | [contract](product/external-beta-acceptance.json) | `BLOCKED`, **0/9** recorded | No candidate, no evidence record in any category. EB-006 needs REAPER, Bitwig Studio and Logic Pro installs; EB-009 needs the complete full-product contract. |
| Full product contract (R1–R20, V01–V18) | [contract](product/full-product-beta-contract.json) | Registry valid; evidence `NOT_RUN` | 20 requirements, 18 work packages, 83 cases. Scope: ja/en/ko, macOS arm64 and Windows x86_64, nine host tuples, six resource kinds, four backends. `releasedResources` is empty and the resource matrix is `UNRESOLVED`. |
| Phase 13A release gates G2–G5 | [matrix](phase13a/mandatory-validation-matrix.json) | All `passed: false` | Validators, `auval`, signing, notarization, clean-OS installers, long soaks and commercial hosts have no target execution. This matrix is older and broader than the nine full-product host tuples. |
| Public release | [contract](product/public-release-acceptance.json) | `BLOCKED`, not `PUBLIC_ACTIVE` | Depends on External Beta closure. The Windows standalone contract PW-001–PW-020 is entirely `NOT_RUN` ([record](product/public-windows-standalone-acceptance.json)). |
| U45 semantic reconciliation | [hold](implementation/U45_TYPED_AUDIT_RECONCILIATION.md) | Held (`UNAVAILABLE`) | Intentional admission hold; do not flip during cleanup. |

## What works today (engineering evidence, not acceptance)

These paths exist in code and are exercised by automated tests on macOS arm64. None of them is user, listening, host or release acceptance.

- **Editing:** the shared SING shell (Cerulean, EMO and SCENE) with notes, lyrics, phoneme/unit/seam lanes, pitch points, expression curves, vibrato, undo/redo and accessibility tree generation (`libs/seam-native-ui`).
- **Project lifecycle:** save, Save As, reopen, autosave recovery and exact voicebank relink without substitution (`libs/seam-authoring-runtime`). Project format writer v21; readers 1–21.
- **Rendering:** sample-concatenative and procedural singer rendering through the shared `ProductionProjectRenderer`; installed-singer song journeys, PCM cache and snapshot continuity; an admitted neural protocol-3 path that only runs arithmetic fixtures.
- **Voicebanks:** signed `.seambank` packaging, verification and transactional installation; installed-singer selection; Studio producer workspace with take review, prepared-donor intake and exact pointer recovery.
- **Export and interchange:** transactional master and stem export with receipts; USTX and Standard MIDI import/export, with an optional external-reader oracle in `tools/openutau_oracle/` (not part of CTest).
- **Plug-ins:** CLAP render player and embedded editor, exercised by the first-party hosts. The Cocoa `seam_clap_editor_host` displayed the editor, received note input and restored 12,239 matching state bytes on 25 November. VST3/AUv2 builds come from the Phase 13A pipeline; no validator or commercial host has run them.

## What blocks the next gate

In priority order. The [completion plan](plans/2026-10-18-project-completion-plan.md) turns these into work, and [functional readiness](plans/FUNCTIONAL_READINESS.md) tracks what a user can actually do today and the next smallest steps.

1. **A production voicebank that is legally usable for its purpose.** No source has passed rights review. The 1,469 female recordings in the October source census (VocalSet f1, Karoryfer Merry Orks, CMU ARCTIC SLT; local and untracked under `out/`) are unadmitted and have performer and personality rights `NOT_ESTABLISHED`. None of them contains Japanese consonant–vowel syllables, so none can become the Japanese bank that Usable Alpha requires. Usable Alpha and External Beta both wait on this.
2. **Owner decisions:** source route and language; rights approver and EULA policy; named producer and distinct reviewer; whether to scope the first Beta to macOS.
3. **Musical judgment:** no owner or listener verdict has been recorded on any rendered song. The frozen native observation kit (`tools/observation/open_native_song.py`, 95 files verified) is ready and waiting.
4. **The unaided Usable Alpha journey** on the target Mac, run after 1–3.
5. **Release engineering against real targets:** Developer ID signing and notarization, REAPER/Bitwig/Logic installs, and a Windows host or an explicit macOS-only Beta decision.

## Test tiers

Every CTest entry has one kind label, `unit`, `journey` or `contract`, and may also carry the tags `gui`, `slow` (5 s or more) and `process-lifetime`. [cmake/SeamTestLabels.cmake](../cmake/SeamTestLabels.cmake) defines the labels, and `seam_ctest_label_coverage` fails when an entry has no kind, two kinds or an unknown label. CTest also runs the fixture setups that a selected entry requires, so a tier can include an entry from outside its label.

On 25 November the full Release rebuild and `ctest --test-dir build/release -j6 --output-on-failure` passed **241/241** in 252.50 seconds in the integrating checkout with local CoreAudio edits. The dedicated scan clone disables native GUI tests and has a different optional SDK configuration. Its latest count and exact filters are on the readiness page. [Integration evidence](implementation/FULL_SCOPE_BETA_EXECUTION.md) · [Scan results](plans/FUNCTIONAL_READINESS.md)

## How to refresh this page

```sh
git fetch origin && git pull --ff-only
python3 -B scripts/verify_usable_alpha_contract.py --root .
python3 -B -m tools.external_beta.voicebank_gate --dossier docs/voicebank/beta-voicebank-01-dossier.json --root .
for gate in G2 G3 G4 G5; do python3 -B tools/phase13a/release_gate.py check --matrix docs/phase13a/mandatory-validation-matrix.json --gate "$gate"; done
```

Then re-read the contract JSON files linked in the table. Change a row only when its own verifier changes state.
