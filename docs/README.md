# Documentation map

The repository holds about 1,200 tracked files under `docs/`. Most are dated engineering records, not guidance. Use this page to tell which document governs what. Last reorganized on 2026-10-18, when the root-level plans, phase reports and duplicate status pages were retired.

## 1. Current guidance — keep these true

| Document | Use it for |
|---|---|
| [README](../README.md) | What the project is, build/run/test, Windows TODO, repository layout |
| [STATUS](STATUS.md) | Gate states and the checks behind them |
| [Completion plan](plans/2026-10-18-project-completion-plan.md) | What to do next, in what order, and when to stop |
| [Functional readiness](plans/FUNCTIONAL_READINESS.md) | What still stops the product from functioning for a user, ranked next actions, and the seven-hourly direction log |
| [Current engineering entrypoints](implementation/CURRENT_ENGINEERING_ENTRYPOINTS.md) | Which code owns each production API; legacy and compatibility surfaces |
| [Architecture overview](architecture/OVERVIEW.md) | Library layers, dependencies, data flow, structural costs |
| [DESIGN.md](../DESIGN.md) and [UI fidelity contract](design/ui-fidelity-contract-v1.json) | Main editor design authority |
| [Product principles](PRODUCT_PRINCIPLES.md) | Audio, editor and character principles |
| [AGENTS.md](../AGENTS.md) | Commit cadence, verification hygiene, evidence honesty |

## 2. Contracts and gate records — change only through the gate's own verifier

| Location | Contents |
|---|---|
| [product/](product/) | Usable Alpha ([contract](product/USABLE_ALPHA_ACCEPTANCE.md), [record](product/usable-alpha-acceptance.json)), External Beta, full-product Beta contract, public release, Windows standalone, runbooks and their JSON schemas |
| [voicebank/](voicebank/) | Beta voicebank acceptance, Japanese CVVC inventory, Beta bank dossier and session/QC templates |
| [legal/](legal/) | Voice-provider contract and synthetic-source rights requirements, clearance templates |
| [phase13a/mandatory-validation-matrix.json](phase13a/mandatory-validation-matrix.json) | Phase 13A release-gate matrix (G1–G5) |

## 3. Specifications and decisions

| Location | Contents |
|---|---|
| [formats/](formats/) | File-format specifications. Several versions coexist on purpose because readers stay compatible; the highest version of each family is current |
| [adr/](adr/) | Architecture decisions, append-only. A decision that code or practice has moved past keeps its file and gains a dated status note naming what replaced it (0003, 0005, 0018, 0021 and 0023 carry one) |
| [design/](design/) | UI fidelity contract, design system, fidelity reviews, captures and benchmarks |
| [authoring/](authoring/), [brand/](brand/), [licensing/](licensing/), [references/](references/) | Authoring behavior notes, Character 01 brand drafts, dependency and reference policy, reference ledger |

## 4. Documents shipped to users

[manual/](manual/) is the canonical offline user manual copied into bundles. [support/](support/) holds support and security-response text, and [public/](public/) holds the public-release documents. [beta/](beta/) contains short pointer files that the External Beta source contract requires. These directories are part of the release payload; edit them as product text.

## 5. Engineering records — dated and append-only

| Location | Contents |
|---|---|
| [implementation/](implementation/) | One report and evidence JSON per landed engineering unit, plus the root-owned ledger [FULL_SCOPE_BETA_EXECUTION.md](implementation/FULL_SCOPE_BETA_EXECUTION.md) |
| [reviews/](reviews/) | Audits, assessments and independent review records |
| [plans/](plans/) | Plans. The 2026-10-18 completion plan is current. [2026-09-05 full-scope Beta plan](plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md) still defines units U1–U48, and the 2026-10-08 pair records the previous goal |

A record describes its own date and commit. Do not edit an old record to make it current; write the current fact in a Section 1 document instead.

## 6. Phase records — historical, partly load-bearing

`phase1/` through `phase13b/` and the phase-numbered notes in [architecture/](architecture/) describe the August–September phases. Treat them as history. Code reads only machine-readable gate data from them:

- `phase13a/mandatory-validation-matrix.json`, the release-gate matrix for G1–G5;
- the `phase13b/` dossiers, matrix, product release dossier and evidence summaries, which hold the Official Voicebank 01 and Character 01 baselines blocked;
- `phase12c/mandatory-validation-matrix.json`, read by the Phase 13B evidence generator;
- `phase12c/LIVE_ARTICULATION.md`, a required file of the External Beta source contract.

No verifier checks the wording or presence of a phase report any more, and the schema-2 and schema-3 test projects are copies under `tests/fixtures/projects/` (completion plan M0-1 and M0-2).

## 7. Retired documents

[history/RETIRED_DOCUMENTS.md](history/RETIRED_DOCUMENTS.md) lists every document and tool removed during the 2026-10-18 cleanup, with the last commit that contained it. Recover any of them with `git show <commit>:<path>`. Links to these paths from dated records are expected to dangle.
