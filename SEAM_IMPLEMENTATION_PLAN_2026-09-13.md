---
title: SEAM Implementation Plan - Integrated Singer Delivery
date: 2026-09-13
status: execution-specification
baseline_commit: 12ad16a0c7566f5ea2054fcd581c90f8dce02731
scope: R1-R20 and Full-Scope U1-U48, without reduction
execution_started_by_this_document: false
---

# SEAM Implementation Plan: Integrated Singer Delivery

> **Execution-order update, September 15:** follow [SEAM Revised Development Plan](SEAM_REVISED_DEVELOPMENT_PLAN_2026-09-15.md) for the current near-term sequence: close the growl checkpoint, retain direct-procedural listening evidence, deliver expression editing, make evidence-led repairs, and finish procedural distribution while neural feasibility proceeds independently. This document's R1–R20/U1–U48/U60 obligations and detailed acceptance criteria remain in force. Its baseline and “begin M1.P1” handoff are historical, not instructions to restart completed work.

## 1. Purpose and authority

This document turns the six-milestone proposal into a development specification that an implementer can follow. It defines the next code changes, their ordering, the data contracts they must preserve, test cases, runnable deliverables, and the evidence needed to close each outcome.

**Delivery goal:** a creator can sculpt an original female singing voice without supplying a recording, or use authorized real/generated source material; edit that material into reusable singer resources; write and tune expressive Japanese, English, and Korean songs; and use the resulting product dependably in standalone and the required DAW environments.

This is not a new product definition. Authority remains:

1. The user's settled full-scope requirements.
2. [Virtual Singer Feasibility and Code Roadmap, revision 2](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/VIRTUAL_SINGER_FEASIBILITY_AND_CODE_ROADMAP_2026-09-05.md).
3. [The original Full-Scope Beta GO plan and its acceptance obligations](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md).
4. This document's remaining-work decomposition and proposed implementation sequence.

The [September 12 progress report](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/SEAM_PROGRESS_AND_REVISED_ROADMAP_2026-09-12.md) explains the assessment behind this plan. It is not repeated here as another audit.

All R1–R20, Full-Scope U1–U48, original verification scenarios, and preserved production-plan U60 obligations remain. A Japanese pilot, diagnostic model, successful regression run, or technically valid resource package is not Full-Scope Beta GO. Later external-cohort closure and public activation remain later operations.

**Document conventions:** paths in code spans are repository-relative; clickable source references use the current workspace's absolute paths. `Existing` means inspected at the baseline. `New` means a proposed implementation artifact, not a file or command that already works. Package IDs such as M1.P1 belong to this execution specification and do not rename original U-IDs.

## 2. Starting state and execution rules

### 2.1 Baseline to preserve

The current checkout remains at `12ad16a0c7566f5ea2054fcd581c90f8dce02731`, on `codex/production-readiness-completion`. At the start of this planning turn, the only untracked root artifact was the September 12 report. No production source changes were present.

The immediately preceding review ran the configured macOS Release build and all registered CTest targets: **121 passed, zero failed; 869 core cases passed**. Those results belong to the September 12 source/run, not to a new execution of this plan. This planning turn does not rerun or re-certify them. The plan authoring does not start an implementation goal, publish resources, commit, or push.

Keep the existing score/performance compiler, immutable snapshots, repository generations, review/publication owners, typed commands, deployment checks, and release audit. Add the missing capability through these owners.

### 2.2 Unit of execution

- Work toward one **milestone outcome**, through its three implementation packages. Do not ask the owner to restart work after every internal boundary or test addition.
- Use one-to-three-week integrated outcome windows as a planning cadence, not a promised completion date. Keep an audible/runnable checkpoint every few working days; re-estimate research-heavy model/resource work from pilot results.
- Keep implementation commits small enough to review and recover. A large milestone does not require one large commit.
- The preferred concurrency is one primary integration milestone and one independent data/model track. More people do not justify simultaneous edits to shared schema, CMake, or native controller ownership.
- A missing reviewer, source, Windows machine, or signing credential blocks the relevant acceptance only. Finish safe technical work, record the exact remaining evidence, and continue an independent package.
- Do not fabricate rights, independent judgments, measured thresholds, trained models, or installed-host results to avoid an acceptance dependency.
- Do not automatically rewrite this plan after every implementation turn. Record brief current execution status separately once implementation begins.

### 2.3 Three different completion claims

| Claim | Meaning | What it does not mean |
|---|---|---|
| Package implemented | Named production path, focused tests, and connected technical demonstration work | All related U-units or musical quality are accepted |
| Milestone outcome demonstrated | Its specified user journey works with retained artifacts; remaining qualification is explicitly identified | Full-Scope Beta GO, or permission to drop later dependencies |
| Unit/release accepted | Every original obligation for that unit/candidate has the required valid evidence | A blanket acceptance of another platform, resource, or later build |

M1 can demonstrate a useful pilot while M3/M4/M6 still own full language/style/resource qualification. Do not mark all 16 M1-associated units accepted merely because the pilot succeeds.

An entry dependency means the code/data contract needed by the next package is available. It does not require every ancestor's independent listening or other-platform acceptance to be finished. For example, M2.P2 can integrate on a working macOS supervisor while Windows execution evidence remains pending. This rule does not permit bypassing invalid inputs, missing source authority, or an unsafe execution boundary.

## 3. Fixed execution decisions and measured decisions

### 3.1 Decisions to use now

| Topic | Working decision |
|---|---|
| First audible outcome | Original recipe → generated inventory → Studio edits → independent review → installed sample bank → unfamiliar 16-bar song |
| Pilot identity | `seam-pilot-01`, an internal original identity; not a final character name or a claim of qualified female identity |
| Pilot language | Japanese first, retaining the mandatory English/Korean completion path |
| Pilot resource shape | Start with style `neutral` and planned MIDI layers 60, 66, 72; target MIDI 60–72 for the diagnostic pilot only. Preserve existing resources' `original`/other style IDs without silently renaming them. |
| Pilot range status | Requested range is unassessed until measured. Never populate a range PASS merely to let generation begin. |
| Sample reference path | Use the existing PSOLA path as the first controlled comparison; retain Raw/spectral/stretch behavior and truthful applicability. Final default selection follows measured quality, not this convenience choice. |
| Neural integration | First-party native worker using the admitted DiffSinger-compatible acoustic/vocoder export family and a pinned CPU inference runtime. CPU operation on both target platforms remains required. |
| Resource loading | Immutable content identities; first-party helper deployment selected by the application, never by a voicebank |
| Audio ownership | Compile score/timing once; keep owned output distinct from context; apply final F0 and sample-domain gain once |
| UI ownership | Native actions call shared application/authoring/production services; no parallel UI-only synthesis or repository mutation engine |
| Release | Existing full-product gate and exact restored-candidate audit; no second GO authority |

The pilot range/layers are implementation defaults, not newly invented acceptance thresholds. M1.P1 must display their unassessed status and allow a versioned pilot revision if measurements show they are unsuitable. Full declared range and the required two reviewed styles are qualified later; a narrow pilot cannot silently become the final scope.

### 3.2 Decisions that must come from experiments

| Decision/output | Owning package | Required result |
|---|---|---|
| Whether the source-filter voice is intelligible and retains the intended identity | M1.P2/P3 | Dry held-out audio, identified failure classes, and the next acoustic change if needed |
| Exact model/runtime/export revisions | M2.P2/P3 | Pinned intake records and an actually compatible nonzero inference candidate |
| Source volume and training strategy | M2.P3 | Learning/coverage evidence comparing the selected lawful data strategy; no unsupported hours estimate |
| Dictionary/resource selection | M3.P1 | Exact versioned assets, language coverage, matching phone inventory and native-language review |
| Final acoustic, expression, identity and machine budgets | M6.P1, using earlier pilots | Frozen values and reference profiles before held-out final scoring |
| Final character and resource approval | M4.P3 / M6.P2 | Reviewed final assets, identity consistency and applicable permissions |

These are specified work outputs, not reasons to stop before writing useful code. Do not spend money, acquire unrelated accounts, record people, or upload source/model material without the required authority.

## 4. Concrete prerequisites discovered in the current code

The implementation must account for the following, rather than following the older file list as though every boundary were already complete.

| Current boundary | Required implementation consequence |
|---|---|
| `UnitAssignment` and `TakeRecord` identify coverage/pitch but have no style owner; publication explicitly rejects multi-style manifests | Introduce style-owned production identity and migrate repository, expectations, review packets and Python mirrors together. Do not just remove the rejection. |
| Python inventory generation accepts only `ja`, validates two/three pitch layers, and creates assignments keyed by coverage/pitch | Add versioned language/style-aware profiles and deterministic assignments; preserve legacy interpretation. |
| The default profile contains a range PASS and normal generation requires PASS | Separate a draft requested range from measured qualification; a diagnostic generation workflow must not manufacture musical acceptance. |
| `ArticulationPlan::compileRecipe()` rejects gestures outside their owning note; released-stop admission is limited to `p/t/k` | Add real phonation context and the missing phone classes, not only recipe labels. |
| Every generation expectation binds the complete producer-state hash | Do not prepare an entire campaign against one state and then expect sequential collection to remain current. Use receipt-bound, just-in-time batch preparation. |
| `seam_neural_synthesis` links `seam_authoring_runtime`, which already links `seam_rendering` | Extract the low-level process runner before adding rendering → neural linkage; otherwise a dependency cycle is introduced. |
| `runBoundedHelperProcess()` is a POSIX implementation and explicitly rejects unsupported platforms; its contract requires exclusive child reaping | Implement a Windows runner and qualify plug-in-host process supervision. A Windows deployment descriptor is not Windows inference support. |
| Neural resources currently contain one frozen opaque blob; the worker launcher supplies no real model bundle | Add typed bundle admission, worker-visible verified assets and an explicit protocol/resource migration. |
| Neural frame transport is version 1, with request metadata kinds v1 and v2 already in use | Do not reuse an existing version number for different bundle semantics. |
| Classical capabilities are a renderer-only table with seven advanced controls false | Add resource-conditioned capability resolution and actual consumers for mandatory controls. |
| Follow Host final preparation explicitly rejects a missing complete authoritative tempo map | Add an acquired and versioned range/map; instantaneous BPM cannot certify future tempo events. |
| Character resources describe six operational states | Add a separate performance presentation contract; do not overload Rendering/Complete with mouth animation. |

