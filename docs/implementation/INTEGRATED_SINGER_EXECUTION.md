# Integrated Singer Execution

Authority: `SEAM_IMPLEMENTATION_PLAN_2026-09-13.md`, preserving the original
R1–R20 and Full-Scope U1–U48 obligations. This is current execution status,
not a replacement product contract or a release approval.

## Active outcome: M1 original voice → bank → unfamiliar song

| Package | Implementation | Demonstrated workflow | Qualification remaining | Next action |
|---|---|---|---|---|
| M1.P1 | In progress: inventory v2→producer v4; language-bound generation; multi-style draft/review/publication; legacy readers retained | Two-style initialization, generation collection, native-controller draft creation, review and candidate reopen regressions | Explicit legacy migration and populated-history parity remain | Complete migration, then campaign/articulation |
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
  single-style manifest paths reject relabeling. The later publication checkpoint
  below admits complete schema-4 style matrices through the canonical publisher.
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
- This review checkpoint alone did not admit publication. See the subsequent
  publication integration below. No musical approval outside the explicitly
  synthetic tests was created.

## Multi-style candidate publication integration

- The canonical publisher now admits schema-4 multi-style workspaces only when
  declared styles exactly match assignment ownership, all styles have the same
  required phone/pitch matrix, and every current assignment has its own valid
  take and retained independent review. Legacy workspaces keep the single-style
  restriction. No approval is inferred from shared PCM.
- Regression coverage publishes and reopens a two-style candidate, verifies its
  manifest and content hash, and rejects omitted styles, reused review evidence,
  extra declared styles and asymmetric phone/pitch requirements.
- Schema-4 source qualification now requires an explicit current source-quality
  assessment. C++ and Python no longer allow the historical no-assessment fallback
  for these workspaces. Source execution remains separate and does not need a
  musical PASS. Test evidence is expressly synthetic, not a real evaluation.
- Scope clarification from code inspection: Python `_production_candidate.py`
  validates a separate legacy `READY`/`unitBindings` export contract, not the
  canonical C++ `com.project-seam.resource-candidate` descriptor. Its pair-based
  legacy contract is not being reinterpreted as authority for these candidates.
- This remains an engineering candidate with `releaseEligible: false`. Native
  multi-style draft authoring, explicit legacy migration, generation campaigns,
  real singer quality and the remaining M1–M6 obligations are still unfinished.
- Verification after this integration: complete configured Release build passed;
  **122/122 registered CTest targets passed**, zero failures (82.22 seconds).

## Native multi-style draft creation

- The shared draft builder now derives all schema-4 styles from assignments and
  generates style-distinct unit IDs. Missing takes retain style-qualified labels.
  The selected identity style must belong to the workspace; it cannot relabel or
  filter its other assignments. Legacy workspaces retain explicit single-style
  behavior. CLI help and Studio's progress status explain the all-styles behavior.
- Tests create a partial and complete two-style draft, prepare it for review,
  reject an unknown style, and prove that selecting either existing style retains
  identical manifest content. A native-controller test asynchronously creates and
  opens both styles without changing producer state or creating reviews.
- Shared manifest, Studio manifest and sample-review CLI targets passed after
  rebuilding affected targets. The prior 122-target run predates this increment;
  no fresh desktop visual QA or musical qualification is claimed.

## Legacy migration preparation

- Added `python3 -m tools.external_beta.voicebank_production prepare-style-migration
  --workspace WORKSPACE --inventory LEGACY_INVENTORY --output NEW_PLAN_JSON`.
  It verifies durable history and matching inventory, captures the source bytes'
  SHA256/generation and inventory evidence, and writes only a new plan outside
  the workspace. Existing output paths are not overwritten.
- A singleton style in the validated legacy inventory can resolve ownership.
  Multi-style legacy inventories produce `UNRESOLVED` with per-assignment reasons;
  the planner does not guess a selected style. A resolved proposal retains old
  reviews/source bindings but clears active marker/pitch approval and requires
  source-quality reassessment. Its generation is not advanced by the planner.
- All 18 production-draft parity tests passed, including actual CLI invocation,
  deterministic proposal content, ambiguous styles, unchanged workspace bytes,
  preservation of historical evidence and rejection of in-workspace/overwrite
  destinations. The proposed state is validated against the target schema.
- **Not yet applied:** the C++ durable migration operation, retained migration
  receipt and history-transition verification remain to implement. Generic save
  continues to reject a schema upgrade; this plan cannot bypass that boundary.

Next concrete implementation owners: explicit evidence-backed legacy migration,
then complete populated-workspace parity and candidate
review/publication parity and the resumable inventory campaign. The generation
test uses synthetic diagnostic material, not a qualified singer. No M1 completion
is claimed.
