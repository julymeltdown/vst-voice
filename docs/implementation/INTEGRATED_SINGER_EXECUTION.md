# Integrated Singer Execution

Authority: `SEAM_IMPLEMENTATION_PLAN_2026-09-13.md`, preserving the original
R1–R20 and Full-Scope U1–U48 obligations. This is current execution status,
not a replacement product contract or a release approval.

## Active outcome: M1 original voice → bank → unfamiliar song

| Package | Implementation | Demonstrated workflow | Qualification remaining | Next action |
|---|---|---|---|---|
| M1.P1 | In progress: inventory v2→producer v4; language-bound generation expectation v2; unassessed requested range; legacy readers retained | Python prepare-draft→C++ init-production→Python verification for two styles; schema-4 score→job→render→CLI collection regression | Explicit legacy migration and complete multi-style review/publication parity remain | Implement evidence-backed migration, then finish multi-style publication and campaign |
| M1.P2/P3 | Not completed by this increment | Existing procedural/producer foundation retained | Connected articulation, campaign, actual bank and unfamiliar-song evidence | Continue after the necessary M1.P1 producer bindings |
| M2–M6 | Remaining full scope retained | No new milestone qualification | As specified by the implementation plan | Independent neural process/data work remains available |

## Verification checkpoint — September 13, 2026

- Nine draft tests plus eight legacy inventory tests passed (17 total), including
  real CLI generation, exact numeric types, bounded IDs and legacy-writer rejection.
- CMake configure succeeded and registered `seam_draft_inventory_tests` passed.
- The focused CTest target was rerun after the bounded-ID case was added.
- The producer now defines `ProductionUnitIdentity` with exact language/style/
  coverage/layer equality and a canonical inventory-v2 SHA256. Python-generated
  rows and C++ agree on ASCII and quoted Japanese-label golden vectors; distinct
  style slugs cannot merge assignments.
- Producer schema 4 now persists a workspace language and per-assignment/take
  style, checks four-axis duplicate/retake ownership, and retains legacy 1–3
  serialization. Raw and generated import matching use style; source assessment
  retains v4 and includes language/style in its material identity. Review and
  single-style manifest paths reject relabeling. Multi-style publication is
  deliberately not enabled before all consumers are integrated.
- Tests import identical PCM into two distinct styles, recover the durable
  workspace, and reject cross-style retakes, missing styles, language changes,
  and relabeling existing takes without mutating saved state. Generic save cannot
  masquerade as legacy migration; the explicit migration operation is pending.
- At checkpoint `5291e652`, rebuilt the complete configured Release tree and ran all **122 CTest targets:
  122 passed, zero failed** (86.13 seconds). The producer target now has 49 cases.
- Following that checkpoint, score-job preparation and CLI collection now carry
  assignment style. Expectation v2 carries explicit language; legacy expectation
  v1 retains its format. Tests prepare/load/render a schema-4 job, collect it via
  the actual CLI, and repeat collection without creating another generation.
  Wrong-style preparation and wrong-language collection fail without changing
  producer state; malformed v2 language/version fields are rejected.
- Rebuilt the complete Release tree after the generation integration. The three
  focused CTest targets (export workflow, draft inventory, producer) passed.
  The 122-target run above predates this latest integration, not a fresh claim.
- Version-aware Python definition preparation now maps inventory v2 to producer
  v4 without repeating workspace language in assignment rows. C++ init-production
  admits empty v4 drafts and still rejects preapproved/imported material. Python
  verification checks style keys, retake binding, immutable language/take identity
  and schema-4 source-quality material hashes. Legacy generic inventory readers
  remain unchanged.
- New parity tests execute both preparation and initialization CLIs, then verify
  the resulting two-style workspace in Python. Missing language/style, duplicate
  assignments and schema relabeling are rejected by both readers. All 33 focused
  Python tests passed; registered draft inventory, external-beta Python contract
  and sample-review CLI tests also passed. This is not exhaustive full-product
  or populated multi-style publication qualification.
- No generated voice, musical review, qualified range, installed bank or Beta GO
  is claimed. The complete implementation goal remains active.

## Multi-style review integration

- Schema-4 workspaces can now prepare and apply a multi-style review packet
  through the shared review service. Legacy style-free workspaces remain rejected.
- A synthetic regression uses two takes with identical PCM, phone coverage and
  pitch but different styles. Reviewing the first does not review the second;
  the second requires a fresh packet and its own explicit decision. Stale packets
  and relabeling into the other style fail, and durable recovery retains both
  separate review records.
- Producer, native Studio review and sample-review CLI targets rebuilt and all
  three focused CTest targets passed. The producer target has 50 cases.
- This admits review, not multi-style publication. Publication still needs its
  complete per-style coverage checks and downstream candidate parity; the Python
  legacy candidate contract still uses coverage/pitch pairs. No musical approval
  outside the explicitly synthetic tests was created.

Next concrete implementation owners: explicit evidence-backed legacy migration,
then complete populated-workspace parity and candidate
review/publication parity and the resumable inventory campaign. The generation
test uses synthetic diagnostic material, not a qualified singer. No M1 completion
is claimed.