Source anchors: [producer types](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voicebank-production/include/seam/voicebank_production/project.hpp:114), [publication guard](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voicebank-production/src/repository_candidate.cpp:475), [inventory generator](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/voicebank_script_generator/inventory.py:208), [profile validation](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/voicebank_script_generator/profile.py:59), [neural dependency](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/CMakeLists.txt:338), [process contract](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-authoring-runtime/include/seam/authoring/helper_process.hpp:31), [worker launch](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-neural-synthesis/src/neural_phrase_backend.cpp:225).

## 5. Milestones, packages, and dependencies

| Milestone | Package sequence | Main deliverable | Primary original-unit ownership |
|---|---|---|---|
| M1 Original singer loop | P1 production/inventory identity → P2 connected articulation/generation → P3 native review/install/song | Reproducible original-voice pilot and separate real-input path | U6–U16, U18–U22 |
| M2 Real neural singing | P1 process/bundle boundaries → P2 actual inference/render integration → P3 data/training/export | Real original-model candidate inside ordinary SEAM song rendering | U35–U37 |
| M3 Multilingual expression | P1 languages → P2 styles/advanced DSP → P3 generated performance | Understandable multilingual singing with audible editable expression | U17, U26–U28, U38–U39 |
| M4 Complete creator product | P1 native tuning/layout → P2 interchange/takes/harmony → P3 character/resource assembly | Complete native creator and producer workflows | U23–U25, U29–U32, U40–U42 |
| M5 Installed reliability | P1 host timing → P2 installed execution/live events → P3 recovery/support | Exact standalone/two-platform/nine-host candidate | U33–U34, U44, U47 |
| M6 Qualification and GO | P1 criteria freeze → P2 corpus/listeners/creators → P3 restored release audit | Evidence-backed authorized `EXTERNAL_BETA_READY` | U43, U45–U46, U48 |

U1–U5 remain the baseline. Ownership accounts for all units exactly once: **5 + 16 + 3 + 6 + 10 + 4 + 4 = 48**. Dependencies do not transfer or duplicate acceptance.

Start M1.P1/P2 as the primary lane. M2.P1 and initial source preparation from M2.P3 can proceed independently. Bring the minimum M4 controls into M1.P3 or M3 when their workflow needs them. Start host smoke tests early; final host acceptance waits for the actual candidate.

### 5.1 Target code dependency direction

```text
core / time / domain / formats / platform process services
                 ↓
synthesis contracts + voice design + neural synthesis/deployment
                 ↓
rendering snapshots / pipeline / cache / scheduling
                 ↓
authoring runtime / producer orchestration
                 ↓
native UI / CLI / standalone / plug-in composition roots
```

This is a target dependency direction, not permission to move every module. M2.P1 makes the one necessary process-runner extraction. Shared schema and CMake changes are serialized by their active package owner.

## 6. M1: Complete the original singer loop

### M1.P1 — Make inventories and producer assignments usable at singer scale

**Entry:** preserved baseline; no new voice or review required to implement the data path.

**Existing owners:** `tools/voicebank_script_generator/profile.py`, `inventory.py`, the CLI shim in `tools/voicebank-script-generator/main.py`; production `project.hpp`, `project_codec_encode.cpp`, `project_codec_decode.cpp`, `project_codec_decode_records.cpp`, `project_codec_validation.cpp`; `repository.cpp`, `repository_operations.cpp`, `generation_expectation.cpp`, `repository_candidate.cpp`; and Python draft/candidate validators under `tools/external_beta/`.

**Required changes:**

1. Define a production identity key containing language, style ID, coverage key and MIDI layer. Alternate/retake ID is a separate identity: multiple takes may compete for one assignment. Add explicit language/style ownership to takes and assignments, or to an immutable project-level language with per-assignment style; use the latter for a single-language producer workspace.
2. Introduce producer schema **4** for these identities: schema 2 is the draft/source-aware contract and schema 3 already carries source-quality assessment data. Preserve readers for 1–3 and their original digests; write a new generation for migration, never rewrite historical generation bytes.
3. Migrate a legacy assignment only with a uniquely captured language/style binding from its authoritative inventory/manifest. If the binding is ambiguous, preserve it as unresolved and require an explicit migration action; do not choose the currently selected style. Old approvals remain historical and cannot silently certify the new review basis.
4. Add inventory schema **2** with language, styles and canonical per-style row identity. Iterate style × sequence × layer × take deterministically. Derive production assignments from style × sequence × layer, not the current style-free pair. Update CSV fields, digests, retake grouping, draft creation, validation and C++/Python parity together.
5. Separate requested range from a captured range assessment. Draft generation can use unassessed requested bounds; qualification/publication policy still requires applicable reviewed evidence. Do not reinterpret the legacy default PASS as a newly observed range measurement.
6. Bind style/language into `GenerationImportExpectation`, stale-collection checks, take lineage, candidate unit bindings, review basis and materialization. Reject collecting a `soft` result into a `neutral` assignment even when the PCM hash happens to match.
7. Keep multi-style publication rejected until every assignment/review/manifest consumer uses the new key. Once that parity is implemented, enable actual multi-style candidate assembly with complete per-style coverage. Merely editing `manifest.styles` is insufficient.
8. Add the internal pilot profile and explicit planned range with unassessed status. Generate a coverage report showing missing phone classes and transitions before running thousands of jobs.

**New artifacts:** `assets/pilots/seam-pilot-01/` for a development-only profile/recipe/score specification; `tests/test_production_style_identity.cpp`; shared parity fixtures under `tests/fixtures/production-style-v4/`. Do not put fake review records or release keys into the pilot assets.

**Regression acceptance:**

- Identical coverage/layer in two styles yields two distinct assignments and take identities.
- Editing style or inventory invalidates affected generation expectations and reviews, without losing unrelated history.
- Legacy single-style migration succeeds deterministically; ambiguous legacy multi-style data stays unresolved.
- C++ and Python accept/reject the same schema-4 fixtures, including cross-style retakes, duplicate keys and missing rows.
- Draft unassessed range allows bounded generation, but cannot create a qualified range or release eligibility.
- A real multi-style candidate cannot omit a mandatory assignment or reuse an approval for changed material.

**Exit:** one deterministic pilot inventory can create a producer workspace with distinct missing assignments, and both readers agree on its identity. This package is not an audible singer or approval milestone.

### M1.P2 — Generate connected, intelligible material through the real render path

**Entry:** M1.P1 identity contract; generation can start on an already valid single-style fixture while the migration is integrated.

**Existing owners:** `libs/seam-voice-design/` (`voice_recipe`, `articulation_plan`, `articulated_stream`, `phonation_source`, `vocal_tract`, consonant sources); synthesis `performance_compiler.cpp` and `phoneme_timing_plan.cpp`; rendering `render_snapshot.cpp` and `render_pipeline.cpp`; authoring `generation_job.cpp` and `generation_batch.cpp`.

**Required changes:**

1. Introduce an explicit articulation-source description for oral vowels, nasals, frication, released/voiced/unreleased stops, affricates, liquids/glides, silence and applicable breath/closure events. Bind each supported phone/style to a source type and parameters. Do not represent an unsupported voiced consonant as renamed unvoiced noise.
2. Replace the assumption that phonation starts/ends at every owning note. Derive phrase context from compiled timing, including admitted pre-onset and release intervals. Keep `PhonemeKey` ownership on the original note even when its acoustic gesture extends into context.
3. Separate **ordered linguistic spans** from **acoustic transition overlap**. Preserve ordered phoneme timing; represent coarticulation as a bounded transition plan between sources rather than weakening the global overlap check. Define how voicing, closure, burst, formant interpolation and aspiration compose in that transition.
4. Preserve source/filter history through chunk boundaries. Render the same owned range from the same full context with identical phase, noise seed and filter state. Context may extend outside the published range; it must not shift note melody or publish extra frames.
5. Version changed recipe semantics and renderer/cache ABI. Decode old recipes with their old meanings; new phone models require explicit opt-in fields. Reopening a saved song must not silently reinterpret an old recipe under a new algorithm.
6. Add a shared **inventory generation planner** that turns admitted inventory rows into small score/timing jobs and calls `prepareGenerationJobFromScore()`. It owns job/score construction, not a second DSP implementation. Generated score IDs, requested phone sequences, recipe/style/layer and timing-template revision must be deterministic and retained.
7. Extend bounded batch orchestration to a resumable campaign made of existing bounded batches. Keep each batch's existing job/frame limits. The campaign freezes job templates, inventory, recipe and source policy, but prepares each batch's generation expectations only once, immediately before that batch runs. After collection, retain the exact committed generation/hash receipt; only the next unprepared batch may capture that new state. Preparing every batch at the initial producer state would make later collection stale after the first commit.
8. Make campaign advancement an explicit prepare → render → collect transaction sequence, with durable phase receipts and no approval side effect. Restart reuses only hash-valid completed jobs and their original expectations. An uncertain commit is resolved through the existing receipt/recovery mechanism before advancing. Require the producer state to equal the last known campaign receipt before preparing the next batch; an external edit returns a conflict. Retake or changed inventory/recipe/policy creates a new identity, never a rewrite of an old expectation.
9. Validate aggregate job count, estimated frames/disk use and cancellation before work; reject over-budget campaigns rather than silently truncating inventory or arbitrarily raising limits. Bound both active memory and cumulative retained output.
10. Produce a small held-out phrase set before the full inventory. Compare dry audio and phone coverage. If articulation is unintelligible, repair the relevant source/transition method before multiplying the same defect across the bank.

