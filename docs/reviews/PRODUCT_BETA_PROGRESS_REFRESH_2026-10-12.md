# Project progress and Product/Beta blockers — current refresh

Date: 2026-10-12, Asia/Seoul. Source HEAD: **`257a2c4698f7db039ade7ea061af6f507f448ba0`**. Fetch and fast-forward pull ran first; local HEAD, tracking master and live remote master agreed. Preserved working-tree change: two new failing regression probes in `tests/test_sing_shell_journey.cpp`; the existing brand-fonts/tooltips stash is unchanged. Production C++ was not modified in this refresh.

## Progress that can be stated accurately

**Product and External Beta remain BLOCKED. A defensible single current overall implementation percentage is not established.** There is substantial working code and a native engineering song, but the available percentages have different denominators:

| Measure | Current result and qualification |
|---|---|
| RS-01–11 remediation task document | **100% closed dispositions: 11/11**, including two permitted NO_CHANGE investigations. This is not full-project progress. |
| Previous U1–U48 local acceptance labels | **12.5% = 6/48**, historical local scope, including POSIX-only U29. Those six exits were not all independently renewed in this refresh. |
| Historical weighted implementation index | **54.17%**, retired. The weights and equal unit sizes are judgments; it is not a fresh census, remaining-effort estimate or release probability. |
| Canonical Usable Alpha | **0% = 0/20 PASS**; all 20 are NOT_RUN, no canonical evidence records, gate BLOCKED. |
| External Beta evidence | **0 accepted records for nine mandatory categories**, BLOCKED. This measures recorded acceptance, not implemented code. |
| Full Product contract | 20 requirements, 18 work packages, 83 registered cases; registry valid, evidence NOT_RUN. **No current accepted full-product completion percentage.** |
| Qualified released resources | **0**; resource matrix and evaluation profile unresolved; 13 pending profile/matrix entries; U45 canonical semantic validation UNAVAILABLE/BLOCKED. |

Returning a new exact-looking percentage would conceal unresolved acceptance and unmeasured partial work. The previous [full 48-unit source/residual inventory](OVERALL_PRODUCT_BETA_REASSESSMENT_2026-10-12.md) remains applicable: Git comparison from its `ea37cd34` baseline to current HEAD changes documentation and the observation launcher, with no changes under `apps/` or `libs/`. This refresh adds runtime evidence, a specific UI failure, and the owner's internet-source route; it does not renew all original exits.

## Fresh execution and integrity evidence

The [evidence index](PRODUCT_BETA_PROGRESS_REFRESH_2026-10-12.evidence.json) binds raw logs, current inputs and the preserved test patch under `out/evidence/overall-readiness-refresh-2026-10-12/`. The source snapshot explicitly records a dirty checkout; these tests are not a signed release candidate.

| Run actually performed | Result and limit |
|---|---|
| Full `cmake --build build/release -j6` | PASS, exit 0, before focused tests. Dependent binaries are current. |
| Expression-on-song, original-singer song journey, procedural install journey, singer-pilot CLI, sample-review CLI | **5/5 CTest entries PASS, 41.68 seconds**. Scoped automated macOS engineering evidence, not full-suite or human acceptance. |
| New installer and refresh semantic-action probes | **Both FAIL**, each exit 1. These intentionally expose currently disconnected callbacks and remain uncommitted. |
| Alpha and Phase13A source contract verifiers | PASS; canonical Alpha stays BLOCKED and external runtime remains NOT_RUN. Validator success does not mean product success. |
| Current full-product registry/profile and Phase13A evaluator | No configuration errors; 13 pending entries; G2–G5 BLOCKED. Historical Phase13A target lists are separate from the nine mandatory full-product host tuples. |
| Frozen native observation kit | **95/95 file hashes verified**; the retained 30-second procedural project/apps remain intact. Human observation is NOT_OBSERVED. |
| Indexed-file census | **3,050 tracked paths; zero missing**. This does not establish that required production assets or unavailable historical proof exist. |

The current project is not merely waiting for paperwork. It has concrete incomplete behavior and resource delivery alongside absent human/platform/release evidence.

The callback failure is an integration-boundary defect: the shared UI declares an action, but the standalone composition omits it at two layers. Passing renderer/producer journeys therefore coexist with broken visible controls. This supports adding focused end-to-end command coverage and repairing that composition; it does not by itself justify rewriting the synthesis architecture. The larger sequencing risk remains developing extensive infrastructure before qualifying a useful singer and its ordinary creator workflow. Acquiring actual source material advances that question, but only the subsequent cohort and listening trials can answer it.

## Remaining Product/Beta tasks and priority

