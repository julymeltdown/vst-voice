# Project SEAM: developer handoff and reading guide

Snapshot: September 15, 2026. This guide identifies the conversations, specifications, implementation records, and source files needed to explain the project and take over development. It does not issue a new acceptance decision.

Current sequencing update: read [SEAM Revised Development Plan](SEAM_REVISED_DEVELOPMENT_PLAN_2026-09-15.md) alongside the scope and September 13 specification. It incorporates the [second-developer review](SEAM_SECOND_DEVELOPER_REVIEW_2026-09-15.md), makes the existing direct procedural route the first creator checkpoint, and prioritizes listening evidence and expression editing before further inventory expansion. It changes execution order, not Full-Scope Beta GO requirements.

## 1. The project in one paragraph

SEAM is a C++20 virtual-singer creation and song-editing application. Its foundation is a sample-concatenative singing editor, but the agreed product now also requires synthesizer-style creation of an original female voice without recordings, production of reusable singer resources from authorized recorded or generated material, classical and neural singing, expressive Japanese/English/Korean editing, synchronized character presentation, and dependable standalone and DAW operation. The entire expanded virtual-singer scope must be completed before the owner will call it Beta GO. A working sample bank, a procedural demonstration, or an operational neural test model is an intermediate result.

## 2. Which conversation to read

The primary Codex task is **“SEAM 완성도와 개발 로드맵 평가”**.

- Task ID: `01a02275-293a-7192-8c2e-144b9325d7dc`.
- Workspace: `/Users/lhs/Downloads/project-seam-usable-alpha-u3-master`.
- This is the current long-running project conversation. Its title and identity were verified through the app's task listing.
- The other currently listed task in this workspace, **“대기”** (`01a0a066-1eba-71f2-8c0d-e21f9419cbcc`), is described as a second developer waiting for instructions. It is not the primary requirements or implementation history.

Do not start by reading every build log. Read these decision clusters in the primary conversation, in order. The quoted snippets are search anchors from the owner's messages; the descriptions explain their significance in English.

| Priority | Conversation search anchor | What the developer needs to understand |
|---|---|---|
| 1 | `하츠네 미쿠와 같은 보컬로이드` and `Virtual Singer Feasibility and Code Roadmap` | The move from improving a sample editor to building a complete original virtual singer. The comparison describes desired capabilities, not permission to copy another singer's identity or assets. |
| 2 | `신디사이저 깎듯이` | Voice creation must include editing synthesis controls without requiring a recording. Recorded/generated-source production remains another required route. |
| 3 | `이것까지 다 되어야 beta go` | The explicit scope decision: the whole revised roadmap is required before Beta GO. Earlier reduced-Beta recommendations no longer define completion. |
| 4 | `전체 목표 / 기존 코드 완성도 / 앞으로의 개발 해야하는 요소` and `너무 짧은 호흡말고` | The request for a source-backed reassessment and larger integrated delivery steps. This leads to the September 12 report. |
| 5 | `좀 더 구체적인 개발 plan` | The request for an executable development specification. This leads to the September 13 plan and its six milestones. |
| 6 | `일부 수정사항이 코덱스 세션 저장 오류로 없어진듯함` | Preserve the working tree and verify commits, files, and runtime evidence. Conversation continuity alone cannot prove that all earlier changes survived. |
| 7 | `전체적인 진행도` and the recent formant, breathiness, tension, airiness, gender, and growl discussions | Understand the distinction between implemented capability, formally accepted roadmap units, and release readiness. Consult current source and the execution ledger for the latest state. |

The OpenUtau comparison and initial design-review discussions are useful background for interoperability, tuning UX, overlapping notes, text overflow, and character layout. Read them after the settled scope and current plan. Early MIDI deferral discussion is historical: the present requirements explicitly include bounded Standard MIDI interchange.

For someone outside this Codex account, provide a selected conversation export if historical rationale is needed. The task ID alone does not grant access. The checked-in documents below should be enough to begin a technical handoff without the complete private conversation.

## 3. Essential files, in reading order

All links below are relative to the repository root so the guide also works in another checkout.