**New interfaces/files:** `inventory_generation.hpp/.cpp` and `generation_campaign.hpp/.cpp` under `libs/seam-authoring-runtime/`; a `TransitionPlan` within `libs/seam-voice-design/`; `tests/test_articulation_context.cpp`, `tests/test_generation_campaign.cpp`, and focused CMake targets. Names are proposed; responsibilities and dependency direction are required.

**Regression acceptance:** onset before note start; nasal/coda after nucleus; voiced stop versus unvoiced stop; affricate sequence; liquid/glide transitions; standalone syllabic nasal; breath/silence; melisma without unintended retrigger; a short note too small for a requested gesture returning an actionable error; same-context chunk/whole equivalence; cancellation and resume after partial output; changed-recipe stale collection.

**Exit:** the pilot recipe produces multiple phonetic classes and connected new phrases through ordinary immutable rendering, and one inventory campaign completes/restarts without manual JSON editing. Retain raw audio and a list of unsupported or poor-quality phone contexts. A waveform being nonzero is not the intelligibility criterion.

### M1.P3 — Finish native editing, review, installation, and the new-song handoff

**Entry:** generated pilot material; a separately authorized real take for the real-input acceptance path when available.

**Existing owners:** native `voice_designer_model.cpp`, `voice_designer_session.cpp`, `voice_designer_audition.cpp`, `voicebank_studio_sample_review.cpp`, production-edit/source-quality/manifest-draft files; `apps/seam-voicebank-studio-native/main.cpp`; voicebank CLI; producer candidate services; authoring installer and export services.

**Required changes:**

1. Native actions must cover select/open recipe, audition, save, select producer inventory, plan/run/cancel/resume generation, inspect collected takes, edit markers/conditioning, request review, apply supplied decisions, publish, package/install, and open a new song using the resulting bank. Reuse existing actions where complete.
2. Add only the missing orchestration and views. CLI and native entrypoints call the same generation and repository services; neither edits repository JSON directly.
3. Display expected/current inventory, recipe, style, generation, missing coverage and stale status. Cancellation retains confirmed material; the view must distinguish committed, uncommitted and committed-but-durability-uncertain publication outcomes.
4. Complete a marker edit, rejection, retake, updated review packet and explicit new review. Review invalidation follows content/metadata changes; a previous decision cannot float to changed units.
5. The existing `publish-sample` action remains a candidate publication, not signing or installation. Invoke existing packaging and installer services as distinct steps with their exact resulting identities.
6. Extend the existing installed-song regression beyond its one-unit fixture. After installing, make the producer workspace and generation inputs unavailable to the new song. Export dry vocal/master/stem from the installed bank, save/reopen, and verify identical resource binding and expected audio.
7. Perform the same source-to-bank path with one authorized recorded/imported take, including capture stop, clipping/rate inspection, edit and retake. A hardware-unavailable run records that boundary; it does not claim microphone completion.
8. Fix any layout issue that prevents this pilot workflow now. The broader M4 layout pass must not be used to defer an unusable primary button, unreadable review identity or inaccessible cancellation action.

**Extend:** `tests/test_sample_review_cli.cpp`, `tests/test_studio_sample_review.cpp`, `tests/test_voice_designer_workflow.cpp`. **New:** `tests/test_original_singer_workflow.cpp` and CTest `seam_original_singer_workflow_tests`; a development-only pilot driver using those public services, with separate explicit review actions.

**Automated exit:** deterministic source→generation→edit→candidate→package→install→new-song lifecycle with test identities, clearly labeled as a fixture; one failed/stale/retry path; immutable old-song behavior after draft changes.

**Human/resource exit:** a genuine reviewer can inspect the pilot and record a supplied decision; the original voice sings the unfamiliar 16-bar song, and the real-input journey is observed. If this evidence is unavailable, mark it pending and continue M2; do not invent an independent reviewer account or auto-accept units.

**M1 deliverable:** recipe/profile, inventory, generated and edited material, campaign state, review packet/receipts, installed-bank identity, reopened song, dry/master/stem audio, measured pitch/timing/coverage, and a short defect list. No Beta claim follows from M1 alone.

## 7. M2: Deliver real neural singing

### M2.P1 — Establish a non-cyclic process boundary and an admitted model bundle

**Entry:** baseline interfaces. Independent of the M1 acoustic-quality outcome.

**Existing owners:** `CMakeLists.txt`; authoring `helper_process.hpp/.cpp` and reading callers; neural `worker_protocol`, `model_contract`, `neural_phrase_backend`, `deployment_descriptor`; synthesis `singer_resource.hpp/.cpp`; rendering snapshots; `tools/phase13a/neural_package.py` and associated package tests.

**Required changes, in order:**

1. Move the bounded process primitive into a lower-level platform owner, proposed `libs/seam-platform/include/seam/platform/helper_process.hpp` and `src/helper_process.cpp`. Preserve the current request/output behavior and deadlines. Migrate Japanese reading and neural callers; a temporary forwarding authoring header may preserve source compatibility, but there must be one implementation and no duplicated child-reaping owner.
2. Remove neural → authoring-runtime linkage. Declare neural's actual dependencies explicitly: synthesis contracts, formats/core, platform process services, distribution/deployment verification. Then rendering can consume neural without the existing rendering → neural → authoring → rendering cycle. Add a dependency-direction check and inspect a fresh CMake link graph.
3. Implement the Windows process backend with explicit executable path, correctly quoted arguments, bounded pipes, cancellation/deadline termination, owned process-tree cleanup, handle isolation and exit-status reporting. Propagate configured resident-memory/CPU ceilings where supported; do not describe the existing POSIX best-effort sampling as a security sandbox.
4. Qualify the process supervisor in plug-in hosts. Preserve the current exclusive-child-reaping constraint until a platform-specific solution is proved. If a host interferes with direct-child supervision, use a first-party platform broker whose child lifecycle is outside that host, and include its installer/signing/IPC trust boundary in M5. Do not change host-global signal handlers or reap unrelated children.
5. Introduce a **data-only neural bundle** in the lower-level resource contract. It binds a canonical manifest, acoustic graph, vocoder, optional required variance graphs, vocabulary, configuration and any admitted tensor assets. Resource types must not include ONNX Runtime headers or depend upward on authoring/rendering.
6. Add a neural admission factory that returns an immutable prepared handle containing the verified bundle, `ModelContract`, `NeuralVocabulary`, graph compatibility metadata and execution identity. Build it outside the audio callback. Share immutable backing across phrases rather than copying a whole model per snapshot.
7. Bind graph sample rate, hop size, mel channels/scale/range/layout, vocabulary/token conventions, language/style/speaker mappings and enabled conditions. Reject acoustic/vocoder pairs that merely happen to have individually valid files but disagree on feature representation.
8. Keep resource/model identity distinct from the signed first-party helper deployment identity. The application chooses executable/runtime dependencies; a bank cannot supply a command, dynamic library path, custom operator or executable script.
9. Retain old opaque model resources as legacy data with explicit unsupported execution until migrated; do not reinterpret the old blob as a valid bundle. Add project/resource migration and cache invalidation for the new bundle kind/revision.
10. Extend worker launch with an application-selected, canonical bundle location plus its expected manifest/content identity. The child re-admits the exact bytes it actually loads. Parent-side pathname hashing alone is insufficient if the child later opens changed bytes.

**Versioning decision:** keep transport frame version 1 unless its binary framing changes. Existing request metadata v1/v2 retain their meanings. Introduce request metadata **v3** for an explicit `bundleContentHash` and bundle-conditioned execution identity; introduce a corresponding response binding. The production launch contract can be `--seam-neural-worker-v2`, with a versioned helper manifest/deployment descriptor admitting it. Never make the v1 test probe accidentally satisfy the production-worker contract.

**Proposed bundle contract fields:**

| Field group | Required semantics |
|---|---|
| Identity | Format/schema, resource ID/version, canonical manifest digest, bundle content digest |
| Files | Unique canonical relative paths, role, exact byte length and SHA-256; aggregate and per-file bounds |
| Features | Sample rate, hop size, channel count, mel representation, tensor names/dtypes/layout and supported dimensions |
| Language/style | Exact vocabulary/dictionary compatibility and style/speaker mapping; unknown values rejected |
| Controls | Supported conditioning inputs and their units/ranges; absent controls are not advertised |
| Execution | Export family/revision, permitted opsets/operators, provider/runtime compatibility, resource ceilings |
| Provenance | Dataset/split/preprocessing/training/export identities and applicable source/model permission references |

**Admission policy:** parse under bounded input/depth/node/tensor limits before constructing an inference session. Bound dimension products with checked arithmetic. Reject unknown custom operators, undeclared external tensors, redirected/escaping paths and unsupported dynamic dimensions. The first admitted export family should use self-contained graph files; support external tensors only after their transitive bytes and loading behavior are explicitly bounded and frozen. This is an input-admission policy, not a claim of comprehensive OS sandboxing.

**New owners:** neural `model_bundle.hpp/.cpp`, `graph_contract.hpp/.cpp`; platform process implementation; synthesis data-only bundle representation; `tests/test_neural_model_bundle.cpp` and `tests/test_neural_process_supervision.cpp`.

**Exit tests:** dependency graph is acyclic in this path; existing reading/process regressions remain green; Windows execution/cancel/pipe-limit tests pass on Windows; graph/vocoder mismatch, overflowed shape, duplicate role, missing tensor, changed loaded bytes, wrong helper and legacy probe are rejected. An admitted bundle still is not proof of useful inference.

### M2.P2 — Run acoustic/vocoder inference inside normal song rendering

**Entry:** M2.P1; an admitted diagnostic export to integrate the runtime. The final singer model may still be in preparation.

**Existing owners:** `prepareNeuralScoreRequest()`, `prepareDiffSingerAcousticInputs()`, `runNeuralWorker()`, `RenderSnapshotFactory`, `PhraseRenderPipeline`, render coordinator/cache, export services and native singer selection.

