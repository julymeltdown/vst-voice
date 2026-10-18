# Retired documents and tools

Files removed from the tree during the 2026-10-18 cleanup and the M0 repository reset that followed it. Git history keeps every one of them. To read one, run `git show <commit>:<path>` with the commit named in its group. Links to these paths from dated records are expected to dangle. Earlier unavailable attachments are listed separately in the [historical evidence availability index](../reviews/HISTORICAL_EVIDENCE_AVAILABILITY_2026-10-11.md).

When a document stops being true, delete it and add it here instead of leaving it in place with a historical banner.

**107 files in 11 groups.**

## Dead code and inert build option

Last present in `6036ae198d52`. Never included; no symbol users; the option only raised a fatal configure error. Replacement: the SING shell on AppKit/CoreGraphics (ADR 0018); [architecture overview](../architecture/OVERVIEW.md).

| Path | Blob | Bytes |
|---|---|---:|
| `docs/architecture/IPLUG2_SKIA_ADAPTER.md` | `0cc05a02edf4` | 2,653 |
| `libs/seam-core/include/seam/core/hash.hpp` | `c1d1a07f29ac` | 1,076 |
| `libs/seam-distribution/include/seam/distribution/signer_provider.hpp` | `02980c6ee8f4` | 445 |
| `libs/seam-live-voice/include/seam/live_voice/expression.hpp` | `4bb9a70a833e` | 939 |
| `libs/seam-platform/include/seam/platform/desktop_backend.hpp` | `6bd6c40ddc7a` | 948 |

## Historical evidence generators and their only consumers

Last present in `fd0893da705b`. The generators rewrote tracked historical evidence under docs/phaseN at a new commit; the demo, benchmarks and SVG renderer were used only by them. Replacement: historical evidence stays in docs/phaseN; the maintained benchmark is seam_phase5_benchmark.

| Path | Blob | Bytes |
|---|---|---:|
| `apps/seam-phase1-demo/main.cpp` | `242a517b6626` | 7,016 |
| `benchmarks/phase10_benchmark.cpp` | `9601434442b7` | 1,750 |
| `benchmarks/phase1_benchmark.cpp` | `4a91dd1cbf76` | 2,122 |
| `benchmarks/phase2_benchmark.cpp` | `d100bfda6851` | 5,402 |
| `benchmarks/phase3_benchmark.cpp` | `17333183c78d` | 10,194 |
| `benchmarks/phase4_benchmark.cpp` | `98a609dc1bd3` | 11,856 |
| `benchmarks/phase9_benchmark.cpp` | `6d9590b82dbf` | 2,284 |
| `libs/seam-editor-ui/include/seam/ui/svg_renderer.hpp` | `5be96a153cb9` | 634 |
| `libs/seam-editor-ui/src/svg_renderer.cpp` | `8f06c7aa1ef9` | 6,840 |
| `scripts/generate_phase10_evidence.py` | `e729114f2409` | 7,025 |
| `scripts/generate_phase1_evidence.py` | `db3e3294d8e9` | 3,141 |
| `scripts/generate_phase2_evidence.py` | `14cf651487af` | 6,191 |
| `scripts/generate_phase3_evidence.py` | `3e5e802087b2` | 10,887 |
| `scripts/generate_phase4_1_evidence.py` | `152bd3bf3d98` | 15,802 |
| `scripts/generate_phase4_evidence.py` | `cb608832c56c` | 14,921 |
| `scripts/generate_phase5_evidence.py` | `cdd7123f316d` | 9,208 |
| `scripts/generate_phase8_evidence.py` | `6dc2ee01a64a` | 7,281 |
| `scripts/generate_phase9_evidence.py` | `ba47fda78ffd` | 11,178 |

## Unrelated document committed by mistake

Last present in `41dc63fd0b7f`. Implementation guide for a different product (a Java audit-evidence service), unrelated to Project SEAM. Replacement: none.

| Path | Blob | Bytes |
|---|---|---:|
| `TextFlow_Audit_Evidence_Service_Implementation_Guide_v1_0.md` | `03c900c56de6` | 66,438 |

## Root-level phase implementation reports

Last present in `41dc63fd0b7f`. Phase 1–13B reports from August; their claims describe superseded states. Replacement: [STATUS](../STATUS.md); phase records remain under docs/phaseN.

