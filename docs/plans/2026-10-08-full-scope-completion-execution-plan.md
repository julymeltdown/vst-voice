# SEAM full-scope completion execution plan

Prepared: 2026-10-08; updated: 2026-10-09 (Asia/Seoul). Initial baseline after fetch/pull: `cfc7c777035537097ef9d247e5a8f21fca620a64`. Updated planning baseline: `96ec8fbee8e08b695866d909c090ca0c52a5a8d9`, verified against origin/master and live remote master during review. Language: English.

Status: **technically approved after required corrections** by **Review in English**, chat `01a11bad-88f2-7521-849e-c38111dc2197`. The reviewer explicitly approved once the prescribed edits landed; the lead applied and checked them. This document plans implementation; it does not mark any implementation or acceptance complete. The accompanying [goal prompt](2026-10-08-full-scope-completion-goal.md) is intended for a later execution run.

## Objective and authority

Complete an original virtual singer that lets a creator design a voice without supplying a recording, also supports authorized real-input production, turns either route into reusable installed resources, and finishes expressive songs in the required standalone and DAW workflows. Preserve Japanese, English and Korean; classical, procedural and genuinely learned neural singing; expressive editing, automatic performance, takes/harmonies, original character performance, and release evidence.

The finish line is the original plan's **full-scope `EXTERNAL_BETA_READY`**, not a percentage, a passing build, or the first useful song. The later external cohort, `EXTERNAL_BETA_CLOSED`, and public activation are distinct operations outside this pre-GO execution goal. Required machinery for those later operations remains in scope where U44/U46 require it.

Authority remains user instructions and [AGENTS.md](../../AGENTS.md), then the [original full-scope plan](2026-09-05-1718-feat-full-scope-beta-go-plan.md) and its incorporated product report, the [full-product contract](../product/full-product-beta-contract.json), and the [External Beta contract](../product/EXTERNAL_BETA_ACCEPTANCE.md). This document owns current ordering and task selection. It neither deletes a requirement nor silently resolves an ambiguous acceptance rule. The [joint final audit](../reviews/FINAL_PROGRESS_AND_COMPLETENESS_REVIEW_2026-10-08.md) supplies the current gap evidence; older plans are historical context, not automatic new work.

**The goal is complete only when all of the following are true:**

1. Every original U1–U48 exit obligation is satisfied with current implementation and applicable acceptance evidence; all R1–R20, V01–V18, and the 83-case registry are covered on their required dimensions. Creators and producers complete AE1–AE5 on the supported installed product as mapped below. Contract-permitted resource/backend incompatibilities are explicit in the frozen matrix; no mandatory feature disappears through an exception.
2. The canonical 20-row Usable Alpha journey has valid accepted evidence from one unaided person, using a rights-cleared production sample bank, including hearing, external audible export verification and the 30-minute session. A procedural-only journey cannot satisfy sample-specific rows. This plan adopts Usable Alpha as a separate required workflow checkpoint; it does not claim the current READY evaluator already enforces that ledger or amend its gate schema.
3. The evaluation profile and resource matrix are frozen; all required assets are rights-cleared, versioned, qualified and bound to the candidate. Independent linguistic, listening, creator and producer acceptance is real.
4. Exact signed-installed macOS arm64 and Windows x64 descendants pass their required surfaces, all nine host tuples, accessibility, recovery, support and prescribed 30/120-minute workloads. Windows being deferred keeps the full goal incomplete.
5. The governed archive restores and rehashes; EB-001 through EB-009 pass for that lineage; required independent audit and authorized release-role decision name `EXTERNAL_BETA_READY`. There are no unresolved blocking/critical findings.
6. Verified implementation is committed and pushed; HEAD, origin/master and live remote master agree. No uncommitted or unpushed required unit is described as delivered.

## Execution policy

The lead integrates on `master` and owns the full-scope execution ledger, contracts and shared build files. Use one implementation stream and one independent reviewer by default. A second implementation stream is justified only by disjoint file ownership and a concrete dependency advantage; use isolated `codex/*` worktrees where needed. Do not create a large agent tree or unrelated chats. If the named reviewer chat is unavailable, continue reversible independent engineering, record the review as pending, and retain the affected acceptance hold. Do not create a replacement chat without user authorization.

Keep execution evidence in [FULL_SCOPE_BETA_EXECUTION.md](../implementation/FULL_SCOPE_BETA_EXECUTION.md) and the existing governed evidence stores. For each unit being changed, record its original exit criterion, current implementation, exact source/build/resource/configuration identity, focused check, raw receipt, responsible role, and remaining acceptance. Fill these rows while implementing; a fresh project-wide audit is not a prerequisite to the first repair. Do not maintain a second competing completion ledger.

Treat engineering verification and product acceptance as separate fields. A package can be engineering-verified while its human acceptance is pending; the original U-unit remains incomplete if its required acceptance is missing. A milestone is **DONE** only when its specified exit evidence exists and its code is integrated. Dependencies below are dependencies on the required engineering artifact unless explicitly labelled an acceptance gate. Waiting for a listener must not prevent independent compiler, persistence, UI, testing, packaging or neural-environment work.