**Required changes:**

1. Add **New** `apps/seam-neural-worker/main.cpp`, plus a worker-only runtime adapter. Pin the runtime and dependencies through `third_party/manifest.yml` and packaging intake. Keep the runtime out of the realtime plug-in processing path; do not add a runtime download-on-first-note behavior.
2. Validate actual input/output tensor names, dtypes and shapes against the admitted export. Reuse the existing DiffSinger converter for token durations, padded F0 and steps. Token zero's exporter-specific padding meaning, explicit silence and duration rounding must remain consistent with the bound vocabulary.
3. Execute acoustic graph → admitted mel representation → compatible vocoder → mono PCM. Reject NaN/infinity, wrong frames/channels/rate, missing output or a mismatched bundle/request response. Trim declared feature padding to the exact requested sample range.
4. Apply the request's sample-domain dynamics once. Do not feed it as model energy and then multiply it again. Neural pitch uses the compiler's final owned F0; do not add manual/generated vibrato a second time inside the worker.
5. Add `RenderSnapshotFactory::createNeural()` with the same pronunciation/revision/owned-range invariants as other resource paths. Carry the prepared admitted bundle identity, feature/control identity, provider/runtime/worker versions and quality settings into cache provenance.
6. Add the neural pipeline branch and backend-neutral result handling. Audit sample-only accesses, such as `snapshot.sample()` and assumptions that a useful `UnitPlan` always exists. Neural results must not invent sample-unit markers or a fake voicebank manifest to satisfy sample-specific consumers.
7. Integrate neural selection, preview, stop/retry, project save/reopen, multi-voice scheduling and dry/master/stem final export through the same authoring coordinator. Worker errors become structured diagnostics; an older successful phrase cannot be published over a failed current request.
8. Measure both cold and warm execution. Start with the bounded process path; if startup/model reload violates the agreed pilot budget, add a bounded session owner keyed by immutable bundle/provider identity. Such a worker may reuse admitted state, but cancellation, replacement and maximum worker count remain explicit. Do not introduce an unbounded process pool.
9. Package the worker/runtime on macOS arm64 and Windows x64. Map deployment `windows-x64` to product-contract `windows-x86_64` explicitly and test it; do not rely on string coincidence.

**New tests:** `tests/test_neural_inference.cpp`, `tests/test_neural_render_workflow.cpp`, CTest `seam_neural_inference_tests` and `seam_neural_render_workflow_tests`. A tiny deterministic graph can test computation and transport, but must be labeled a diagnostic fixture, not an original singer.

**Exit tests:** nonzero expected diagnostic output; actual candidate phrase inference; exact request/response identity; preview/final declared behavior; two simultaneous singer tracks; pitch/timing edit invalidation; cancelled/stale worker output; missing dependency; crash/deadline; save/reopen using installed assets with producer/source folders unavailable.

**Audible exit:** a real model produces intelligible held-out lyrics inside SEAM and a song export contains those vocals. The separate M2.P3 provenance/quality work and M6 acceptance still apply.

### M2.P3 — Build and execute the original model/data production pipeline

**Entry:** start source manifests and corpus preparation early; lock training/export configuration against M2.P2's admitted graph family before the training run intended for qualification.

**New owner:** `tools/voice_model_training/`. Reuse source admission and immutable asset conventions, but do not treat a sample-bank inventory as a complete neural dataset.

**Required pipeline:**

1. `admit`: capture authorized source manifests, exact audio, speaker/original-identity, session/song IDs, language/style, permissions and source evidence. Training/model distribution permission is not inferred from permission to publish a rendered WAV.
2. `prepare`: inspect audio, segment phrases and retain transforms/retake lineage. Keep original source immutable. Fail on invalid channels/rates/bounds with per-item diagnostics.
3. `label`: attach lyric/phoneme sequence, note pitches, slurs, durations, F0, voicing and alignment confidence. Native/human corrections produce reviewed label revisions. Low-confidence alignments enter a correction queue; do not silently become training truth.
4. `split`: assign source songs/sessions and their derived/augmented relatives to train/validation/test before augmentation. Use deterministic group hashing, cross-split duplicate checks and an explicit held-out set. Exact duplicates and retakes must not inflate evaluation quality.
5. `train`: record code/model initialization, dependency versions, configuration, seeds, feature schema, hardware and checkpoint identity. Choose permitted adaptation or from-scratch training from measured pilot results; neither a pretrained model's availability nor procedural generation alone establishes suitable rights/quality.
6. `export`: produce the admitted acoustic/variance/vocoder family plus vocabulary/configuration. Run graph admission and training-runtime versus exported-runtime comparisons using predefined tolerances. Retain the chosen checkpoint, not only an unlabeled final ONNX file.
7. `qualify-candidate`: run held-out phone/range/style and musical tests, inspect failures, and emit a candidate dossier. It must be possible to fail qualification while retaining an auditable trained model.

**Required records:** source manifest, transformations, label revisions, split manifest, duplicate report, training configuration, environment lock, checkpoint digest, exporter identity, bundle manifest, compatibility results and actual held-out audio. Store bulky/permission-sensitive assets in the controlled artifact store; source control holds code, specifications, allowed fixtures and references, not unreviewed recordings or model weights.

**Command contract to implement:** `admit`, `prepare`, `label-report`, `split`, `train`, `export`, and `qualify-candidate` subcommands, each consuming a captured configuration and writing a new named output. `--help` must state inputs, overwrite behavior, limits and resulting identities. No subcommand may issue musical approval or release GO. These are planned interfaces, not current runnable commands.

**Tests:** deterministic split; duplicate/retake leakage rejection; missing permission/evidence; unknown phone; alignment outside phrase bounds; voiced/unvoiced mismatch; failed/resumed preparation; export feature mismatch; repeated export comparison under declared reproducibility tolerance; actual macOS and Windows CPU inference using the exported candidate.

**Exit:** an original-model candidate with traceable lawful source, repeatable preparation/export, nontrivial held-out singing and measured runtime behavior on both targets. Five minutes of green protocol tests cannot substitute for this result.

## 8. M3: Complete multilingual expression

### M3.P1 — Connect complete pronunciation resources to actual singer coverage

**Entry:** M1.P1 inventory identity and the pronunciation reconciliation baseline. English/Korean implementation can proceed before the neural model is qualified.

**Existing owners:** `libs/seam-phonemizer/`, Japanese reading/resource jobs in `libs/seam-authoring-runtime/`, voicebank inventory tools, domain pronunciation identity, native lyric/correction actions and language tests.

**Required changes:**

1. Separate language profile data from common inventory generation. Japanese must retain its existing symbols and special cases; English and Korean need their own inventory/sequence construction, not the Japanese CV/VC template with different labels.
2. Add reviewed English lexical pronunciations, stress handling, inflection/context rules and explicit unknown-word correction. Replace the 26-entry bootstrap as the advertised general-purpose resource; preserve its deterministic behavior as a fixture/legacy identity where needed.
3. Add Korean lexical/morphological exceptions and reviewed boundary behavior around the current decomposition rules. Cover coda neutralization, liaison, assimilation and relevant irregular forms through concrete corpus cases, not blanket substitution rules.
4. Complete Japanese dictionary/helper packaging and ordinary unknown/mixed-reading workflows. Retain explicit resource identity, request cancellation and saved user corrections when dictionary content changes.
5. Introduce a compatibility check from resolved phone/stress/context IDs to each selected sample/procedural/neural inventory. Missing coverage lists the exact phone/context and affected notes; it never silently maps all unknown sounds to a vowel or silence.
6. Preserve `PhonemeKey`, sequence identity and manual timing ownership during re-resolution. An ambiguous remap becomes unresolved; undo restores the exact prior intent. Bind language resource changes to rendering invalidation.
7. Put all resources through exact-version intake and source/resource permissions. A successful helper call is not language-quality acceptance.

**New contract/files:** a `LanguageResourceBundle` loader under `libs/seam-phonemizer/` binds language, resource ID/version, lexicon/rule/vocabulary digests and a validated phone-symbol mapping. Add language-specific profile implementations under `tools/voicebank_script_generator/languages/ja.py`, `en.py` and `ko.py`, with common canonical serialization/identity kept in the existing inventory owner. Existing pronunciation resolvers consume the admitted bundle; do not add a parallel UI dictionary resolver. Exact lexicon/dictionary asset selection is the measured/intake output named in section 3.2.

**New tests:** multilingual inventory/profile fixtures and `tests/test_language_resource_coverage.cpp`; extend English/Korean/Japanese/reconciliation suites. Add corpus examples for ordinary words, proper names, mixed scripts, punctuation, repeated syllables, multi-note syllables, explicit hints and corrections.

**Exit:** each language has a real resource-backed pronunciation path and matching singer candidate coverage; held-out phrases have no unreported missing phones; native-language review outcomes are retained separately from parser success.

### M3.P2 — Make style blending and advanced controls audibly real

**Entry:** style-owned assignments from M1.P1; two compatible candidate styles and a working renderer/model path. This package owns actual algorithms, not merely UI availability.

**Existing owners:** synthesis `renderer_capabilities.cpp`, `unit_selection.cpp`, performance compilation and classical processors; voice design sources; neural conditioning/adapter; domain performance state and native expression inspection.

**Required changes:**

