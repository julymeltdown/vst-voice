# Project SEAM Beta Readiness: Discovered Issues

- **Snapshot date:** 2026-08-30
- **Branch:** `codex/external-beta-completion`
- **Source commit:** `970d159d06a2daa11932a9dbc22a337ecf9dbe25`
- **Current decision:** `HOLD / NO-GO` for an open/public beta
- **Estimated practical readiness:** approximately 42% (engineering estimate, +/-5 percentage points)
- **Formal promotion readiness:** 0% because the canonical Usable Alpha matrix is 0/20 PASS and no External Beta candidate evidence has been accepted

This file is the root-level issue register requested after the repository-wide beta-readiness review. It is intentionally shorter and more operational than the full analysis in [docs/reviews/PROJECT_SEAM_OPEN_BETA_READINESS_2026-08-30.md](docs/reviews/PROJECT_SEAM_OPEN_BETA_READINESS_2026-08-30.md).

The register distinguishes implementation from release proof. A source contract, validator, unit test, or schema can be complete while the corresponding installed-product gate remains open.

## Severity and status

- **P0:** Blocks any honest external beta claim.
- **P1:** Must be closed before a controlled external cohort is supportable.
- **P2:** Does not independently block a small private alpha, but materially raises beta regression and operating cost.
- Every item below is **OPEN** unless a later evidence-bound update explicitly changes its status.

## P0: Release blockers

### SEAM-BETA-P0-01: No rights-cleared, usable Beta Voicebank

**Evidence**

- [docs/voicebank/beta-voicebank-01-dossier.json](docs/voicebank/beta-voicebank-01-dossier.json) is `BLOCKED`.
- Package hashes, delegated signing identity, trust epoch, source/derived assets, rights review, clean-install receipt, and reference-song receipt are empty or `NOT_RUN`.
- All four mandatory permissions are false: source use, transformation, redistribution, and end-user rendered audio.
- The dossier's `requiredUnits` array is empty.
- [docs/voicebank/BETA_JAPANESE_CVVC_INVENTORY.json](docs/voicebank/BETA_JAPANESE_CVVC_INVENTORY.json) is only an inventory artifact. Its checked-in snapshot contains 72 coverage keys and 144 takes but only the `k`, `s`, and `t` consonant families; it is not evidence of a complete, recorded, redistributable Japanese singing bank.
- [docs/voicebank/BETA_VOICEBANK_ACCEPTANCE.md](docs/voicebank/BETA_VOICEBANK_ACCEPTANCE.md) explicitly says the real `.seambank` and private rights records are external release inputs and that the checked-in dossier is only a blocked contract template.

**Impact**

External musicians cannot evaluate the product's core singing journey using a bank Project SEAM is demonstrably allowed to transform and redistribute.

**Closure criteria**

- One exact non-official `(voicebankId, version, contentSha256)` is frozen.
- Source and derived assets are hash-bound.
- Rights evidence explicitly covers source use, transformation, redistribution as a local singing voicebank, and commercial/non-commercial end-user renders in the intended territories.
- Required Japanese unit and pitch-layer coverage is complete.
- Marker, pitch-mark, loop, clipping, DC-offset, and retake QA pass.
- The bank passes four-renderer listening QA, hostile-package validation, signed package verification, clean installation, and a canonical reference-song render.

### SEAM-BETA-P0-02: Release candidate identity is stale and not immutable

**Evidence**

- Repository HEAD is `970d159d...`.
- The inspected `build-release-current` CMake cache and built `Info.plist` identify source commit `776d43e2...`, build ID `0.13.1-local`, and trust epoch `0`.
- The build system permits an incremental local build to retain an older configured identity.

**Impact**

Screenshots, test output, binaries, plug-ins, installers, SBOMs, and evidence records cannot be proven to describe one exact source candidate.

**Closure criteria**