For each coherent increment: inspect the current code and changed upstream commits, reproduce or identify the exact unmet criterion, make the smallest complete change, rebuild dependent targets, run focused checks, review the diff, update the root-owned ledger, commit and push promptly, and verify the remote hash. Follow AGENTS.md's checkpoint cadence and commit-body footer. A review finding is work to resolve, not an automatic request for human permission. Preserve unrelated files, stashes, branch refs and historical evidence. Never force-push master or merge an old branch wholesale without inspecting it.

Remain macOS-first. Keep Windows explicitly TODO in README. Resume Windows-specific implementation and qualification when the required Windows host and interactive operator capability are available; do not block useful macOS work meanwhile. Do not modify `.github`. Do not purchase compute/assets, contact outsiders, publish a release, or manufacture role approvals from this goal's engineering authority.

## Milestone order and work packages

Each W-ID below is a scheduling label, not a replacement for U/R/V IDs. Each package must map its concrete increments back to original exit criteria. “Review” below means technical review unless a human/role requirement is stated.

| Milestone | Packages | Observable exit | Hard prerequisite |
|---|---|---|---|
| M0 — Trustworthy execution inputs | W01–W03 | Portable listening packet checks reject bad inputs; branch work has explicit integration dispositions; supported test configurations have trustworthy receipts. | Current source snapshot; each package can start independently. |
| M1 — Usable macOS song workflow | W04 and W05a | Connected authoring/recovery/export behavior passes, and Gate A has an actual unaided session receipt. Sample-specific canonical checks are satisfied on the reviewed production sample bank. | Rebuilt app, applicable W02/U14 exact installation, W05a's bank approved by distinct producer/reviewer, and one unaided person. Agent rehearsal can start earlier with engineering resources; full M2 is not a prerequisite. |
| M2 — Complete source-to-singer production | W05 | Both real-input and recording-free routes create reviewed, versioned, installed resources that sing unseen lyrics and preserve old projects. | Relevant W02 packaging work, declared resource inputs and production reviews. |
| M3 — Complete musical and creator behavior | W06–W08 | Required musical controls, languages, interchange, host behavior, automatic performance and native surfaces satisfy their original criteria. | Required resource/backend contracts; development can proceed with labelled fixtures. |
| M4 — Learned neural candidate | W09 | A rights-bound trained acoustic/vocoder candidate runs held-out singing through the installed native worker with reproducible machine results. | Pinned environment and admitted inputs; final perceptual qualification is W12. |
| M5 — Complete character, platform and operations | W10–W11 | Production character, actual platform editor surfaces, accessibility and support/recovery operations have their required evidence. | Respective assets, devices and operators; Windows work is conditional on host availability. |
| M6 — Qualified full resource/product set | W12 | Frozen profile/matrix; all required resources, languages, styles, songs and independent creator judgments qualify. | Applicable W04–W11 artifacts; freeze criteria before final qualification scoring. |
| M7 — Exact candidate Beta GO | W13 | Signed-installed final lineage and restored evidence produce authorized `EXTERNAL_BETA_READY`. | All required implementation and acceptance from M0–M6, including Windows. |

The table is not a waterfall. Start the neural environment and external-input preparation early. Start Windows only when its prerequisite exists. Do not wait for all M0 acceptance before running the existing macOS journey. W12 has two stages: calibration/freeze supplies criteria to W06/W09; final qualification consumes their resulting candidates. Neither W06/W09 nor W12's calibration requires W12 final acceptance, avoiding a circular dependency.

### W01 — Repair listening packet integrity

**Owners:** `tools/singing_quality/build_listener_packet_002.py`, `verify_listener_packet_002.py`, reusable `listening_packet.py`, the packet guide, and focused packet tests. **Scope:** U43/U45 evidence; R16/R18/R19. **Prerequisite:** none beyond the current source and explicitly supplied input assets.

Use one versioned schema for stable named verdicts and derive both manifest and guide from it. Construct a Q3 pair from the same project, timings, lyrics, recipe and renderer, changing only the intended `pau`/vowel treatment while preserving the time slot. Use the existing rest-measurement tool as an input, not the old unmatched 3-second/7-second comparison. Replace hard-coded home/tmp inputs with explicit validated paths; retain project, recipe, resource, binary and settings hashes per rendered artifact. After the repaired tools are committed/pushed and the matching binary rebuilt, generate **packet 003** from those identities. Old packets remain immutable and `NOT_REVIEWED`; record their supersession separately. Never add invented provenance to r2's copied WAV.

Make checksum and objective-property failures return nonzero. Do not turn subjective quality into an automatic PASS. Register focused tests for a corrupt hash, missing input, mismatched identity, swapped verdict definitions and a control that changes unrelated notes/timing. **Engineering exit:** a new packet regenerates in a fresh output directory from declared inputs, originals still rehash, and each negative probe fails for the intended reason. **Acceptance exit:** a real reviewer records stable named judgments on these exact bytes. A diagnostic packet verdict does not close full singer qualification.