1. Preserve persisted channel units: pitch `midi-cents`, timing `microseconds-offset`, dynamics `linear-gain`, formant `semitones-shift`, gender `bipolar`, attack/release `milliseconds`, and normalized breathiness/tension/airiness/style-blend/growl. Do not silently change their ranges or meaning. Any necessary semantic change requires a versioned migration and new ABI identity.
2. Add a backend/resource-conditioned capability resolver. Its result must account for the selected resource's phone coverage, paired styles and supported model inputs, not only a `RendererHint` enum. A capability may be supported in one combination and explicitly unsupported in another; no mandatory feature may remain unsupported everywhere at GO.
3. Style blend uses compatible aligned material or an admitted model conditioning path. Validate pair identity, phone coverage, range and alignment; render endpoints and intermediate values. Blend 0/1 must match the selected endpoint within declared numeric tolerance. Switching style IDs is not a blend.
4. Implement formant shift as spectral-envelope/tract change independent of target F0; breathiness as controlled voiced/aperiodic balance; airiness as its declared high-frequency/noise behavior; tension as effort/spectral/source change independent of linear gain; gender as the defined tract/source mapping; growl as bounded evaluated roughness/subharmonic processing. The chosen DSP/model method can differ by backend, but each must have a neutral behavior and an acoustic oracle.
5. Keep portamento, vibrato onset/rate/depth/fades/phase, legato/melisma, consonant timing, breath placement and release in the combined expression test matrix. The existence of the newer seven flags must not cause these original section-7 obligations to disappear.
6. Define interpolation/control-rate smoothing and voice/context reset behavior. Reject invalid values and incompatible control requests. Do not silently fall back to Raw when it cannot preserve required intent.
7. Give every newly audible algorithm a cache/provenance revision. Neutral-versus-changed comparisons use the same source, target pitch, timing and gain where those are not the tested variable.

**New tests:** `tests/test_advanced_expression_audio.cpp`, `tests/test_paired_style_render.cpp`; focused CTest targets. Extend compiler, capability, performance snapshot and manual-preservation regressions.

**Acoustic acceptance examples:** formant change moves the envelope without changing intended F0; tension differs after level matching; breathiness preserves voiced coverage/intelligibility; growl remains finite and bounded; style interpolation behaves consistently across neighboring units; neutral controls preserve the declared reference behavior; all controls survive undo/save/reopen/final export.

**Exit:** every mandatory expression has at least one implemented, candidate-supported audible path, with dry comparisons and explicit remaining quality judgments. A slider test or boolean capability table is not the exit.

### M3.P3 — Replace contract-only automatic performance with useful editable proposals

**Entry:** existing ownership/revision model; an admitted performance prediction method and usable native comparison controls from M4 as needed.

**Existing owners:** `automatic_performance.hpp/.cpp`, `PerformanceJobContext`, performance commands, domain take/selection state, render compiler and neural interfaces.

**Required changes:**

1. Keep the deterministic four-channel generator as a labeled test/reference backend. Add a production proposal backend that predicts useful pitch, timing, energy/articulation from score/language/style context; connect model-driven performance where required by the neural workflow.
2. Normalize output into existing proposal lanes and the immutable captured musical/pronunciation/ownership revisions. Give the generator/model/configuration/seed an exact identity. Prediction produces a proposal, never automatic acceptance.
3. Implement full and selected-range/channel regeneration. Locked manual edits remain authoritative; accepting a proposal uses shared commands, rejects stale ownership and supports exact undo.
4. Resolve timing proposals through the ordered timing solver before audio rendering. Generated F0, manual offset/replacement and manual vibrato must compose once according to the existing ownership contract.
5. Implement alternate-take audition at matched playback position, accept/reject, comparison with the previous accepted take, and persistence. Bound retained takes/points using existing domain limits and actionable capacity diagnostics.
6. Retain actual before/after repair work for the creator study. A different seed or random pitch perturbation is not evidence of reduced manual effort.

**Tests:** stale completion after note/lyric/ownership change; partial acceptance; locked subrange; revision-bound undo; cancellation; invalid timing proposals; generated/manual vibrato collision; save/reopen; different model identity invalidating only affected results.

**Exit:** propose → compare → accept selected intent → hand-correct → regenerate an unlocked range works in the native product without losing edits. M6.P2 must establish whether it improves creator work on counterbalanced unfamiliar tasks.

## 9. M4: Finish the native creator product

### M4.P1 — Complete tuning controls and repair dense-layout behavior

**Entry:** current command/scene/semantics baseline. Start the subset required by M1.P3 immediately; do not wait for neural qualification to repair a blocked native action.

**Existing owners:** `libs/seam-application/src/note_commands.cpp`, `lyric_commands.cpp`, `tempo_commands.cpp`, `performance_commands.cpp`; `libs/seam-editor-ui/src/note_visual_layout.cpp`, `note_spatial_index.cpp`, piano-roll/phoneme/dynamics/vibrato models; native `editor_controller.cpp`, `editor_scene.cpp`, `editor_semantics.cpp`, inspectors and text measurement/composition adapters.

**Required changes:**

1. Audit each required editing action through its command, state mutation, undo, layout, hit testing and accessibility action. Finish unequal-note duplication, tempo/meter editing, lyric distribution/search/hints, phoneme timing, portamento/vibrato and advanced expression inspection as integrated workflows.
2. Resolve overlap by selection/navigation, not by silently moving or shortening notes. Use the existing visual-layout/spatial-index owners to expose overlap groups, active-note emphasis and deterministic keyboard cycling. Painted geometry, pointer hit testing and accessibility bounds must derive from the same layout result.
3. Preserve lyric/phoneme text for short notes: show only what fits in the note, provide full accessible text and a readable detail/edit surface. A clipped label must not become clipped editable content. Avoid drawing labels on top of neighboring notes merely because they share a baseline.
4. Apply measured text layout to lyrics, style/resource names, file paths, diagnostics and review identities. Use wrapping in detail panes, ellipsis in compact controls and full text through focus/accessible details. Long paths or error messages must not expand the application beyond the viewport.
5. Preserve IME composition for Japanese/Korean and multi-code-point text. Composition must not commit partial text repeatedly or trigger note deletion/shortcuts while the text field owns input.
6. Complete expression applicability and units in the inspector. Show the selected backend/resource's actual capability decision; preserve an unsupported stored edit and explain the required resource change instead of discarding it.
7. Extract the active controller responsibilities into focused adapters only when needed to implement these behaviors. Keep shared command execution and the scene/semantics contract; do not rewrite the whole native application.

**Required visual matrix:** the actual minimum supported window; 1024×768 and 1280×800 where admitted; dense chords/overlaps; one-tick and very long notes; long Japanese/English/Korean lyrics; large resource IDs; long file paths; multiline failures; available text/display scaling on each platform; keyboard-only operation. If a proposed size is below the product minimum, test the enforced minimum and document that fact rather than claiming the smaller size passed.

**New tests:** `tests/test_native_dense_layout.cpp` and `tests/test_expression_inspector_workflow.cpp`. Extend existing lyric, tempo/meter, ownership, authoring characterization and native semantics suites. Retain actual screenshots/accessibility observations for the relevant installed candidate.

**Exit:** a complete native tuning session is possible with no overlap-selection dead end, inaccessible full text, IME data loss or unsupported-control deception. A screenshot alone does not prove editing/undo, and a model test alone does not prove readable layout.

### M4.P2 — Complete interchange, takes and editable harmony workflows

**Entry:** bounded interchange codecs/services and M3.P3 proposal ownership; native comparison can be integrated while the production generator is under development.

**Existing owners:** `libs/seam-interchange/`, authoring `interchange_service.cpp`, application performance/harmony commands, native import/export and take/harmony controls.

**Required changes:**

1. Keep the existing SMF Type 0/1 PPQ and typed USTX 0.9 subset boundaries. Inventory retained, converted and unsupported fields in the conversion report. Do not restore the old MIDI deferral or expand the parser into arbitrary YAML/full-format compatibility without a concrete required workflow.
2. Native import is a staged operation: choose file → bounded parse → preview conversion/losses → confirm a target/new document → one command transaction. Cancellation, stale document revision and failure must not partially mutate the open song.
3. Native export writes the declared subset to a new target through existing safe publication owners. Re-import into SEAM and compare musical structure; test the declared subset in the actual external application. A byte-for-byte file comparison is not the sole musical oracle when ordering/formatting may differ.
4. Complete take audition, comparison, selection, rejection and partial regeneration UI. Display take identity, generator/resource/pronunciation revision, locked ranges and stale status. All actions use the same domain commands tested in M3.P3.
5. Finish editable harmony generation: deterministic target notes/tracks, configurable intended intervals/range, explicit out-of-range or conflicting-note diagnostics, lyric/phoneme handling and preserved source-track ownership. A generated harmony remains editable score data, not an untraceable pre-rendered stem.
6. Verify master/stem export and project save/reopen with alternate takes and harmonies active. A later source-track edit invalidates only dependent results; it must not overwrite a manually edited harmony without the declared regeneration action.

**Tests:** extreme PPQ/count/length before allocation; malformed/truncated data; unsupported timing mode; tempo/meter retention; explicit field loss; cancellation after conversion; stale destination; unequal-note/lyric round trip; saved take selection; harmony out-of-range, undo and manual edits.

**Extend:** `tests/test_smf_interchange.cpp`, `test_ustx_interchange.cpp`, `test_interchange_service.cpp`, `test_harmony_workflow.cpp` and take/ownership suites. **New:** `tests/test_native_conversion_workflow.cpp` for the actual native command flow.

**Exit:** an unfamiliar imported score can be tuned, compared through takes, given an editable harmony, saved/reopened and exported through native actions with declared losses visible.

### M4.P3 — Add synchronized character performance and assemble final resources

**Entry:** admitted singer/resource identities and stable rendered pronunciation/timing. Approved final artwork can arrive separately from implementation fixtures.

**Existing owners:** `libs/seam-character/`; native `character_presentation.cpp`, scene/controller/semantics; render snapshots/coordinator; character packaging; `packaging/release-resource-inventory.json`; `docs/product/full-product-beta-contract.json`.

**Required changes:**