- A clean candidate configure injects the exact source commit, non-local release ID, and valid trust epoch.
- Configuration fails closed when cached identity differs from the candidate source.
- App, CLAP, VST3, AUv2, installer, SBOM, and all evidence records report the same immutable candidate identity.

### SEAM-BETA-P0-03: No distributable, trusted installer candidate

**Evidence**

- The current local macOS app is ad-hoc signed and fails strict distribution verification.
- A local Developer ID staging artifact remains unnotarized and Gatekeeper-rejected.
- No single authoritative notarized/stapled macOS installer is present.
- No signed Windows x64 installer and clean-install evidence are present.

**Impact**

The repository has build outputs, but not a release artifact a beta tester can safely install and identify.

**Closure criteria**

- macOS app, plug-ins, and installer are Developer ID signed with Hardened Runtime, notarized, stapled, and verified after download on a clean target account/machine.
- If Windows remains in beta scope, the x64 installer and binaries are Authenticode-signed, timestamped, clean-installed, and uninstall-tested.
- Installed bytes resolve to the exact candidate root from P0-02.

### SEAM-BETA-P0-04: Canonical standalone musician journey is 0/20 PASS

**Evidence**

- [docs/product/usable-alpha-acceptance.json](docs/product/usable-alpha-acceptance.json) has no completed physical run for the 20 canonical rows.
- The rows cover Finder launch, first-run flow, project creation, notes and lyrics, technical edits, production audio, transport, save/reopen, recovery, bank relink, master/stem export, external playback, and a 30-minute session.

**Impact**

Unit/controller success does not prove native dialogs, device negotiation, permissions, crash recovery, file associations, or exported audio work as one installed journey.

**Closure criteria**

- All 20 rows pass against the same signed-installed candidate.
- Evidence is timestamped, reviewer-attributed, hash-bound, and retained under the canonical evidence root.

### SEAM-BETA-P0-05: Target OS and DAW compatibility matrix is incomplete

**Evidence**

- No completed checked-in records cover the required nine host tuples.
- The required set includes REAPER and Bitwig CLAP/VST3 on macOS and Windows, plus Logic Pro AUv2 on macOS.
- Internal hosts and generic plug-in validators do not demonstrate real DAW scanning, editor lifecycle, state recall, isolation, or offline bounce.

**Impact**

Host-specific crashes, state corruption, scan rejection, GUI lifecycle defects, and bounce differences can reach testers undetected.

**Closure criteria**

- Every in-scope OS/format/DAW tuple passes scan, instantiate, edit, save/reload, playback, offline bounce, close/reopen, and uninstall/rescan scenarios using installed candidate bytes.

### SEAM-BETA-P0-06: Physical audio, accessibility, soak, and human acceptance are unproven

**Evidence**

- VoiceOver, Accessibility Inspector, Narrator, UIA Verify, and Inspect runs are `NOT_RUN`.
- There is no current signed-installed evidence for physical listening, external-player comparison, audio/MIDI device loss and reconnect, sleep/wake, buffer-size or sample-rate changes, or actual MIDI hardware.
- There is no accepted 30-minute or 120-minute physical soak result.
- There is no completed external-musician cohort evidence.

**Impact**

The product can pass deterministic and visual checks while failing on real audio hardware, assistive technology, long sessions, or musician workflows.

**Closure criteria**

- Physical audio/MIDI scenarios and soak thresholds pass.
- VoiceOver and Narrator target runs pass on installed builds.
- Multiple external musicians complete the reference journey with Blocker/Critical count at zero.

### SEAM-BETA-P0-07: No governed release authorization or immutable archive

**Evidence**

- The External Beta aggregate remains `BLOCKED` with no accepted evidence set.
- There is no signed release-role authorization, complete candidate-root provenance tree, governed raw evidence archive, or external immutable anchor.

**Impact**

Even if individual checks pass, the team cannot prove what was authorized, shipped, or later revoked.

**Closure criteria**