### W02 — Recover and reconcile branch work

**Owners:** the production, distribution and authoring-runtime files touched by the audited branches; `CMakeLists.txt` is lead-owned. **Scope:** U12–U14/U21/U45–U46; R3/R5/R15/R18/R20.

U21's request-owned retained-byte accounting is already integrated in `96ec8fbe`, including batch-directory entry accounting, exclusion of unrelated/preflight files and unsafe-entry refusal. The integrating session recorded a full dependent Release build, export/generation CTest 2/2 and Python mirror tests 5/5; this planning review inspected the commit and source, but did not rerun those checks. Do not schedule or reimplement `7cf0ee6c`. Preserve its regressions and implemented resume/cancellation/staleness behavior. The separate migration/admission disposition for pre-registry campaigns and human/reviewer acceptance remain open.

Before further integration that depends on the public-release suite, W03 must establish its supported passing verification policy; source review and unrelated engineering may continue meanwhile.

Review U14's dependent units in order: `8ffe2f7a` typed pinned-generation candidates → `cc083ad6` atomic installation → `620acabe` separate package/install steps. Reconcile each with current master and test each coherent increment before integration. Do not assume the merge tip `ce62f0d3` is itself the correct patch to merge. Then reconcile U45's `2a3d23e0` typed raw-evidence executor with master's existing `full_product_report.py` and newer replay/soak checks. U45 source review can run earlier; its resource-specific validation follows the final U14 representation.

For U12/U55, compare current behavior and patch equivalents; their files already exist. Paint-test patch `23c074e3` is already represented. Preserve the local stop-status WIP and all stashes. U55's workflow changes are outside scope. **Exit:** each relevant branch increment is integrated with focused regression evidence, or explicitly superseded/retired with its original requirement still accounted for. Installed bytes and typed gate decisions remain consistent through interrupted installation, stale producer generation and archive/report tampering. U14 installation runs under the sanitize/thread-sanitize lanes; a U14-produced engineering candidate can exercise U45 positively while still being rejected as release evidence. No branch is deleted as part of reconciliation.

### W03 — Make verification results dependable

**Owners:** CMake test registration, production/external-beta Python suites, and `tools/voice_model_training/` environment definitions. **Scope:** U1/U35/U44–U46; all later evidence.

Preserve the observed 231/232 run and isolated passing retry. Measure `seam_public_release_python_tests` under the intended scheduler, inspect fixture cost and resource contention, then choose an evidenced fix: split a slow independent workload, enforce justified serialization/resource limits, or adjust timeout with measured margin. Its first full suite receipt must include `96ec8fbe`, whose production mirror test was verified only in the focused lane at integration. Do not merely hide a failing case or assume the cause was proved.

Create a separate environment using the existing `requirements-training-macos-arm64.lock.txt` and [training instructions](../../tools/voice_model_training/TRAINING_COMMAND.md); configure its interpreter with the existing `SEAM_VOICE_TRAINING_PYTHON` cache variable and record the runtime fingerprint. Do not hard-code another user's machine path. The current `/usr/local/bin/python3` lacks Torch/ONNX/ORT. Keep the other runtime/fixture environments intact. **Exit:** the repaired verification path passes under its declared concurrency policy; training/export tests required for W09 actually execute with their dependencies. Every remaining skip has a reason and a named verification lane that covers the required behavior, or remains an explicit blocker. Default 218 versus optional 232 registrations are configuration differences, not a fixed universal test total. Training environment completion is not a prerequisite for unrelated packet/UI fixes.

### W04 — Finish and observe the macOS workflow

**Owners:** `libs/seam-standalone/`, `libs/seam-authoring-runtime/`, native controller/scene/inspector, TUNE/MIX/VOICE workspaces. Reuse `tests/test_original_singer_song_journey.cpp`, procedural install journey, Studio handoff and authoring coordinator tests. **Scope:** U23–U25/U32/U40; R1/R2/R7/R8/R11/R15/R20.

The current song journey already tests tuning, cancel/retry, undo/redo, exact save/reopen/export, copy-to-draft preservation, and Designer/Studio handoff. Re-run the relevant existing path first. Bound this initial UI work to UA-001–UA-020 plus the canonical quantitative targets: launch <3 seconds; preview median <150 ms/p95 <400 ms; 10,000-note interaction target 60 FPS with no ordinary selection/pan/zoom frame >50 ms; no callback allocations/locks; zero underruns at 48 kHz/128 frames during the 30-minute session; the stated save/autosave limits, atomic cancellable export and memory-growth limit. Use the exact [Usable Alpha contract](../product/USABLE_ALPHA_ACCEPTANCE.md), including workload caveats. Record an agent UI rehearsal separately. Other valid work remains in its U-unit/package rather than expanding this first milestone into a redesign.