| Path | Blob | Bytes |
|---|---|---:|
| `PHASE10_IMPLEMENTATION_REPORT.md` | `c4221d026966` | 5,794 |
| `PHASE10_IMPLEMENTATION_REPORT_KO.md` | `de786098bbd1` | 8,851 |
| `PHASE11_IMPLEMENTATION_REPORT.md` | `b4641ff5c19e` | 1,966 |
| `PHASE11_IMPLEMENTATION_REPORT_KO.md` | `a5cdbd18f385` | 5,199 |
| `PHASE12A_IMPLEMENTATION_REPORT.md` | `7f091f20c260` | 858 |
| `PHASE12B_IMPLEMENTATION_REPORT.md` | `c515473e9c37` | 408 |
| `PHASE13A_IMPLEMENTATION_REPORT.md` | `ae69ff0133bc` | 1,575 |
| `PHASE13A_IMPLEMENTATION_REPORT_KO.md` | `6e2d9f7a0751` | 4,464 |
| `PHASE13B_IMPLEMENTATION_REPORT.md` | `c65ef0f2c5e3` | 1,464 |
| `PHASE13B_IMPLEMENTATION_REPORT_KO.md` | `5e78d8cd9d9f` | 7,254 |
| `PHASE1_IMPLEMENTATION_REPORT.md` | `dfd113d3386d` | 8,713 |
| `PHASE2_IMPLEMENTATION_REPORT.md` | `0bff79dc052c` | 17,028 |
| `PHASE3_IMPLEMENTATION_REPORT.md` | `9d98b1fcbe0d` | 18,139 |
| `PHASE4_1_IMPLEMENTATION_REPORT.md` | `6d90bb1c7291` | 16,458 |
| `PHASE4_IMPLEMENTATION_REPORT.md` | `fdb83bdd21ef` | 16,784 |
| `PHASE5_1_IMPLEMENTATION_REPORT.md` | `dc5535d35895` | 2,489 |
| `PHASE5_IMPLEMENTATION_REPORT.md` | `140d64569b34` | 13,044 |
| `PHASE6_IMPLEMENTATION_REPORT.md` | `23590361d977` | 1,582 |
| `PHASE7_IMPLEMENTATION_REPORT.md` | `89c5e4599c9a` | 1,600 |
| `PHASE8_IMPLEMENTATION_REPORT.md` | `e7ed06340364` | 6,240 |
| `PHASE9_IMPLEMENTATION_REPORT.md` | `6d0a26e2c3d0` | 1,592 |

## Root-level dated plans, reviews and logs

Last present in `41dc63fd0b7f`. Plans and direction reviews from 2026-09-09 to 2026-09-25, superseded several times over. Replacement: [completion plan](../plans/2026-10-18-project-completion-plan.md).

