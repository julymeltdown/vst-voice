# Beta gate inventory refresh and next closable pick (2026-09-22)

Follow-up: local POSIX U29 was accepted by Developer 2 at commit 480a5e3
after held-input and regression-harness repairs. See
U29_ACCEPTANCE_AUDIT_2026-09-22.md for the precise evidence and limits.
The U29 selection discussion below describes the earlier baseline.

Scope: refreshed after the flatness/level component-ablation STOP
(assessment 0023b724). This is a navigation refresh, not a fresh audit
of all 48 units; statuses cite the execution ledger and acceptance
contracts as recorded evidence.

## Gate-level state

- External Beta contract (docs/product/external-beta-acceptance.json):
  status BLOCKED. EB-001..EB-009 all require immutable evidence
  records; EB-009 adopts the full R1-R20/V01-V18 scope.
- Neural singing (R9 / EB-009 mandatory): BLOCKED on research. The
  unvoiced-aperiodicity defect is parked, not waived; baseline UV RMS
  0.657 fails its own guardrail. Next input: human listening packet
  (2026-09-22-stage-localization, NOT_REVIEWED).
- Windows x86_64: deferred by user direction (macOS-only development
  machine); recorded in README. Any criterion naming "both desktop
  platforms" is blocked on hardware, not code.
- External gates (signing/notarization, host licenses, trademarks,
  EULA, performer/character rights, external reviewers): blocked on
  credentials/assets outside the codebase.
- U3-U5: locally accepted per the execution ledger (U5 acceptance
  audit 2026-09-06). U6 open pending renderer integration and
  remaining obligations; U7 typed-resource migration in progress.
- U29-U32 interchange: codecs + service implemented and tested;
  no formal acceptance audit recorded for U29/U30/U31; U32 needs a
  native review panel on both platforms (Windows-blocked).
- U35-U37/neural deployment: contract fixtures exist; actual model
  qualification blocked with the acoustic line.

## Closability screen

Excluded: anything needing Windows hardware (U32 panel, host tuples,
PW rows), external credentials/assets (EB-003..EB-008 external
evidence, Phase 13B gates), human listening (acoustic qualification),
or the parked neural research. Remaining code-owned candidates with
current evidence:

- U29 bounded interchange boundary: implemented
  (interchange_service.cpp, symlink admission, bounded reads, draft
  imports, create-new export), three service test cases plus codec
  suites pass, but no criterion-to-evidence acceptance audit and no
  recorded coverage of every plan test scenario.
- U6 performance compiler: large, explicitly open on renderer
  integration; not closable in a bounded step.
- U31 SMF codec: codec complete, but its verification criterion
  requires a real-DAW melody exchange - external dependency.

## Pick: U29 acceptance audit and scenario closure

Owner: this thread (root agent), files
libs/seam-authoring-runtime/src/interchange_service.cpp,
libs/seam-interchange/{include,src}, tests/test_interchange_service.cpp.

Binary completion test: a U29 acceptance audit document maps every
plan criterion and test scenario to named code and test evidence, AND
the three plan scenarios each have at least one passing test:
(1) oversized input/declared allocations rejected before allocation;
(2) symlink, reparse, parent replacement and changed input bytes
cannot silently redirect a held import; (3) interrupted output or
destination conflict preserves the original file and document.
Verification: Debug+Release runs of seam_interchange_service_tests and
seam_ustx_interchange_tests/seam_smf_interchange_tests all pass.

Known gaps to close: reparse/parent-replacement and changed-bytes
holding tests are not recorded; oversized-allocation rejection needs an
explicit named test; the audit document does not exist.

Explicitly out of scope for the pick: USTX field coverage breadth
(U30), real-DAW exchange (U31 verification), native review panel
(U32), and any Beta-GO promotion claim.