**Bank route:** propose baking an original procedural voice into a generated **sample bank**, followed by proper production review, packaging and exact installation through W02/U14 and early W05a. This preserves the recording-free path while making the sample-concatenative requirement testable. Prepare the route and rights dossier for the user's decision; it is not automatic production/rights approval. The bank contract requires producer and reviewer to be **two distinct people** (`producerReviewerRelation: DISTINCT_PEOPLE`, `generatorSelfApproval: REJECT`). Obtain their actual registration and asset-scoped review; do not self-register the user as an independent reviewer or call an oscillator fixture a production bank. If the proposed route cannot satisfy the contract, present a concrete authorized-bank/recording alternative. Agent rehearsal can start with declared engineering assets; canonical acceptance waits for W05a's bank, without waiting for every W05b feature.

**Engineering exit:** a 30–60-second lyric song follows the installed-resource path through editing, cancellation, exact reopen, dirty recovery, missing-resource relink, master/stem export and independent audio inspection without hidden developer file repair. **Gate A exit:** one person completes the whole intended native journey unaided, without CLI or DAW, with the exact project/build/resource receipt. Run all UA-001–UA-020 with their own evidence, including production sample-concatenative hearing (UA-010), external audible verification (UA-019) and 30 minutes (UA-020). Require validator success **and** explicit canonical `gate.status = PASSED`, 20 `PASS` rows and valid evidence hashes. `verify_usable_alpha_contract.py` exit 0 alone can validate a coherent BLOCKED ledger; it is not acceptance. A procedural fixture proves only its applicable engineering steps. This short workflow milestone does not replace three full songs or five creators.

### W05 — Complete production and Voice Designer

**Owners:** `libs/seam-voicebank-production/`, `libs/seam-voice-design/`, Studio, authoring generation and voicebank CLI. **Scope:** U9–U14/U18–U22; R3–R6/R15/R19/R20.

**W05a — Early Gate A bank:** obtain the user's bank-route decision and distinct registered producer/reviewer, prepare admitted sources, and complete the minimum real production review, typed package and exact install required for W04. Bind the bank to its source permissions and actual review evidence. This artifact enables M1; it does not claim the whole production subsystem complete.

**W05b — Full production completion:** deliver the remaining original production and Designer exit obligations below. W05b and the macOS workflow may advance together after their particular dependencies are ready.

Preserve implemented intake, immutable takes, QC receipts, Accept preconditions, producer write locking and generation lifecycle. Implement the separate marker-only review transition and Studio alternative-take picker; repository/CLI selection already exists. Resolve original recipe persistence, identity, phonation, articulation/baking and generation obligations using the capability/resource matrix rather than silently enabling unsupported controls. Finish physical capture/import, retakes and interruption recovery with real authorized input.

**Exit:** native and CLI routes reproduce the same pinned candidate lineage. A generated original voice created without a supplied recording and an authorized real-input bank each pass review, package/install, unseen-lyric rendering and old-project reproducibility. Changing/deleting drafts does not mutate installed resources. Full inventory/range/two-style claims and acoustic judgments remain subject to W12; fixture-operator Accept is not a human approval.

### W06 — Close musical compilation, acoustic and language gaps

**Owners:** synthesis compiler/timing/selection/capabilities, rendering snapshots/jobs/cache, acoustic analysis/conditioning and Japanese/English/Korean phonemizers. **Scope:** U3–U8/U15–U17/U19–U20/U26–U28; R1/R2/R6/R7/R8/R15.

Start with original U6/U7 exit criteria and the audited limits, not a rewrite: target voicing, cross-family supported performance, context-complete chunk boundaries/invalidation, Raw provenance, manually regenerated superseded derivatives, and approximately ±512-sample voicing boundaries. Commit `5353b8af`, already in the audited source baseline, retargets measured voiced edges in PSOLA, SpectralClassic and Stretch, with synthetic regression evidence. Edges without usable stored marks retain source samples and are reported at dispatch; unvoiced regions deliberately retain source behavior. The previous audit's blanket source-pitch edge claim was stale and is corrected. Do not reimplement that fix. U16 still requires fixed-corpus measurements/listening, alternative analysis/synthesis trial comparison and classical qualification. Distinguish measured defects from research limits and contract-allowed backend incompatibilities. A mandatory expression cannot be removed merely because one carrier rejects it.

**Exit:** required intents reach the actual supported renderer, survive chunking/tempo/ownership changes and persistence, and meet fixed or prospectively frozen acoustic tolerances. Language correction, hints, syllable/context reconciliation and native entry work on real declared resources in all three languages. Exercise timing displacement, pitch/octave/voiced coverage, derivative lineage, cancellation, resource mutation and selective invalidation. Native-speaker and perceptual acceptance belongs to W12; procedural labels and analyzer numbers alone are insufficient.

### W07 — Complete expressive and assisted song creation

**Owners:** application commands, automatic-performance proposals, harmony/take workflow, native inspector/editing, and backend expression adapters. **Scope:** U23–U25/U38–U40; R2/R10/R11/R15.

Complete the original expression inventory with truthful resource capabilities and usable TUNE/MIX/VOICE surfaces. Preserve manually locked channels/ranges; reject stale proposals; make alternate takes, harmonies, partial regeneration and undo/redo predictable. Keep rule-based proposals labelled as such; learned-performance claims require W09/W12 evidence. Reuse shipped layout/paint tests and inspect actual interaction; avoid unrelated redesign work.