| Path | Blob | Bytes |
|---|---|---:|
| `BETA_VOICEBANK_EXPERT_REVIEW_REQUEST.md` | `bfb1794c670a` | 39,073 |
| `DEVELOPER_READING_GUIDE_2026-09-15.md` | `0bbd842a7948` | 15,668 |
| `DEVELOPMENT_DIRECTION_CHECKPOINT_REVIEW_2026-09-09_EN.md` | `2c872c0459a7` | 40,785 |
| `DEVELOPMENT_DIRECTION_CHECKPOINT_REVIEW_2026-09-09_KO.md` | `8e818d3e7ffa` | 43,979 |
| `DEVELOPMENT_DIRECTION_DEEP_REVIEW_2026-09-09_KO.md` | `e4788067c5b9` | 43,489 |
| `DEVELOPMENT_DIRECTION_POST_INTEGRATION_REVIEW_2026-09-09_KO.md` | `c3c79f53664c` | 47,822 |
| `DEVELOPMENT_DIRECTION_REVIEW_REFRESH_2026-09-09_KO.md` | `34f69ab75588` | 44,567 |
| `SEAM_AUTOMATED_VERIFICATION_AND_ACCELERATION_2026-09-19.md` | `20bb85a76670` | 61,323 |
| `SEAM_COMPLETION_PLAN_2026-09-22_KO.md` | `2970177cc3b5` | 23,681 |
| `SEAM_COMPLETION_REVIEW_2026-09-20.md` | `4572f04fb12a` | 22,283 |
| `SEAM_DETAILED_DEVELOPMENT_PLAN_2026-09-19.md` | `0dd86971d01e` | 24,587 |
| `SEAM_DEVELOPMENT_PLAN_R2_2026-09-15.md` | `c623bb1ccf38` | 23,655 |
| `SEAM_FINAL_PROGRESS_AND_FINISHING_PLAN_2026-09-22_KO.md` | `ca456c6cd6e6` | 27,269 |
| `SEAM_IMPLEMENTATION_PLAN_2026-09-13.md` | `f578f4456d6d` | 93,929 |
| `SEAM_JOINT_DEVELOPMENT_PLAN_R3_2026-09-15.md` | `7d66bcdafbb3` | 54,650 |
| `SEAM_JOINT_DEVELOPMENT_PLAN_R4_2026-09-16.md` | `b07a77d28fda` | 44,292 |
| `SEAM_PROGRESS_AND_FINISHING_PLAN_2026-09-22_KO.md` | `7ea4e3327afc` | 25,768 |
| `SEAM_PROGRESS_AND_FINISHING_PLAN_2026-09-23_KO.md` | `99625b362792` | 31,671 |
| `SEAM_PROGRESS_AND_REVISED_ROADMAP_2026-09-12.md` | `cc18c4c7c26f` | 57,241 |
| `SEAM_PROGRESS_REPORT_2026-09-25.md` | `be7babf9e8bc` | 5,582 |
| `SEAM_REVISED_DEVELOPMENT_PLAN_2026-09-15.md` | `9dc465e2fcd0` | 41,730 |
| `SEAM_SECOND_DEVELOPER_REVIEW_2026-09-15.md` | `6eed039d8724` | 30,613 |
| `direction-review-2026-09-09.log` | `a2011892e6d7` | 117,112 |

## Stale status, backlog and readiness pages

Last present in `41dc63fd0b7f`. Dated snapshots that disagreed with the gates; two were unmarked and still described August states. Replacement: [STATUS](../STATUS.md).

| Path | Blob | Bytes |
|---|---|---:|
| `docs/PROGRESS-AUDIT-2026-10-08.md` | `39bb1e8bdbcc` | 9,439 |
| `docs/RELEASE_READINESS.md` | `268cdac44eb1` | 1,094 |
| `docs/RELEASE_READINESS_KO.md` | `ade79e6e16eb` | 2,572 |
| `docs/REMAINING_TASKS.md` | `1c3c8fde3618` | 2,027 |
| `docs/REMAINING_TASKS_KO.md` | `ac843f6b4dba` | 4,329 |
| `docs/STATUS_KO.md` | `01f383907be3` | 4,426 |

## Superseded plans and agent planning files

Last present in `41dc63fd0b7f`. August–September plans and tool-generated plan and spec drafts. Replacement: [completion plan](../plans/2026-10-18-project-completion-plan.md); U1–U48 are still defined by the 2026-09-05 plan.

| Path | Blob | Bytes |
|---|---|---:|
| `.superpowers/sdd/2026-08-18-phase12c-implementation-index/progress.md` | `0bbea54ad2df` | 549 |
| `docs/plans/2026-08-21-1901-feat-project-seam-external-beta-plan.md` | `b90e6409b592` | 257,335 |
| `docs/plans/2026-08-26-0134-fix-engineering-candidate-completion-plan.md` | `9daa43de8aed` | 68,539 |
| `docs/plans/2026-08-27-0346-feat-native-editor-design-completion-plan.md` | `4662f6fe5b07` | 61,463 |
| `docs/plans/2026-08-30-2246-feat-production-readiness-completion-plan.md` | `64afd2bbce33` | 112,541 |
| `docs/plans/2026-09-01-0303-feat-creator-workflow-parity-plan.md` | `95713c14ae17` | 80,657 |
| `docs/superpowers/plans/2026-08-18-phase12c-implementation-index.md` | `3c4af188573e` | 3,941 |
| `docs/superpowers/plans/2026-08-18-phase12c-linux-runtime-validation.md` | `42fb72cb398f` | 17,016 |
| `docs/superpowers/plans/2026-08-18-phase12c-live-voice-articulation.md` | `8f515f62ca0c` | 26,169 |
| `docs/superpowers/plans/2026-08-18-phase12c-target-os-and-daw-gates.md` | `371fed739a52` | 12,921 |
| `docs/superpowers/plans/2026-08-18-phase13a-distribution-certification.md` | `19df06010379` | 3,594 |
| `docs/superpowers/plans/2026-08-18-phase13b-content-ip-release.md` | `83c6ae2fe7e5` | 5,023 |
| `docs/superpowers/plans/2026-08-18-phase13b-official-voicebank-character-release.md` | `95b4b5cbf49f` | 5,907 |
| `docs/superpowers/plans/2026-08-18-usable-standalone-product.md` | `dc2ab71f1f24` | 83,078 |
| `docs/superpowers/specs/2026-08-18-phase12c-runtime-validation-design.md` | `1296419793e1` | 15,676 |
| `docs/superpowers/specs/2026-08-18-phase13b-content-ip-release-design.md` | `1524c419d370` | 5,344 |
| `docs/superpowers/specs/2026-08-18-phase13b-official-voicebank-character-release-design.md` | `126a2aa576e4` | 5,279 |