1. Add a **New** `CharacterPerformanceSnapshot` containing singer/style/resource identity, render revision, pronunciation identity, sample-time mouth/phonetic cues and bounded energy/expression envelopes. Produce it from the same successful phrase result used for audible playback, not from speculative future notes.
2. Keep operational state and performance state separate. Warning/error/readiness remains truthful when no current audio is available; mouth movement must not pretend Pending/stale audio is playing.
3. At presentation time, map the current playhead to the immutable snapshot and render a lightweight mouth/energy/expression state. Handle stop, seek, loop, singer switch, resource replacement and overlapping tracks deterministically. Track selection/active-singer policy is explicit.
4. No disk decoding, resource verification, waveform analysis or heap-heavy animation work occurs in the audio callback. Preload/cap assets on the UI/worker side and publish only a bounded read model.
5. Use portraits, thumbnails and performance assets for their intended spatial roles. Collapse secondary artwork before compromising the editor's minimum musical workspace. Preserve an identity/status indicator and accessible description; reduced motion disables movement without hiding useful state.
6. Add versioned performance/viseme asset bindings and preserve old status-only packages as status-only. Development-only artwork cannot be marked production-ready by renaming its folder or manifest status.
7. Assemble actual candidates for the original recipe, generated/real sample resources, original model/vocoder, language resources and character. Every advertised language/style/range/control combination maps to existing candidate identities and actual evidence.
8. Update the canonical resource matrix only with reviewed real resource entries. Keep unqualified candidates visibly distinct from released resources; a populated matrix with invented approval is worse than an honest unresolved matrix.

**New tests:** `tests/test_character_performance.cpp`; native layout/stop/seek/reduced-motion integration cases; resource-manifest mismatch/tamper tests. Keep deterministic fixture artwork clearly development-only.

**Exit:** the selected original singer has correctly synchronized, space-efficient presentation; final resource candidates are assembled with exact identities and applicable provenance. Release qualification remains M6's job.

## 10. M5: Qualify installed host and recovery behavior

### M5.P1 — Implement authoritative Follow Host preparation

**Entry:** current Fixed Audio `OfflineRenderSession` and host timeline mapper. Work can begin before the neural model is final.

**Existing owners:** `libs/seam-clap-editor/src/host_timeline.cpp`, `offline_render_session.cpp`, `editor_runtime_project.cpp`; their headers; render coordinator and wrapper transport adapters.

**Required changes:**

1. Add **New** `PreparedHostTimeline`: host/project identity, capture revision, exact covered musical/sample range, tempo/meter segments, loop/offset semantics, sample rate, completeness flags and canonical map identity. It must distinguish observed samples from unobserved future range.
2. Implement a real acquisition route supported by the host/wrapper: a complete host-provided map where available, or an explicit full-range capture/import workflow whose authority can be validated. Do not assume every API supplies a whole map. A constant-BPM observation is not a complete map unless the admitted workflow establishes constant tempo over that range.
3. Define gap, discontinuity, seek, loop and tempo-change handling. Reject incomplete/inconsistent capture; preserve available ramp/segment semantics or explicitly reject an unsupported map before preparation. Never quietly flatten a changing map to its initial BPM.
4. Freeze the acquired map/range outside the realtime callback and compile the exact final vocal range against it. Bind it into `OfflineRenderIdentity` and render/cache identity. Fixed Audio remains an explicit persisted alternative.
5. At tempo/map/resource/project changes, invalidate the affected readiness identity and stale phrase audio. A late result from an older map cannot set Ready. The user can see what range needs recapture/preparation.
6. Separate SEAM's ability to refuse Ready from a host's ability to create a bounce file. Use supported host error/preparation mechanisms, and never claim universal prevention of host file creation. If a host can still write silence or partial audio, the workflow must expose that failure and its qualification test must fail; it cannot be counted as a successful vocal bounce.

**New tests:** `tests/test_host_timeline_capture.cpp`; extend `tests/test_offline_render_session.cpp` and `tests/test_clap_offline_host.py`.

**Required cases:** tempo change mid-phrase; tempo change after preparation; meter change; sample-rate change; nonzero project offset; backward seek; loop crossing; stopped capture; missing map segment; stale completion; Fixed/Follow switch; cold offline bounce; late resource replacement. Verify actual sample alignment and current vocal presence, not only a Ready enum.

**Exit:** a complete admitted host range can be prepared and bounced with current vocals, and incomplete/stale preparation is truthfully rejected. Actual host proof follows in M5.P2; a synthetic timeline alone does not close R13.

### M5.P2 — Finish real installed execution and live-expression semantics

**Entry:** integrated singer, M2 worker support and M5.P1 timing behavior; signing/install credentials for release-grade runs.

**Existing owners:** CLAP/live voice/editor adapters, VST3/AUv2 wrappers, standalone composition roots, platform module/resource resolution, packaging/signing scripts and installed-evidence tools.

**Required changes:**

1. Materialize helpers, model/vocoder, dictionaries and character under each real installed surface. Resolve relative to the loaded SEAM module/application resources, not the DAW executable, shell PATH, working directory or developer checkout.
2. Complete per-note event addressing and live pan/vibrato/timbre behavior, sustain/pedal, panic, note lifecycle and reload. Translate wrapper-specific events into shared musical intent and test exact target-note ownership; do not substitute channel-wide changes where note-specific addressing is required.
3. Prove helper lifecycle under each host, including cancellation/quit/crash and the supervisor constraints from M2.P1. No orphan workers, blocked host shutdown or reaping of unrelated host children is acceptable.
4. Sign/package in the required order and measure the exact installed effective files. If signing changes bytes, hashes/evidence must describe those final bytes rather than the unsigned build directory.
5. Run standalone on macOS arm64 and Windows x64, plus these nine explicit tuples:

| Platform | Host | Format |
|---|---|---|
| macOS arm64 | REAPER | CLAP |
| macOS arm64 | REAPER | VST3 |
| Windows x64 | REAPER | CLAP |
| Windows x64 | REAPER | VST3 |
| macOS arm64 | Bitwig | CLAP |
| macOS arm64 | Bitwig | VST3 |
| Windows x64 | Bitwig | CLAP |
| Windows x64 | Bitwig | VST3 |
| macOS arm64 | Logic Pro | AUv2 |

6. For each tuple, exercise load/reload, editor open/close, live input, note expression, transport/loop/seek, tempo behavior, offline bounce, missing/changed resource and worker failure. Capture exact host/version/settings, installed identities and expected versus actual audio.

**Exit:** every required tuple and standalone surface has observed current-candidate behavior. Compile-only Windows checks, a generic plug-in probe or one REAPER run cannot certify the entire matrix.

### M5.P3 — Complete preserved U60 recovery/support and required soaks

**Entry:** existing crash/recovery/support implementations; use current candidate identities for final acceptance.

**Existing owners:** `libs/seam-platform/src/crash_capture*`; authoring autosave/project lifecycle/support bundle; native `recovery_support_panel.hpp`; Studio production/recovery; existing public-support/installation/soak tools.

**Required changes:**

1. Map every unfinished obligation from production-plan U60 to a real product action and test. Preserve its original privacy and recovery requirements; do not declare it replaced by the new milestone names.
2. Exercise failed startup, interrupted project save, export publication interruption, interrupted generation/import, cancelled model inference and resource replacement. Restore only a verified committed generation/result, explaining any uncertain durability.
3. Verify recovery does not overwrite a newer user document or unrelated producer work. Replay/retry is idempotent where promised; otherwise it requests a new destination or explicit selection.
4. Complete native recovery selection, diagnostics and support-bundle UX. User attachments are explicit and separate from automatically included metadata. No automatic recording/model/source upload is introduced.
5. Run the required full-duration host/product/device/producer soaks from the controlling contract. Short smoke runs remain smoke runs. Monitor bounded memory, worker count, device recovery and stale publication with named failure thresholds.
6. Restore a clean installation and its evidence archive on the intended machines; verify that recovery/resource/helper behavior is independent of developer directories.

**Tests:** crash artifact privacy/redaction; bundle size/path limits; missing/changed recovery entry; startup failure; interrupted durable publication; malicious attachment path; unrelated project preservation; actual long-run memory/worker/device behavior.

**Exit:** all preserved U60 obligations and required installed/soak cases have valid evidence on the exact effective candidate. A support dialog or crash-file unit test alone is not completion.

## 11. M6: Establish quality and issue Beta GO

### M6.P1 — Freeze criteria from pilot evidence before final scoring

**Entry:** earlier source/model/runtime pilots and reference-machine access. Begin collecting data during M1/M2; complete the freeze before the final held-out study.

**Existing owners:** `docs/product/full-product-beta-contract.json`, empirical/profile/protocol validators under `tools/external_beta/`, singing-quality tools and corpus definitions.

**Required outputs:**

| Currently unresolved criterion | Evidence-producing packages |
|---|---|
| Acoustic boundaries | M1.P2/P3, M3.P2 |
| Expression tolerances | M3.P2/P3 |
| Pronunciation scoring | M3.P1 and native-language pilots |
| Identity rubric | M1.P3, M2.P3, M4.P3 and independent pilot judgments |
| Generation budgets | M1.P2/P3 |
| Neural budgets | M2.P2/P3 |
| Resource limits | M1/M2 and installed M5 measurements |
| Cancellation budgets | M1.P2, M2.P1/P2, M5 |
| Reference machines | M2/M5 operators and measured configurations |
| Source volume | M2.P3 coverage/learning evidence |
| Reproducibility tolerances | M1 deterministic paths and M2 training/export/runtime comparisons |

**Required changes:** define workload, measurement method, units, machine/provider configuration, aggregation and exclusion rules for each criterion; retain calibration evidence; version/freeze the profile through the existing contract process. Preserve the 18 already fixed criteria/protocols unless an explicitly authorized contract change is made.

Do not tune a threshold on the final held-out results, replace a missing value with zero, or label a method fixed while its comparison population remains undefined. Changed material or methods can invalidate earlier evidence.

**Exit:** the canonical evaluation profile is genuinely complete and frozen, with traceable values and unchanged normative fixed requirements. This package defines the scoring rules; it does not supply passing final scores.

### M6.P2 — Run real acoustic, language, identity and creator qualification