**Exit:** every required expression and assisted operation has command, persistence, audible-effect and native-interaction evidence on a supported qualified combination. Manual versus assisted unfamiliar tasks are counterbalanced in creator qualification. No automatic/manual vibrato doubling, lost ownership, stale audio or unexplained disabled control survives acceptance.

### W08 — Complete interchange and host authority

**Owners:** `libs/seam-interchange/`, native conversion lifecycle, CLAP adapter, host timeline/transport and `phase12c` engine. **Scope:** U29–U34; R12/R13/R15.

Implement the USTX voice-color-to-approved-style bridge with a pinned external fixture, explicit palette/per-phoneme mapping and truthful loss for unsupported heterogeneous choices. Preserve POSIX import safety and versioned SMF/USTX behavior. Re-run real OpenUtau and DAW exchange after the October rest/export changes; the September receipt is historical. Test Follow Host tempo/transport, invalidated Pending ranges, live expression and offline bounce against final exported audio.

**Exit:** real external files round-trip with declared losses and preserved timing/lyrics/expressions; final bounce cannot use stale tempo or missing required vocals. Native dialogs and cancellation/relink paths work. Harness results are engineering evidence; the installed nine-host matrix is completed in W13, including AUv2 where required. Windows safety/host evidence waits for W11's actual target.

### W09 — Produce the learned neural singer candidate

**Owners:** `tools/voice_model_training/`, model-production manifests, `libs/seam-neural-synthesis/`, `apps/seam-neural-worker/` and deployment closure. **Scope:** U35–U37 and neural U38/U39; R7/R9/R10/R15/R19.

Start the reproducible environment during W03. Inventory and reuse the existing external checkpoints/corpus/export receipts rather than asserting no model exists. Recheck rights/admission, split leakage, phonetic/linguistic coverage, acoustic profile, compatible vocoder, seeds and runtime identity. A generated-teacher experiment is a bounded diagnostic; it cannot create missing native-language intelligibility or constitute an original learned-singer qualification by itself.

Run the smallest end-to-end checkpoint/export/vocoder/held-out/installed-worker experiment that can falsify the current hypothesis before scaling training. Specify the dataset, hypothesis, fixed evaluation subset, maximum updates/time/storage and required improvement before each run. Reuse the repository's checkpoint, export and qualification tools. A machine-clean qualification command intentionally remains `UNRESOLVED` until independent listening; do not change it to emit `QUALIFIED` from synthetic success.

**Engineering exit:** an actual learned candidate with admitted sources renders held-out sung phrases through the installed worker, passes repeated-request determinism, finite/non-silent checks, conditioned musical behavior and prospectively frozen resource/latency/cancellation budgets, and has portable provider closure. **Acceptance exit:** W12's linguistic/identity/quality evidence and W13's two-platform installed proof close R9. Arithmetic ONNX graphs, a training loss curve or exported weights alone do not.

### W10 — Deliver the production character and identity

**Owners:** character package/loading/presentation, native/CLAP character surfaces, character asset provenance and readiness dossier. **Scope:** U41/U42; R14/R19.

Compare current shipped layers/performance states with the production character contract; old September claims of absent mouth assets are not automatically current defects. Finish the actually missing production artwork/model/rig/expressions/animations and identity/name/rights deliverables with their designated asset owners. Keep development assets labelled appropriately. **Exit:** approved production assets render required synchronized mouth/emotion/performance states and readable layouts on supported surfaces, with source provenance and rights evidence. Neutral fallback remains a truthful fallback, not final character acceptance.

### W11 — Finish platform, accessibility and support readiness

**Owners:** native paint/window/input/accessibility, platform audio/capture/crash, support/privacy/triage, packaging/installer/migration and dependency closure. **Scope:** U11/U29/U44/U47; R11/R13/R15/R17/R19.

Complete macOS physical audio, microphone, keyboard/IME, VoiceOver, recovery and support intake/triage exercises with consent and identity-bound receipts. Retain completed support implementation; qualify its operational path rather than rebuilding it. Include installed N→N+1 migration and interruption/update/revoke behavior, not only synthetic schema migrations.

When a Windows interactive host becomes available, implement the missing vector/text renderer and design-preferences backend: current non-Apple builds use `canvas2d_unavailable.cpp` and `design_preferences_unavailable.cpp`. WASAPI input/output, Win32 IME/window handling, UI Automation and `LockFileEx` locking already exist in source; execute and repair them on Windows rather than assuming they need rewriting. Complete actual editor interaction parity and plug-in views, signing-trust/installer lifecycle, dependency closure, Windows acoustic acceptance and four Windows host tuples. Backend-neutral Canvas2D conformance tests and reference rendering evidence may be prepared on macOS. No Windows validation claim follows from that preparation or source compilation. Keep `.github` unchanged and Windows TODO in README under the standing project instruction; record actual qualification results in the evidence ledger.