| Order | File | Purpose and reading instructions |
|---|---|---|
| 1 | [Virtual Singer Feasibility and Code Roadmap](VIRTUAL_SINGER_FEASIBILITY_AND_CODE_ROADMAP_2026-09-05.md) | Read the revision decision, Goal Capsule, R1–R20, and capability requirements. This defines what the finished product must do. Its original defect observations are historical, not proof that each defect remains. |
| 2 | [September 13 implementation plan](SEAM_IMPLEMENTATION_PLAN_2026-09-13.md) | The active implementation specification. Read sections 1–5, then the six milestone/package definitions, exit criteria, command runbook, and evidence contracts. M1.P1–M6.P3 are delivery packages, not replacements for the original U-units. |
| 3 | [Integrated Singer Execution](docs/implementation/INTEGRATED_SINGER_EXECUTION.md) | The most useful current implementation narrative. Read the newest entries at the top, including every `Verified.` and `Not claimed.` paragraph. Then find the September 14 milestone table, generated-take-to-installed-bank lifecycle, neural worker, Follow Host, and automatic-performance entries. Read adjacent follow-ups before assuming an older limitation is still open. |
| 4 | [September 12 progress and revised roadmap](SEAM_PROGRESS_AND_REVISED_ROADMAP_2026-09-12.md) | Explains the assessment and why execution was regrouped into six outcomes. Its commit, test counts, and remaining-work statements are a dated baseline; substantial implementation followed. |
| 5 | [Full-Scope Beta GO plan](docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md) | The original U1–U48 decomposition, requirement mapping, acceptance obligations, and Definition of Done. Use it to decide whether a unit can actually close. |
| 6 | [Full-Scope Beta Execution](docs/implementation/FULL_SCOPE_BETA_EXECUTION.md) | Earlier implementation and unit-acceptance history. Useful for U1–U5 and foundational compiler/resource/production changes. Do not treat old “local/uncommitted” notes as the current Git state. |
| 7 | [Full-product contract](docs/product/full-product-beta-contract.json), [evidence schema](docs/product/full-product-beta-evidence.schema.json), and [External Beta acceptance](docs/product/EXTERNAL_BETA_ACCEPTANCE.md) | Machine-readable scope and release-evidence requirements. These explain why engineering tests alone cannot authorize Beta GO. |

[README.md](README.md) is useful as a module and phase index, but it mixes historical phase summaries with later additions. For example, its “master branch only” statement differs from the active checkout. Do not use the README, `docs/STATUS.md`, or an old readiness report as a standalone current-status authority.

## 4. Current progress to communicate

The accurate description is **an advanced engineering alpha with substantial integrated capability, still short of the full virtual-singer product and Beta GO**.

Verified repository snapshot before this guide was added:

- Active branch: `codex/production-readiness-completion`.
- `HEAD`: `c9aca6fdabb6d51587ab1d1e571a737d0fbb8ad9`.
- Local remote-tracking `origin/master` resolves to the same commit. This guide did not fetch or independently query the live remote.
- Recent commits implement formant editing, breathiness, tension, airiness, and gender controls, with measured DSP behavior and applicability checks. Their presence does not mean every renderer supports them.
- Growl is an unfinished local integration checkpoint: 22 tracked source/build files modified and three untracked files before this guide. Its changes include schema 17, compiler revision 14, DSP, commands, menu wiring, and a new test suite. The committed ledger still says growl has no algorithm; the local diff is newer than that entry.
- The retained `/tmp/ctest-growl.log` reports **163 of 164 registered targets passed**, including growl. The failure is `seam_tracked_source_closure`. The new growl files remain untracked. These are inspected prior-run results; this documentation task did not rerun tests, stage files, or certify a clean candidate.

The implementation ledger records meaningful advances beyond the September 12 assessment: generatable pilot inventory, a connected generated-take-to-installed-bank-to-song regression, neural worker execution and render integration, editable performance proposals, Follow Host preparation, character phrase presentation, and advanced expression channels.

The important remaining outcomes include qualified original singer/model resources, complete multilingual and paired-style workflows, full expression editing surfaces, independent acoustic/listener/creator evaluation, and exact installed platform/host/release qualification. Style blend needs compatible aligned styles; the recent expression entries explicitly leave drawn lanes and inspector applicability rows open. Procedural DSP measurements do not establish listener-perceived identity or singing quality.

Do not quote “95% complete” as the current full-product status. The September 12 report records **5/48 = 10.4% historical unit acceptance**, while acknowledging considerable implementation in later units. That percentage measures recorded acceptance, not source-code completion or remaining effort. This guide does not invent a new overall percentage or reaccept every unit.

## 5. Source and evidence reading map

Read one complete path from persisted project intent to rendered audio before making implementation changes.

