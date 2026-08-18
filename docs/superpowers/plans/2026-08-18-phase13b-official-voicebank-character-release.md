# Phase 13B Official Voicebank and Character Release Engineering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement fail-closed Official Voicebank 01 and Character 01 release dossiers, audits, evidence verification, candidate packaging, and mandatory external release gates.

**Architecture:** Add a Python release-engineering subsystem under `tools/phase13b`, integrated through `phase13b/CMakeLists.txt`. Dossiers and evidence remain data-only. Audits inspect existing Project SEAM assets and manifests; deterministic candidate ZIPs are generated only after evaluating independent component and combined gates.

**Tech Stack:** Python 3 standard library, unittest, CMake/CTest, JSON, SHA-256, ZIP.

**Spec:** `docs/superpowers/specs/2026-08-18-phase13b-official-voicebank-character-release-design.md`

## Global Constraints

- Work only on `master`; create no branch.
- Do not claim a performer contract, recording completion, trademark clearance, signing, or public-release approval without actual hashed evidence.
- `SOURCE_READY`, `CI_CONFIGURED`, and `TARGET_BUILD_PASS` are never equivalent to `PASS`.
- Current public-domain voice fixtures are not Official Voicebank 01.
- Current Character 01 concept/blockout assets are not final commercial character assets.
- Absolute paths, traversal paths, symlinks, empty evidence, and SHA-256 mismatch must fail closed.
- Candidate ZIP output must be deterministic.

---

### Task 1: Dossier models and evidence verifier

**Files:**
- Create: `tools/phase13b/common.py`
- Create: `tools/phase13b/evidence.py`
- Test: `tests/phase13b/test_evidence.py`

**Interfaces:**
- Produces: `sha256_file(path) -> str`, `verify_evidence(root, item) -> list[str]`, `load_json(path) -> dict`.

- [ ] Write tests that reject missing, traversal, symlink, empty, and hash-mismatched evidence and accept a complete hashed evidence file.
- [ ] Run the tests and verify RED because the module is missing.
- [ ] Implement bounded JSON loading and evidence validation.
- [ ] Run the tests and verify GREEN.
- [ ] Commit.

### Task 2: Voicebank inventory audit and official release gate

**Files:**
- Create: `tools/phase13b/voicebank_audit.py`
- Create: `tools/phase13b/voicebank_gate.py`
- Test: `tests/phase13b/test_voicebank_gate.py`

**Interfaces:**
- Produces: `audit_voicebank(bank_root, profile) -> dict`, `evaluate_voicebank_dossier(dossier, root) -> GateResult`.

- [ ] Write failing tests for demo-bank blocking, required-category enforcement, inventory deficits, valid synthetic PASS, and evidence tampering.
- [ ] Run and verify RED.
- [ ] Implement inventory counting and fail-closed official dossier evaluation.
- [ ] Run and verify GREEN.
- [ ] Commit.

### Task 3: Character asset audit and commercial release gate

**Files:**
- Create: `tools/phase13b/character_audit.py`
- Create: `tools/phase13b/character_gate.py`
- Test: `tests/phase13b/test_character_gate.py`

**Interfaces:**
- Produces: `audit_character(character_root, profile) -> dict`, `evaluate_character_dossier(dossier, root) -> GateResult`.

- [ ] Write failing tests for current Character 01 blocking, required asset categories, ownership/trademark evidence, synthetic PASS, and tampering.
- [ ] Run and verify RED.
- [ ] Implement asset/hash inventory and fail-closed character evaluation.
- [ ] Run and verify GREEN.
- [ ] Commit.

### Task 4: Combined product gate and deterministic candidate builder

**Files:**
- Create: `tools/phase13b/product_gate.py`
- Create: `tools/phase13b/candidate_builder.py`
- Test: `tests/phase13b/test_product_gate.py`
- Test: `tests/phase13b/test_candidate_builder.py`

**Interfaces:**
- Produces: `evaluate_product(...) -> dict`, `build_candidate(...) -> dict`.

- [ ] Write failing tests showing component PASS is insufficient when Phase 12C/13A mandatory rows are unresolved and showing byte-identical ZIP output.
- [ ] Run and verify RED.
- [ ] Implement combined gating and canonical ZIP output.
- [ ] Run and verify GREEN.
- [ ] Commit.

### Task 5: CLI, current blocked dossiers, documentation, and CTest integration

**Files:**
- Create: `tools/phase13b/release_cli.py`
- Create: `phase13b/CMakeLists.txt`
- Create: `docs/phase13b/ACCEPTANCE.md`
- Create: `docs/phase13b/MANDATORY_OFFICIAL_VOICEBANK_AND_CHARACTER_VALIDATION_KO.md`
- Create: `docs/phase13b/MANDATORY_OFFICIAL_VOICEBANK_AND_CHARACTER_VALIDATION.md`
- Create: `docs/phase13b/official-voicebank-01-dossier.json`
- Create: `docs/phase13b/character-01-dossier.json`
- Create: `docs/phase13b/mandatory-validation-matrix.json`
- Create: `scripts/verify_phase13b_contracts.py`
- Modify: `CMakeLists.txt`
- Modify: `docs/remaining-tasks.json`

**Interfaces:**
- CLI commands: `audit-voicebank`, `audit-character`, `check`, `build-candidate`.

- [ ] Write source-contract tests asserting all mandatory docs, schemas, CMake tests, and fail-closed statuses exist.
- [ ] Run and verify RED.
- [ ] Implement CLI, dossiers, docs, CMake/CTest integration, and remaining-task status updates.
- [ ] Run Phase 13B tests and full CTest.
- [ ] Generate current blocked reports and deterministic review candidates.
- [ ] Commit.

### Task 6: Final verification and packaging

**Files:**
- Create: `docs/phase13b/IMPLEMENTATION_REPORT_KO.md`
- Create: `docs/phase13b/IMPLEMENTATION_REPORT.md`
- Create: `docs/phase13b/EVIDENCE.md`

- [ ] Run warnings-as-errors build, Phase 13B tests, full CTest, master-only policy, license audit, and `git fsck --full`.
- [ ] Verify current voicebank and character candidates remain BLOCKED for the documented external reasons.
- [ ] Package the clean master repository with `.git` and perform a fresh-extraction verification.
- [ ] Commit reports and evidence.