**Entry:** candidate resource matrix, M6.P1 freeze, actual current code and required evaluators. Final installed claims use the M5 candidate, not a convenient development build.

**Required execution:**

1. At least 60 short phrases per required language, at least 180 total, and three complete songs spanning Japanese/English/Korean. Cover every advertised resource/range/style combination and every mandatory capability according to the original case matrix.
2. Retain fixed pitch requirements: median steady-voiced error at most 30 cents and at least 90% of designated steady frames within 50 cents. Score octave errors, voiced coverage and predeclared exclusions separately. Silence or removed difficult frames cannot manufacture a pitch pass.
3. Verify deterministic 30 ms phoneme timing edits within one sample of the intended displacement under the declared workload. Measure attacks/releases/transitions, unvoiced regions and complete melody rather than one sustained vowel.
4. Preserve distinct latency workloads: the full-scope classical small-edit p95 requirement and any preserved legacy median/p95 requirement are separate tests with their own definitions. Do not pass one by citing the other. Neural/generation/cancellation metrics use the frozen measured profile.
5. Obtain native-language/identity/listening judgments and at least five independent pre-GO creators. Include real-source and generated-source producer journeys, full native song work, and counterbalanced unfamiliar manual-versus-assisted tasks.
6. Evaluate at least two reviewed styles and an actual supported paired-style blend. Evaluate female/original identity from the produced voice and agreed rubric, not a preset name or narrator claim.
7. Preserve raw observations, anonymized participant/task identities where appropriate, source and candidate hashes, scores and failed cases. Implementer-created test identities are never independent human participants.
8. When a capability fails, reopen the package that owns its cause, retain the failed observation, repair it and re-run affected acceptance on the new candidate. Do not reduce scope or retroactively relax the criterion.

**Exit:** genuine full-product case evidence across acoustic, pronunciation, identity, usability and resource/workflow requirements. All required cases must satisfy the original contract, not just the subset convenient to automate.

### M6.P3 — Complete canonical semantics, restore the archive and authorize READY

**Entry:** M5 installed identities and M6.P2 accepted evidence; required release-role authority.

**Existing owners:** `tools/external_beta/full_product_report.py`, contract/profile readers, `release_gate.py`, `release_audit.py`, evidence archive/freeze tools and release promotion paths.

**Required changes/execution:**

1. Reconcile stale canonical metadata such as `semanticValidation.status: UNAVAILABLE` through a deliberate accepted contract revision. The validator's existing positive synthetic test is real engineering progress, but not enough to mark real evidence available or passing.
2. Feed actual candidate-bound case records and raw artifacts through the existing EB-009 semantics. Preserve exact case IDs, measurement definitions, required applicability and evaluator roles. Do not create a parallel release evaluator just for this plan.
3. Keep both success and rejection tests: complete synthetic input is accepted only under its appropriate synthetic contract; missing/duplicate cases, changed PCM, stale resource/installed identity, incomplete criteria and tampered archives are rejected.
4. Assemble the final exact archive, restore it independently of the build workspace, and run the actual release audit/promotion path. All required EB-001–EB-009 conditions must pass together.
5. Use a policy-compliant release verification checkout for any master-only branch/source-closure rules. Never delete user branches to make the license or release audit appear green. Any change to that branch policy is a separate deliberate repository-policy decision.
6. The authorized release role issues `EXTERNAL_BETA_READY` only after all conditions pass. READY does not fabricate the later external cohort, CLOSED or PUBLIC_ACTIVE events.

**Exit:** exact restored-candidate audit succeeds, required roles authorize READY, and no original unit obligation remains unearned. This is the only Full-Scope Beta GO exit in this plan.

## 12. First implementation batch: follow this sequence

The next implementation request should start here. Do not begin by writing another whole-project report.

1. **Preserve and inspect the checkout.** Record HEAD and dirty files. Read only changes since this baseline and the relevant package owners. If production code is unchanged, reuse the known architecture findings; establish fresh tests for the code actually being changed.
2. **Implement M1.P1 as one coordinated schema change.** Add style/language-owned assignments, inventory v2, producer v4, explicit legacy migration and Python/C++ parity. Preserve old history and review invalidation. Finish the current draft range/qualification separation.
3. **Make one useful phrase work in M1.P2.** Use the pilot profile to expose the exact missing articulation. Implement context and the necessary consonant/transition classes, then retain the resulting dry audio and failures. Do not generate the full bank until this phrase-level result is informative.
4. **Complete campaign advancement and recovery.** Use just-in-time expectation capture and receipt-bound batch progression. Test interruption before collection and after committed collection but before campaign receipt publication; recover without duplicate material or silent expectation rebinding.
5. **Finish M1.P3's native route.** Wire generation, editing, supplied review, package/install and new-song selection to shared services. Fix the layout problems that obstruct this route. Extend the existing installed-song fixture rather than inventing a second installation mechanism.
6. **Produce the unfamiliar 16-bar result.** Save/reopen and export dry/master/stem. Retain recipe/inventory/bank identities and exact source-to-bank lineage. Report real listening/review/capture evidence separately from test fixtures.
7. **Continue M2 independently.** Begin process extraction/bundle admission and lawful data preparation without waiting for every M1 quality judgment. If the implementer is a single agent/person, interleave at stable commits rather than editing shared CMake/schema concurrently.

The desired first checkpoint is a runnable/audible singer loop, not “M1.P1 planned” or “one more validator passes.” If an experiment fails, retain its example and implement the next targeted repair within the same outcome.

## 13. Test and command runbook

### 13.1 Commands available at this baseline

Run from the repository root. These are existing build/test/help commands, not implementation evidence generated by this document:

```sh
git status --short
git rev-parse HEAD
cmake --build build/release -j 2
ctest --test-dir build/release --output-on-failure -j 2 --no-tests=error
build/release/seam_voicebank_cli --help
```

The current build is configured Release with tests, native desktop and CLAP/editor targets enabled. For another checkout or platform, reproduce the intended options using the repository's platform setup; do not assume a default configure enables every required target. A missing required test configuration is not a pass.

**Focused first-batch regression:**

```sh
cmake --build build/release --target seam_voice_design_tests seam_voice_designer_tests seam_performance_compiler_tests seam_production_draft_tests seam_production_ownership_tests seam_production_staging_tests seam_production_import_outcome_tests seam_sample_review_cli_tests seam_studio_sample_review_tests seam_voicebank_production_tests -j 2
ctest --test-dir build/release --output-on-failure -j 2 --no-tests=error -R '^seam_(voice_design|voice_designer|performance_compiler|production_draft|production_ownership|production_staging|production_import_outcome|sample_review_cli|studio_sample_review|voicebank_production)_tests$'
```

The ten names above were checked against the current CTest registry while writing this plan. This was a test-list inspection, not a fresh passing run. Add the new M1 tests to the focused group when implemented.

For inventory parity, use the configured Python interpreter and run `tests/test_voicebank_script_generator.py` through unittest discovery, alongside the existing production/draft Python parity tests. Do not add a fake command-line case filter to C++ harnesses that only implement `runAll()`; CTest selects the focused executables.

### 13.2 Verification groups

| Change group | Existing regression anchors | Additional implementation evidence |
|---|---|---|
| M1 inventory/producer | Production draft, ownership, staging/import outcomes, voicebank production, sample-review CLI/native; Python inventory/draft parity | New style identity, legacy migration, campaign recovery and original-singer workflow tests |
| M1 articulation | Voice design/Designer, performance compiler, phoneme timing, synthesis quality | New context/transition/chunk equivalence cases plus held-out audio |
| M2 process/bundle | `seam_helper_process_tests`, `seam_neural_worker_protocol_tests`, Japanese reading and deployment/package suites | New bundle/supervision tests, Windows execution and acyclic link graph |
| M2 actual inference | Performance snapshots, render coordinator, worker protocol/package checks | New real inference/render workflow, export comparison and model candidate evidence |
| M3 language/ownership | `seam_language_phonemizer_tests`, `seam_japanese_pronunciation_tests`, reconciliation, performance commands/edit preservation | Multilingual inventory coverage and native-language observations |
| M3 expression/takes | Renderer capabilities, compiler, automatic performance and harmony suites | New advanced-control/paired-style audio cases and creator tasks |
| M4 native/interchange | Tempo/meter, melisma, creator batch edits, SMF/USTX/service, authoring characterization | Native dense-layout/conversion/inspector tests and visual/accessibility sessions |
| M4 character | Existing character package/asset/release checks | New synchronized performance/seek/reduced-motion tests |
| M5 hosts/recovery | Offline session/host, recovery support, standalone/plugin/platform source contracts | Actual installed tuples, real devices, worker lifecycle and prescribed soaks |
| M6 release | External Beta Python contract, public release, singing-quality contract and source-admission suites | Genuine canonical evidence, restored archive and authorized release-role decision |

Do not sum overlapping test executables into an invented coverage score. New focused targets should be added for meaningful test boundaries, not to increase the test count. Review source changes with `git diff --check` and proportionate code review.

### 13.3 Verification cadence

- During local implementation: build the affected owners, run focused positive/error/ownership cases, and reproduce the motivating example.
- At a connected workflow checkpoint: use actual shared CLI/native/library entrypoints, saved artifacts and expected audio/persistence; verify cancellation/retake.
- At an integration checkpoint: run the complete registered Release suite and affected sanitizer/race/source-closure checks. A prior green run does not certify new code.
- At a milestone exit: retain the actual user-facing demonstration and explicitly identify unearned resource/human/platform qualification.
- At final acceptance: use exact signed-installed artifacts and restored evidence, not convenient development fixtures.

### 13.4 Existing production commands and their limits

The following are **current CLI templates**. Uppercase names are required captured inputs, not literal example identities or approvals:

```text
seam_voicebank_cli init-production WORKSPACE DRAFT_DEFINITION FILE_SHA256 PRODUCER UTC
seam_voicebank_cli prepare-generation WORKSPACE PROJECT TRACK_ID REGION_ID TAKE_ID JOB_ID JOB_DIRECTORY
seam_voicebank_cli run-generation JOB_DIRECTORY MANIFEST_SHA256
seam_voicebank_cli prepare-generation-batch OUTPUT_JSON JOB_REFERENCE [JOB_REFERENCE ...]
seam_voicebank_cli run-generation-batch BATCH_JSON BATCH_SHA256 [MAX_TOTAL_FRAMES]
seam_voicebank_cli import-generated-batch WORKSPACE BATCH_JSON BATCH_SHA256 OPERATOR UTC [MAX_TOTAL_FRAMES]
seam_voicebank_cli create-sample-draft WORKSPACE BANK_ID VERSION NAME ja|en|ko STYLE OUTPUT_DIRECTORY
seam_voicebank_cli prepare-sample-review WORKSPACE MANIFEST OUTPUT_PACKET
seam_voicebank_cli inspect-sample-review PACKET FILE_SHA256
seam_voicebank_cli review-sample WORKSPACE PACKET FILE_SHA256 REVIEWER UTC accept|reject [UNIT ...]
seam_voicebank_cli publish-sample WORKSPACE MANIFEST EXPECTED_GENERATION PROJECT_SHA256 OUTPUT_DIRECTORY
```

Resolve hashes, IDs and UTC values from actual artifacts/actions. `review-sample` records a supplied independent decision; it is not an implementer shortcut. `publish-sample` produces a candidate and does not sign/install it. Keep the existing provenance/source-quality admission and packaging/installer stages around these commands.

### 13.5 Planned campaign CLI to implement in M1

These commands **do not exist at the baseline**. They define the desired orchestration surface, implemented over the shared services:

```text
seam_voicebank_cli plan-generation-campaign WORKSPACE PLAN_JSON PLAN_SHA256 NEW_OUTPUT_DIRECTORY
seam_voicebank_cli inspect-generation-campaign CAMPAIGN_JSON CAMPAIGN_SHA256
seam_voicebank_cli advance-generation-campaign WORKSPACE CAMPAIGN_JSON CAMPAIGN_SHA256 OPERATOR UTC
```

`PLAN_JSON` binds inventory/recipe digests, language/style/layers, timing-template revision and aggregate limits. Planning writes an immutable campaign definition and missing/unprepared job templates; it cannot approve material. `advance` completes at most one bounded batch's prepare/render/collect progression and returns a durable receipt/current state. The native campaign controller may repeat advancement until complete or cancelled, keeping render work off the UI thread and durable mutation in its declared owner. It must stop on external producer changes or unresolved committed outcomes.

There is no `approve-all`, `ignore-stale`, `force-qualified` or release switch in this interface.

## 14. Evidence, invalidation and handoff contracts

### 14.1 Milestone artifact set

Use a task-specific workspace for raw development evidence, with immutable result directories per run. Keep approved code/fixtures/manifests in source control and large or permission-sensitive assets in the designated artifact store. An ignored `build/` folder alone is not a durable release archive.

Each milestone result must identify:

- source commit and relevant dirty patch identity;
- build configuration, platform/machine and effective binary identity;
- input score, recipe, inventory, language, bank/model/vocoder and character identities as applicable;
- the exact operation, arguments/configuration, output identities and current result;
- focused/full test outcome and retained audio/visual/runtime observations;
- which evidence is automated fixture, internal pilot, genuine human judgment or installed acceptance;
- failures, missing qualifications and the package responsible for the next action.

Do not store credentials, private signing keys or unnecessary participant/source personal information in these records. A hash identifies captured bytes; it does not establish rights, reviewer independence or musical quality by itself.

### 14.2 Invalidation rules

| Change | Required consequence for the changed candidate |
|---|---|
| Recipe/source audio/processing markers | New generation/material identity; affected sample review and audio evidence must be re-established |
| Inventory/style/layer ownership | New assignments/migration and expectations; no reuse of a differently bound review |
| Dictionary/phonemizer/phone vocabulary | Reconciliation and pronunciation identity change; affected timing/render/model compatibility and language evidence rechecked |
| Model/vocoder/export/provider settings | New admitted execution/cache identity; relevant runtime/acoustic results regenerated |
| Score/tempo/ownership/take selection | Affected phrase/readiness invalidation; stale proposals/results rejected; unrelated user edits preserved |
| Helper/binary/signing output | Installed execution and candidate identity updated; affected installed evidence requalified |
| Character/UI geometry | Relevant visual/accessibility/performance presentation evidence rechecked |
| Contract/profile measurement rules | Deliberate version change and applicability review; old scores cannot silently become scores under new rules |

These rules do not delete history or mutate an old installed resource. Old approvals remain attached to their original immutable material; they simply do not approve changed material.

### 14.3 Execution record to create when implementation starts

**New future file:** `docs/implementation/INTEGRATED_SINGER_EXECUTION.md`. Use it as the concise current status page; retain `FULL_SCOPE_BETA_EXECUTION.md` as historical evidence and do not renumber its entries.

Use one row per package:

```text
Package | Implementation | Demonstrated workflow | Qualification remaining | Source/evidence | Next action
M1.P1  | ...            | ...                   | ...                     | ...             | ...
```

At handoff, state the current milestone, exact latest checkpoint, the next executable action and any genuine external dependency. Do not write “continue U35” without the controlling plan name and actual remaining behavior.

## 15. Requirement preservation checklist

The original unit bodies and verification contract remain authoritative. This index makes omissions visible while executing the larger milestones.

| Requirement | Completion packages |
|---|---|
| R1 timing/syllables/melody/articulation | M1.P2/P3, M3.P1/P2, M4.P1, M6.P2 |
| R2 all persisted/audible expression | M3.P2/P3, M4.P1/P2, M6.P2 |
| R3 recording-free original female Voice Designer | M1.P1–P3, M4.P3, M6.P2 |
| R4 real and generated production | M1.P3, M5.P2/P3, M6.P2 |
| R5 reproducible editable bank lifecycle | M1.P1–P3, M5.P3 |
| R6 full declared coverage/range/two styles/blend | M1.P1/P2, M3.P1/P2, M4.P3, M6.P2 |
| R7 Japanese/English/Korean | M3.P1, M2.P3, M4.P1/P3, M6.P2 |
| R8 dependable classical rendering/capabilities | M1.P2/P3, M3.P2, M6.P1/P2 |
| R9 qualified original neural singer | M2.P1–P3, M5.P2, M6.P2 |
| R10 useful editable automatic performance/takes/harmony | M3.P3, M4.P2, M6.P2 |
| R11 native editing/layout/accessibility | M1.P3, M4.P1/P2/P3, M5.P2, M6.P2 |
| R12 safe USTX/SMF exchange | M4.P2, M5.P2 |
| R13 standalone/nine hosts/live/tempo/bounce | M5.P1/P2, M6.P2 |
| R14 identity and synchronized character | M4.P3, M5.P2, M6.P2 |
| R15 bounded/recoverable/explainable execution | All implementation packages; integrated M5.P3 and M6 acceptance |
| R16 fixed-corpus/acoustic/listener/creator proof | M6.P1/P2 with earlier pilot inputs |
| R17 exact installed release and preserved U60 | M5.P2/P3, M6.P3 |
| R18 non-bypassable full-product gate | M6.P1–P3 |
| R19 applicable rights/provenance | M1.P1/P3, M2.P1/P3, M3.P1, M4.P3, M5/M6 |
| R20 connected UI/batch/source→bank→song | M1.P1–P3, M2.P2, M4.P1/P2, M5.P2, M6.P2 |

## 16. Failure handling and final completion checklist

| Situation | Continue with | Do not do |
|---|---|---|
| Procedural pilot sounds poor | Retained problem phrase, measured failure and targeted M1.P2 DSP/articulation repair; independent M2 work | Generate more copies and call it a qualified bank |
| No lawful neural corpus/model is ready | Preparation/split/export tools, diagnostic runtime, resource intake and available authorized pilot material | Assume a free code repository licenses voices/weights/data |
| Real review or language evaluator unavailable | Technical workflow, material packaging and independent implementation packages; explicit pending review | Create a fake human approval or stop all engineering |
| Windows/DAW machine unavailable | Portable code, fixtures, explicit missing platform run and independent work | Turn source compilation or a macOS run into installed Windows evidence |
| Signature/archive evidence missing | Complete internal candidate and prepare exact inputs for authorized release operators | Bypass GO or copy old candidate PASS rows |
| Shared schema/API changed in another active edit | Preserve it, reconcile the actual diff, serialize integration | Reset the checkout or blindly follow stale line numbers |
| Full test suite fails | Preserve the failure, classify it against current changes and fix the relevant owner | Weaken a test or present a stale previous green log |

Before calling the project Full-Scope Beta GO, verify all of the following:

- [ ] The recording-free original voice and separate real-input workflow both work through actual editing, review, installation and unfamiliar-song use.
- [ ] Real neural acoustic/vocoder inference, original model provenance, training/export and both-platform execution are qualified.
- [ ] Japanese, English and Korean resources/pronunciation work; the declared range, two reviewed styles and paired blend are proved.
- [ ] Every original expression, useful automatic performance, manual preservation, takes and editable harmony works and survives persistence/export.
- [ ] Native editing/interchange/layout/accessibility and synchronized character performance are complete.
- [ ] Standalone and all nine required host tuples, worker lifecycle, final timing/audio, recovery/U60 and prescribed soaks are accepted on exact installed artifacts.
- [ ] Empirical criteria and resource matrix are genuinely complete; the full corpus/listener/creator case set is accepted.
- [ ] Every original U1–U48 obligation is reconciled; the restored candidate passes the existing full audit and authorized roles issue READY.

**Implementation handoff:** begin M1.P1, continue through the M1 singer loop, and start M2's independent process/data work at stable integration points. Use this plan to ship connected capabilities. Do not replace its deliverables with further planning, inflated percentages, or approvals that did not occur.