**Exit:** both actual native platform surfaces can perform their required creator workflow, physical accessibility/audio and support/recovery exercises pass, and release packaging is ready for final-candidate installation. Lack of a Windows host blocks only the Windows work and final full-scope GO, not macOS engineering.

### W12 — Freeze criteria and qualify resources and creators

**Owners:** the existing full-product profile/matrix and evidence contracts; voice/data/character producers; independent listeners, native speakers and creators. **Scope:** U2/U36/U42/U43; all R outcomes, especially R16/R19.

**W12a — Calibration and freeze:** prepare calibration early and resolve all 11 empirical categories with their existing owner units: acoustic boundaries, expression tolerances, pronunciation scoring, identity rubric, generation budgets, neural budgets, resource limits, cancellation budgets, reference machines, source volume, and reproducibility tolerances. The 175 specified cells are coverage obligations, not 175 automatic PASS tasks. Codex prepares the calibration packet, proposed methods/thresholds and exact affected coverage. The user confirms freeze authority by naming the actual U2/U19/U20/U35/U36/U43 decision owners and appointing appropriate independent reviewers; those owners and required reviewers approve the freeze. Record the actual people and reviewed revisions. The contract requires a resolved, reviewed frozen profile but does not itself name a person. Candidate authors and their technical agent reviewer cannot self-certify this empirical acceptance decision. Freeze the profile, methods/reference machines and actual resource × language × range × style × capability matrix before final qualification. Preliminary resource creation and measurements may continue before freeze; they are not accepted U42 output. Contract changes follow review and invalidate affected evidence; never lower a threshold to retroactively pass a failed candidate.

**W12b — Final qualification exit:** every required resource kind (real sample, procedural sample, original recipe, neural, dictionary and character) has exact identity, lineage, permissions and supported-combination evidence against W12a's frozen criteria. Complete at least 60 phrases per language, three complete songs spanning the required languages, five independent pre-GO creators, native-language review and real/generated producer journeys. Preserve all other normative criteria, including two reviewed styles; median steady pitch ≤30 cents, ≥90% of designated steady frames within 50 cents; 30 ms edits within one rounding sample; classical small-edit p95 ≤500 ms and the separate existing preview p95 ≤400 ms/median ≤150 ms workload. Report octave errors, voiced coverage and exclusions as specified. Review dry audio before mixes and retain failures as well as passes. Gate B's identity/intelligibility/phrasing decisions are independent judgments, not inferred from PCM.

### W13 — Freeze, sign, install, restore and authorize

**Owners:** release materialization/signing/install, host/standalone/soak evidence, full-product semantic gate, governed archive and authorized release roles. **Scope:** U45–U48, with all preceding requirements.

**W13a — Pre-freeze cleanup:** inspect abandoned experimental paths, default-off experiments and local WIP for actual release reachability. Remove only abandoned code from the release path, preserve useful experiments and historical evidence, and leave unrelated branches/stashes intact. Preserved experiments must be excluded from release builds or demonstrably unreachable with that boundary documented; they must not remain hidden release fallbacks. No automatic deletion of the unvoicing experiment or stop-status WIP follows from its name. Review the cleanup, rebuild and rerun affected verification; record its exact source before U47 freezes the candidate. An empty cleanup diff is acceptable only with a documented reachability disposition.

**W13b — Exact candidate:** build the resulting tree and bind source, resources, contract, profile, toolchain and dependencies to one candidate root. Produce signed/notarized descendants, independently install them, and capture actual installed-byte standalone/producer/creator/accessibility/neural/support/workload results on both platforms. Execute the nine required tuples: REAPER and Bitwig, each CLAP/VST3 on macOS/Windows, plus macOS Logic Pro AUv2.

Use the existing install, standalone, host and product-soak runners and governed archive audit. Prove every required 30-minute authoring and 120-minute soak/fault workload; shorter tests only prepare the harness. Restore raw evidence independently; recompute typed checks rather than trusting summary booleans. Tampered/omitted/stale evidence, changed assets or profile, revoked/paused lineage and any unresolved blocking/critical issue must prevent READY. A rebuild starts a new root; signing/notarization/installation are governed descendant transformations. Replay affected evidence according to actual lineage rules, never by copying PASS labels.

**Exit:** EB-001–EB-009 pass on the exact lineage and the existing authorized roles issue `EXTERNAL_BETA_READY`. Actual release authorization is a final human/operator action on a complete reviewable packet. READY must not start an unauthorized external cohort or become CLOSED/PUBLIC_ACTIVE by inference.

## Initial execution sequence

The first execution run should produce code and current receipts, not another general roadmap:

1. Sync safely and inspect current changed files; recheck whether another session already fixed W01 or integrated W02. Record the baseline in the existing ledger.
2. Deliver W01's packet/verifier repair and packet 003 with negative probes. Prepare one concrete handoff containing the packet, the Gate A production-bank route and missing corpus/rights inputs; present it to the user, and send it to another person only if authorized. Human listening can then proceed independently; do not perform subjective DSP tuning before that input.
3. Establish W03's reliable public-release verification policy before further integrations that depend on that suite. U21's budget correction is already integrated at `96ec8fbe`; retain its evidence and address only remaining unmet criteria. Run the existing installed-song/Studio journey and bounded macOS UI rehearsal; select the next actual creator-blocking defect from that observation. Unrelated packet/UI engineering need not wait for the test-policy repair.
4. Reconcile U14's three units, one verified commit at a time, then U45's executor. Begin the pinned training-environment work early when it does not contend with active validation. Do not require all model tests before unrelated packaging work.
5. Continue the highest-priority ready work package that advances a creator outcome or removes a real dependency. In parallel with code, prepare the exact input packets listed below. Do not wait for a global reclassification exercise or all listeners to return.

This order is a priority queue, not a universal prerequisite chain. If a packet input is missing, complete its safe verifier/schema work, record the missing artifact precisely, and proceed with ready song/integration work. No unavailable input is replaced by a fixture presented as production evidence.

## Required installed-product acceptance examples

The original plan's AE1–AE5 and definition-of-done item 3 remain mandatory. Automated regressions prepare these scenarios; final evidence must exercise the installed product and bind the relevant human observations to its identity.

| Original example | Required observable behavior | Responsible packages |
|---|---|---|
| AE1 — Original female voice | Save the recipe, generate, manually edit a unit, approve and install; sing an unfamiliar phrase. Later draft changes cannot alter the installed bank or an old song. | W05a/W05b; final qualification W12/W13 |
| AE2 — Pronunciation correction | A changed pronunciation cannot attach an old timing lock to the wrong phoneme; uncertain correspondence remains unresolved, and undo restores the prior state. | W06/W07; installed observation W13 |
| AE3 — Assisted performance | Regenerating a manually tuned phrase returns a proposal; accepting it preserves locked channels/ranges, rejects stale revisions and avoids automatic/manual vibrato doubling. | W07; creator qualification W12 |
| AE4 — Follow Host | A host-tempo change silences only invalidated Pending vocal ranges; final bounce cannot use old tempo or omit required vocals. | W08; installed host tuples W13 |
| AE5 — Full Beta gate | Old Beta PASS rows cannot bypass missing qualified neural/Designer evidence; READY remains distinct from CLOSED and PUBLIC_ACTIVE. | W02/U45; final negative and authorization checks W13 |

## Verification and integration economy

Use [CMakePresets.json](../../CMakePresets.json) and inspect an existing cache before changing its generator/options. After a `libs/` source/header change, rebuild all dependent binaries; the default in this checkout is `cmake --build build/release -j6` before focused CTest. Check timestamps and configured identities before attributing a failure to new code. Inspect unfamiliar CLI argument handling rather than blindly passing `--help`.

| Change surface | Existing focused verification to extend/reuse |
|---|---|
| Packet/verdict/provenance | Packet comparison/quality tests and explicit subprocess negative probes; new meaningful tests only for repaired behavior. |
| Production/package/generation | `seam_voice_generation_workflow_tests`, `seam_production_review_tests`, `seam_take_inspection_tests`, Studio generation/bank handoff, installed procedural/song journeys; register branch-specific install tests after integration. |
| Compiler/rendering/controls | Performance compiler/contract/job-context/snapshot/edit-preservation, renderer capability/singer route, phoneme timing, style blending, formant and synthesis-quality tests. |
| Native creator workflow | Existing original-singer journey plus real native observation; targeted authoring coordinator, design-shell input, TUNE/MIX/VOICE tests for changed behavior. |
| Languages/interchange/hosts | Language/Japanese pronunciation, USTX/SMF/interchange service, CLAP offline host/live events/timeline tests, followed by real external applications where acceptance requires them. |
| Neural | Pinned training/export tests, native worker/render/materialization/relocatability lanes, real held-out model candidate; arithmetic worker fixtures remain separately labelled. |
| Gates/support/release | External-beta/public-release Python suites, recovery/support, typed gate tamper tests, installed evidence and restored archive audit. |

Run focused checks after each coherent unit. Run the relevant broader suites once at a subsystem/milestone integration point; run actual sanitizer/race checks for changed memory/concurrency boundaries. Complete the original plan's full Debug/Release and ASan/UBSan/TSan verification obligations before the final candidate. Run the full supported Release matrix after substantial integration and before freeze. Repeat a broad run only after changed code/configuration, a failure, or a specific unresolved concern. Never repeatedly run the entire matrix merely while waiting for a person.

A receipt names the actual configuration, interpreter, runtime/assets, selected tests, skips and raw artifact hashes. Do not promise a fixed CTest count. A timeout followed by an isolated PASS remains two separate observations until the supported suite run succeeds. Any required dependency/environment skip prevents its corresponding claim of completion.

## External inputs and scoped stop rules

Prepare these requests during the first execution cycle, with concrete files to review. The lead records the responsible role and exact unblock condition; it does not invent a person's name, credential or approval. The user recruits outside listeners, native speakers and creators; Codex prepares their packets and records supplied evidence. Contacting those people requires explicit authorization.