| Area | Starting files/directories | What to trace |
|---|---|---|
| Musical state and persistence | [project.hpp](libs/seam-domain/include/seam/domain/project.hpp), [project_json.cpp](libs/seam-formats/src/project_json.cpp), [performance_commands.cpp](libs/seam-application/src/performance_commands.cpp) | Notes, pronunciation, curves, ownership, migration, undo/redo, and validation. |
| Score to audio | [performance_compiler.cpp](libs/seam-synthesis/src/performance_compiler.cpp), [renderer_capabilities.cpp](libs/seam-synthesis/src/renderer_capabilities.cpp), [render_snapshot.cpp](libs/seam-rendering/src/render_snapshot.cpp) | Shared timing/expression, supported controls, frozen resources, cache identity, and refusal paths. |
| Synth-style voice creation | [phonation_source.cpp](libs/seam-voice-design/src/phonation_source.cpp), [vocal_tract.cpp](libs/seam-voice-design/src/vocal_tract.cpp), [voice-design module](libs/seam-voice-design), [pilot executable](apps/seam-singer-pilot/main.cpp) | Recipe-owned excitation, tract behavior, articulation, and the actual generated output. |
| Voicebank production | [authoring runtime](libs/seam-authoring-runtime), [generation_campaign.cpp](libs/seam-authoring-runtime/src/generation_campaign.cpp), [voicebank CLI](apps/seam-voicebank-cli/main.cpp) | Generation, resumability, source/style identity, review, publication, signing, and installation. |
| Neural singing | [neural synthesis module](libs/seam-neural-synthesis), [worker entrypoint](apps/seam-neural-worker/main.cpp), [runtime tools](tools/neural_runtime/README.md), [training tools](tools/voice_model_training/README.md) | Admitted model bytes, supervised inference, deployment, training/export, and the distinction between test models and a qualified learned singer. |
| Native creator workflow | [editor_controller.cpp](libs/seam-native-ui/src/editor_controller.cpp), [application_controller.cpp](libs/seam-standalone/src/application_controller.cpp), [AppKit menus](libs/seam-platform/src/application_menu_appkit.mm) | Whether implemented services are reachable, undoable, visible, and useful through the application. |
| End-to-end evidence | [campaign workflow test](tests/test_original_singer_campaign_workflow.cpp), [gender tests](tests/test_gender_expression.cpp), [growl tests — currently untracked](tests/test_growl_expression.cpp), [neural snapshot tests](tests/test_neural_render_snapshot.cpp) | What each test actually exercises and which user/resource/platform claims it cannot establish. |
| Pilot resources | [pilot README](assets/pilots/seam-pilot-01/README.md), [campaign report](assets/pilots/seam-pilot-01/CAMPAIGN_REPORT.md), [coverage report](assets/pilots/seam-pilot-01/coverage-report.json) | Reproduction commands, generated/prepared inventory, and unapproved development-resource status. Coverage counts are not pronunciation scores. |
| Release authority | [release audit](scripts/run_external_beta_release_audit.py), [evidence audit](scripts/run_external_beta_evidence_audit.py), [release authorization](docs/product/EXTERNAL_BETA_RELEASE_AUTHORIZATION.md) | The actual evidence consumers and conditions for accepting an exact release candidate. |

## 6. Handoff instructions for the next developer

1. Read the scope, implementation plan, and newest execution entries first. Use the conversation for intent and rationale.
2. Run `git status --short`, `git log -10 --oneline`, and `git diff --stat`. Preserve the local growl work. A fresh clone at the recorded commit does not include the three untracked files or other uncommitted changes.
3. Review the growl source, integration, and existing test results before completing that checkpoint. Add its ledger entry and resolve source closure as part of any later authorized publication. Do not claim the entire regression suite passed in its current recorded state.
4. Trace the code and tests for the milestone being resumed. Use the September 13 plan's exit criteria; do not restart completed foundation work because an old status document says it is missing.
5. Report progress as implemented behavior, demonstrated workflow, and accepted unit/release evidence. Always name the source revision and distinguish retained evidence from fresh verification.

Suggested briefing to send with this guide:

> Please review SEAM as a full original virtual-singer product, including recording-free voice design, reusable singer-resource production, classical/neural singing, multilingual tuning, and standalone/DAW operation. Start with the revised virtual-singer contract, the September 13 implementation plan, and the newest Integrated Singer Execution entries. The repository has substantial working infrastructure and connected workflows, but Beta GO remains unaccepted. The current committed checkpoint is c9aca6fd; a growl integration is present only in the local working tree. Cross-check source and runnable evidence before quoting completion percentages or deciding what to implement next. The primary conversation is “SEAM 완성도와 개발 로드맵 평가,” task 01a02275-293a-7192-8c2e-144b9325d7dc.
