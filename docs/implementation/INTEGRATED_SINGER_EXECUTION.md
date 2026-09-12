# Integrated Singer Execution

Authority: `SEAM_IMPLEMENTATION_PLAN_2026-09-13.md`, preserving the original
R1–R20 and Full-Scope U1–U48 obligations. This is current execution status,
not a replacement product contract or a release approval.

## Active outcome: M1 original voice → bank → unfamiliar song

| Package | Implementation | Demonstrated workflow | Qualification remaining | Next action |
|---|---|---|---|---|
| M1.P1 | In progress: inventory v2; producer v4 codec and raw import ownership; unassessed requested range; legacy readers retained | Pilot inventory CLI and C++ two-style workspace import/save/recovery tests | Explicit legacy migration, Python producer parity, generation-job/CLI wiring and multi-style publication incomplete | Connect the inventory to the canonical v4 writer, then finish generation and migration |
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
- Rebuilt the complete configured Release tree and ran all **122 CTest targets:
  122 passed, zero failed** (86.13 seconds). The producer target now has 49 cases.
- Legacy producer readers intentionally do not admit schema-2 inventory yet;
  they must not discard style identity. The CLI assignment export declares its
  schema-4 producer requirement explicitly.
- No generated voice, musical review, qualified range, installed bank or Beta GO
  is claimed. The complete implementation goal remains active.

Next concrete implementation owners: Python draft definition/validation parity,
`generation_job.cpp` and the generation-collection CLI (currently reconstructing
style-free raw inputs), explicit evidence-backed legacy migration, then complete
candidate review/publication parity. Existing expectation state hashes bind the
workspace language indirectly; its explicit versioned language contract remains
to be added with the generation integration. No M1 completion is claimed.