| Required input | Prepared by Codex | Supplied or judged by | What waits; what continues |
|---|---|---|---|
| Listening and native-language decisions | Corrected hash-bound dry audio, matched cases, named rubric and proposed decision form | Independent listeners/native speakers | Subjective acoustic repair/qualification waits; objective defects, UI and infrastructure continue. |
| Unaided creator sessions | Pinned installed app/resources, task script, evidence capture and privacy guidance | Independent creators/operators | Gate A and later five-creator acceptance wait; automation and observed engineering repairs continue. |
| Gate A bank route and review registration | Proposed generated-sample-bank route, rights dossier, exact candidate and review form | User chooses the route; two distinct registered people act as producer and reviewer | W05a's production approval and canonical Gate A wait; bank tooling and agent rehearsal continue. |
| Corpus/model/resource rights and production assets | Inventory, source/label admission and missing-permission checklist; bounded model experiment report | Rights holders/authorized producer/reviewer and asset owners | Use/qualification needing those rights waits; existing authorized fixtures and unrelated engineering continue. |
| Empirical profile and matrix decisions | Measurements, proposed thresholds/methods and exact affected coverage | User names U2/U19/U20/U35/U36/U43 decision owners and independent reviewers; they approve and record the reviewed freeze | Final scoring waits; labelled calibration and implementation continue. Candidate authors/technical agents cannot self-certify. |
| Windows machine, DAWs and interactive session | Build/dependency/backend/installer checklist and reproducible workload | User/operator with the required environment and licenses | Windows-specific implementation/acceptance and full GO wait; macOS remains active. |
| Signing, notarization and externally anchored immutable archive | Complete unsigned candidate, payload inventory, provenance, archive retention/restore design and exact signing/anchor request | Existing authorized release roles and infrastructure owner | Signed-installed/final GO waits; complete all safe candidate preparation first. A local `/tmp` directory is not the required immutable external anchor. |

First validate the measuring tool against a known-answer fixture; an unvalidated analyzer result is not yet a product defect or a counted research hypothesis. For each confirmed failed acoustic/model criterion, declare the expected measurable change and fixed comparison before editing. Permit at most two evidence-driven repair hypotheses against that failure before a scoped architecture/input review. The limit prevents an indefinite tune/train loop; it is not a product-quality waiver. The lead and technical reviewer may authorize a further bounded hypothesis when new evidence justifies it; report every third-or-later hypothesis and its rationale in the next user handoff. This notification is not another permission gate. Ask the user only for missing external input, new authority, or a change to settled product decisions. Preserve previous results. Do not repeat whole training runs or change multiple acoustic variables without a falsifiable comparison.

Stop the affected action immediately for lost provenance, unsafe migration, unexplained installed identity drift or an unavailable required authority. Preserve outputs and use a focused revert/new commit or restore from a verified backup when necessary; never destroy unrelated work. Continue independent ready units. If no independent task remains executable, report the exact required input and truthful partial completion; follow the active goal tool's blocked-state rules. Never mark the full goal complete because of a time/token budget, an external wait, a macOS-only milestone or a passing synthetic gate.

## Review and handoff contract

Request independent review at high-risk integration boundaries (package/install identity, compiler/resource contracts, neural admission, migration, typed gates) and each milestone before acceptance claims. Routine reversible fixes do not require a new human permission round. Resolve actionable reviewer findings and rerun affected checks before integration; an implementer's “done” message is not proof of landing. Missing technical review stays explicit; it is not silently treated as approval.

Each handoff states: latest pushed hash and remote equality; newly observable behavior; checks actually run and evidence locations; remaining uncommitted/unpushed work; unresolved acceptance/input; and the next ready small unit. Update only the relevant ledger rows. Use counts of verified exit criteria with denominators and evidence classes if progress is requested; the old 54% planning index is not the goal's success metric.

The independent technical review approved publication after the prescribed corrections, with no further round required. The discussion retained full-scope READY as the finish line; it rejected treating later cohort closure as a pre-GO requirement, blanket duplicate full-suite runs and a second project-wide traceability ledger. It added the early reviewed production-bank dependency, explicit installed AE1–AE5, freeze decision owners including U2, calibration/final-qualification ordering, Windows implementation boundaries, release-path cleanup and external archive requirements. It also corrected stale U21/U16 work so an executor does not repeat already integrated behavior. Reviewer unavailability, human recruitment and bounded research escalation now have explicit handling. The reviewer found no defect in the integrated U21 budget correction on source/test inspection; acceptance remains open.

Document verification: all 48 original U-units have a scheduling owner across 13 work packages; all five installed acceptance examples are mapped; 35 relative links across the plan and updated audit documents resolve; the JSON receipt parses; stale pending-U21/blanket-U16 instructions are removed; `git diff --check` passes. These checks establish document consistency, not product completion. This planning turn ran no product builds/tests and did not activate a development goal. The U21 runtime results above are attributed to the integrating session; the earlier audit's runtime receipts retain their original identity and date.