- Independent release roles approve the exact candidate.
- Named build/install/evidence transformations are hash-bound.
- The raw archive is immutable and externally anchored.
- Pause, revoke, rollback, and revalidation decisions are auditable.

## P1: Product and operating defects

### SEAM-BETA-P1-01: Repeated support export can collide

`NativeEditorApp` always targets `Support/latest-diagnostic.zip`, while `SupportBundleService` rejects an existing destination. A second export can therefore fail unless the previous archive is removed.

**Required change:** use a timestamp/candidate-bound filename or an explicit preview plus atomic replace/save-as flow.

### SEAM-BETA-P1-02: User attachments are assigned an unsafe privacy class

Generated diagnostics are allowlisted and filtered, but consented attachments are copied after only basic regular-file, name, and size checks. The enclosing manifest can still label the entire bundle `ExportSafe` even when a project, lyric, raw log, secret, or audio file was attached.

**Required change:** separate generated-diagnostic and user-attachment privacy classes; preview every attachment and require explicit per-file consent without claiming the attachment itself is export-safe.

### SEAM-BETA-P1-03: Operational approvals are not cryptographically authoritative

The release operations path changes state from actor role strings, booleans, and approval lists. It checks candidate consistency but does not require a signed, hash-chained audit/approval record.

**Required change:** bind every approval to candidate root, previous decision digest, signer identity, signature, and append-only authority.

### SEAM-BETA-P1-04: Pause and revoke are not proven to reach installed clients

The operations model can represent `DISTRIBUTION_PAUSED` and `REVOKED`, but there is no end-to-end evidence that an installed updater/client consumes and enforces that authority.

**Required change:** implement and rehearse signed pause/revoke propagation, client enforcement, cached/offline behavior, and recovery.

### SEAM-BETA-P1-05: An invalid soak profile silently becomes a five-second smoke run

The Phase 12C soak runner selects 7,200 seconds only for the exact `full` profile; arbitrary or misspelled profile values fall through to a five-second run.

**Required change:** parse a closed enum, reject unknown profiles, bind the selected duration into the receipt, and require heartbeat/watchdog evidence for the full run.

### SEAM-BETA-P1-06: Validators are ahead of evidence collectors

Several release tools validate supplied JSON records but do not drive the installed product, collect metric series, capture the environment, or preserve raw evidence themselves.

**Required change:** add candidate-bound collectors for installation, DAW hosts, accessibility, physical soak, and cohort sessions; validators should consume collector-produced records.

### SEAM-BETA-P1-07: No verified field support loop

The application can create a local support ZIP, but there is no verified intake destination, ticket handoff, acknowledgement, triage owner, escalation path, or pause/revoke service-level rehearsal.

**Required change:** run a complete tester-to-triage support exercise with privacy review and measured response ownership.

## P2: Structural and maintenance risks

### SEAM-BETA-P2-01: Native UI complexity is concentrated in very large files

The editor controller, native UI test file, editor scene, AppKit window implementation, and application controller each concentrate several unrelated state machines. This raises merge conflict, regression, and field-fix cost.

**Required change:** before broad beta iteration, extract only stable boundaries: input mode, selection/edit commands, accessibility dispatch, and overlay/panel coordination. Preserve behavior with state-machine tests and visual evidence.

### SEAM-BETA-P2-02: Cross-version migration is not verified as an installed product journey

Schema migrations and future-version rejection exist, but there is no predecessor-to-current fixture that jointly verifies projects, autosaves, media, voicebank catalog, CLAP state, plug-in rescan, and updater behavior.

**Required change:** create immutable N-to-N+1 installed fixtures and test update, rollback, reopen, rescan, and data preservation.

### SEAM-BETA-P2-03: Status documentation can overstate readiness

Several documents mix contract validity, source implementation, target-machine pass, and beta readiness. Strings such as `*_CONTRACT=PASS` can be mistaken for product acceptance, while older readiness percentages use a different denominator.