| Priority | Task and code-level evidence | Required exit |
|---|---|---|
| **P0.1** | Finish standalone installer/refresh wiring. `authoring_session.cpp:336` forwards selection but drops installer/refresh callbacks; `native_editor_app.cpp:418` also only constructs selection. `editor_controller.cpp:1159`/`:1167` returns Unsupported when these callbacks are absent. Both current semantic-action regressions fail. | Reuse `StandaloneApplicationController`'s real refresh (`application_controller.cpp:1007`) and existing InstallVoicebank command (`:1485`), forward callbacks through AuthoringSession, and verify both UI routes plus cancel/no-document-mutation. Full dependent rebuild and focused journey checks; review, commit/push. Do not replace real work with a success no-op. |
| **P0.2** | Use internet recordings for W05a, replacing generated sample takes. [Acquired PJS corpus and source task](../implementation/INTERNET_SAMPLE_SOURCE_INTAKE_2026-10-12.md) supplies a concrete trial. Original labels are structurally defective; pinned manual labels pass bounded checks. | Source-aware draft cohort, actual coverage/pitch/marker assessment, conditional license obligations and distinct producer/reviewer approvals; then exact sample candidate/install. PJS is male and cannot close the original female-singer requirement. |
| **P0.3** | Observe actual musical usefulness and creator workflow. The frozen engineering kit is ready, but no owner listening/ranked-problem record is supplied. W01's recipe path still lacks the sample preutterance properties its Q1 requires. | Owner observation and ranked quality/workflow decision; same-material repair; unchanged full sample-path Q1–Q3 packet; real unaided canonical UA-001–020 with production sample hearing and external playback. No speculative subjective DSP changes before observation. |
| **P1.1** | Typed resources and learned product delivery: `candidate_package.cpp:415` explicitly refuses model installation. Dictionary/character candidate/runtime closure and packaging/recovery obligations also remain. U35–38 experiments do not establish qualified installed models. | Complete installed model/helper/dependency lineage, source-authorized selected acoustic/vocoder candidate and independent combined-model cohort; usable editable automatic performance. Complete all required resource kinds, with actual positive capability cells. |
| **P1.2** | Musical breadth: U6–8/U16–17/U25–28/U30–34/U39–41 retain compiler, renderer, style/expression, language, interchange, live/offline-host and character exits. Existing isolated passes/refusals do not establish all mandatory combinations. | Close original exits against measured positive behavior and independent language/music/creator review. Preserve manual edit ownership and exact resource/capability identity. |
| **P1.3** | Governed resource and quality qualification: U42/U43; releasedResources empty, source dossier empty/BLOCKED, 13 pending profile/matrix entries. | Actual lawful resources for declared scope; freeze measured criteria/matrix; 60 phrases per language, three songs, five independent creators and independent reviews. A PJS trial is acquisition progress, not these final deliverables. |
| **P1.4** | U44–46 support/recovery and semantic replay: `candidate_package_commands.cpp:230` explicitly refuses sample/model operation capture; `:233` refuses backing-media attestation. U45 reconciliation hold remains. | Native resource/dependency capture and operation/continuity/measurement replay, installed privacy/crash/recovery, external reviewer authority and shared promotion/restore predicates. Preserve refusals until the corresponding implementation and evidence exist. |
| **P2.1** | U47 installed release qualification: both platforms, nine mandatory commercial-host tuples, real accessibility, signed/notarized artifacts, install/update/uninstall and 30/120-minute workloads. README retains Windows TODO; its editor vector backend is also not implemented. | Actual candidate-bound macOS/Windows runs and independent acceptance. Continue macOS development while Windows is deferred; full Beta cannot pass without its mandatory Windows obligations. |
| **P2.2** | U48 final release: immutable externally anchored archive, restored raw proof and authorized decision. EB-001–009 currently have no accepted records. | Full contract replay, no Blocker/Critical defects, accountable independent release decision and authorized READY. Cohort CLOSED/public activation are subsequent distinct decisions. |

Also repair the reporting mismatch before displaying an accepted Alpha row: `release_status.py:106` checks COMPLETED, while the canonical verifier accepts PASS. Today's independently counted 0/20 is unaffected; the seven-row status page is not a whole-project denominator.

## Next development goal

> Safely fetch/pull and preserve existing work. First complete the standalone installer/refresh callback repair using existing application commands, with cancel/no-mutation and real-catalogue regressions, a full dependent rebuild, independent technical review and a verified commit/push. Then prepare a bounded, source-bound PJS/manual-label sample cohort through the normal producer import path, retaining exact source/license/label/cut hashes and actual coverage gaps. Present concrete material for distinct producer/reviewer and owner musical judgments; package/install only the genuinely approved generation. Run the unchanged W01 sample-path questions and canonical unaided Alpha when their real prerequisites exist. Repair the largest observed musical/workflow defect on the same material. Preserve every original U1–48/R1–20/V01–18, language, original-female, learned-resource, Windows, host, signing, workload, privacy/recovery/archive and release obligation.

The existing full-scope goal remains active. Source acquisition is completed; the import/cohort, callback repair, owner observation and approvals remain open. Engineering work can prepare these inputs now; it cannot invent musical verdicts, rights outside a source license or named people.

## Technical review status at publication

Critical review was requested from the existing **Review in English** chat (`01a11bad-88f2-7521-849e-c38111dc2197`). Its verdict is **PENDING** at this checkpoint. Lead source/execution/hash checks above are completed; no independent approval is claimed. Any actionable review findings require a subsequent corrected, verified and pushed unit. This does not reopen or override the earlier approved full-scope assessment.
