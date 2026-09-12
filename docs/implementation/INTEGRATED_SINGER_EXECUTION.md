# Integrated Singer Execution

Authority: `SEAM_IMPLEMENTATION_PLAN_2026-09-13.md`, preserving the original
R1–R20 and Full-Scope U1–U48 obligations. This is current execution status,
not a replacement product contract or a release approval.

## Active outcome: M1 original voice → bank → unfamiliar song

| Package | Implementation | Demonstrated workflow | Qualification remaining | Next action |
|---|---|---|---|---|
| M1.P1 | In progress: explicit draft inventory schema 2 and CLI; style/layer/take identity; unassessed requested range; legacy schema 1 retained | Pilot profile generates validated inventory through the actual CLI | Producer schema 4, migration, review/expectation and C++/Python parity still incomplete | Carry style identity into producer persistence and canonical mutation owners |
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
  style slugs cannot merge assignments. This primitive is not yet connected to
  schema-4 persistence or canonical mutation consumers.
- Rebuilt `seam_voicebank_production_tests`; all 47 C++ cases passed. The two
  focused registered CTest targets passed together. The whole Release suite has
  not been rerun; its prior full regression remains dated baseline evidence.
- Legacy producer readers intentionally do not admit schema-2 inventory yet;
  they must not discard style identity. The CLI assignment export declares its
  schema-4 producer requirement explicitly.
- No generated voice, musical review, qualified range, installed bank or Beta GO
  is claimed. The complete implementation goal remains active.

Next concrete implementation owner: production `project.hpp`, split project
codecs, assignment/take validation and canonical repository operations, followed
by generation expectations, candidate review/publication and Python parity.