## Dated product implementation reports

Last present in `41dc63fd0b7f`. U1–U3 reports and the August Usable Alpha progress log, filed beside the canonical contracts. Replacement: [Usable Alpha contract](../product/USABLE_ALPHA_ACCEPTANCE.md) and [STATUS](../STATUS.md).

| Path | Blob | Bytes |
|---|---|---:|
| `docs/product/ENGINEERING_REMEDIATION_GOAL.md` | `945467f024e9` | 10,903 |
| `docs/product/U1_IMPLEMENTATION_REPORT.md` | `3b7fb5db670a` | 8,764 |
| `docs/product/U1_SHARED_AUTHORING_RUNTIME_COMPLETION.md` | `ff14218aa245` | 1,451 |
| `docs/product/U2_IMPLEMENTATION_REPORT.md` | `68f7aa1ec859` | 10,910 |
| `docs/product/U3_IMPLEMENTATION_REPORT.md` | `d20afcc499d7` | 2,227 |
| `docs/product/U3_VOICEBANK_WORKFLOW.md` | `a982b9058612` | 2,364 |
| `docs/product/USABLE_ALPHA_IMPLEMENTATION_PROGRESS.md` | `3ecec7d95dc7` | 61,288 |
| `docs/product/USABLE_ALPHA_U1_3_IMPLEMENTATION.md` | `9ccd8927a9e3` | 2,665 |

## Superseded architecture and UX notes

Last present in `41dc63fd0b7f`. Phase 1–2 notes whose dependency maps and backlogs no longer match the build. Replacement: [architecture overview](../architecture/OVERVIEW.md).

| Path | Blob | Bytes |
|---|---|---:|
| `docs/architecture/MODULE_BOUNDARIES.md` | `358f4fa8a5c7` | 2,192 |
| `docs/architecture/PHASE2_BACKLOG.md` | `c476182adee5` | 2,010 |
| `docs/solutions/.gitkeep` | `8b137891791f` | 1 |
| `docs/ux/PHASE1_INTERACTIONS.md` | `fb0a80e7e65e` | 1,176 |

## Agent tool artifacts (untracked; local copies kept)

Last present in `41dc63fd0b7f`. Tool output force-added under the ignored .omo/ directory. Replacement: none.

| Path | Blob | Bytes |
|---|---|---:|
| `.omo/evidence/native-editor-design-completion.md` | `71bf99ee6378` | 4,374 |
| `.omo/evidence/project-seam-af5-gate-review.md` | `5dccafc632b8` | 5,600 |

## Phase-era verifier history locks (M0-2)

Last present in `228daaa1ae69`. The backlog had been stale since Phase 13B; two verifiers and one test read it only to lock historical task states. The verifier duplicated the Phase 11 contract check and never ran in CTest; only its path list was imported. Replacement: [STATUS.md](../STATUS.md) and the [completion plan](../plans/2026-10-18-project-completion-plan.md) for status and work; `scripts/verify_clap_editor_sources.py` for the CLAP editor sources.

| Path | Blob | Bytes |
|---|---|---:|
| `docs/remaining-tasks.json` | `3f0dfd344f56` | 3,483 |
| `scripts/verify_phase11_sources.py` | `7c22e4b747e2` | 4,352 |