**Required change:** maintain one current release status page with four separate states: `CONTRACT_VALID`, `IMPLEMENTED`, `TARGET_PASS`, and `BETA_READY`.

## Beta Voicebank sourcing decision

### Decision

It is technically feasible to create the non-official Beta Voicebank by generating and processing synthetic speech, but a generic “free for commercial use” label is not enough for Project SEAM's distribution model.

The recommended order is:

1. **Best legal/technical fit:** a project-owned procedural synthetic voicebank generated from DSP primitives, with no borrowed human voice identity.
2. **Best quality-to-cost fit:** record the maintainer's or a consenting performer's voice under an explicit release, then apply documented pitch/formant/timbre processing to create a fictional non-official beta identity.
3. **Conditional experiment:** use an offline TTS model only after the engine, model weights, voice, training-data provenance, generated-output, modification, sample-library redistribution, and end-user render terms all pass review.
4. **Do not use for the distributable bank without written clearance:** free-tier SaaS TTS output, platform/system voices, celebrity-like voices, cloned voices, or corpus clips whose copyright license does not separately settle voice/personality/privacy rights.

### Why the procedural option is especially suitable here

- Project SEAM already has a deterministic Japanese inventory generator and a signed data-only `.seambank` format.
- A procedural generator can produce the exact vowel, CV, VC, VV, breath, glottal, release, and special units at the required pitch layers instead of cutting arbitrary prose.
- Generator version, parameters, source code, unit WAV hashes, markers, and package identity can be reproduced and audited.
- The result can be intentionally robotic and fictional. Beta needs a reliable non-official evaluation instrument; it does not need to claim final commercial character quality.
- The remaining risk becomes audible quality and phonetic coverage, which can be measured, rather than unclear rights in a human likeness.

### Mandatory license checklist for any third-party TTS path

- [ ] TTS engine code license is compatible with how the generator is run and distributed.
- [ ] Model-weight license permits the intended commercial use.
- [ ] The specific voice/model card is reviewed; a repository-level license is not substituted for a per-voice license.
- [ ] Training-data provenance and performer consent are documented sufficiently for the release territory.
- [ ] Generated audio may be used commercially.
- [ ] Generated audio may be modified and transformed.
- [ ] Processed unit WAVs may be redistributed as a reusable sample/singing-voicebank product, not only embedded in a finished video or song.
- [ ] End users may commercially release songs rendered from the bank.
- [ ] Attribution, share-alike, notice, source-offer, and downstream-license duties are compatible with `.seambank` distribution.
- [ ] The voice is not confusingly identifiable as, or marketed as, an unconsenting real person.
- [ ] A reviewer signs the redacted rights approval required by the Beta Voicebank dossier.

### Proposed evidence-bound spike

The fastest responsible experiment is a **procedural Beta Voicebank spike**, not immediate adoption of a third-party TTS service:

1. Generate a deliberately small but musically usable Japanese subset at MIDI 60 and 72 from owned DSP code.
2. Produce source WAVs deterministically and retain generator version, parameters, and hashes.
3. Generate or hand-correct markers and pitch marks, then run existing production validators.
4. Package and sign a non-official, characterless `.seambank`.
5. Clean-install it and render the canonical reference phrase/song in all four production renderers.
6. Conduct blind listening for intelligibility, joins, sustain stability, pitch accuracy, and fatigue.
7. Expand to the full accepted inventory only if the spike clears those thresholds.

This spike can establish technical viability without weakening the existing rights gate. It must not change the dossier to `PASS` until the actual package, complete inventory, listening results, clean install, and rights approval all exist.

## Open-beta GO rule

Open beta remains `NO-GO` until every P0 item is closed. A narrowly labeled macOS private alpha may be considered earlier, but only with an exact rights-cleared bank, immutable signed candidate, clean install, completed core musician journey, privacy-safe support path, and explicit scope exclusions.
