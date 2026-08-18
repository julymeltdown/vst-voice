# Phase 13B Content and IP Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a fail-closed Official Voicebank 01 and Character 01 release-readiness pipeline while producing a deterministic development content pack from the already-cleared demo voice and selected character assets.

**Architecture:** Python tools under `tools/phase13b` validate evidence-backed JSON dossiers, derive deterministic character assets, build deterministic ZIP packages, and evaluate the G5 release gate. CMake exposes the contract as CTest without adding runtime dependencies to the audio engine.

**Tech Stack:** Python 3 standard library, Pillow for the asset-generation script, CMake/CTest, existing Project SEAM JSON and package conventions.

**Spec:** `docs/superpowers/specs/2026-08-18-phase13b-content-ip-release-design.md`

## Global Constraints

- Work only on `master`; create no additional branch.
- Never fabricate contracts, recordings, trademark clearance, legal approval, signing, notarization, DAW certification, or performer identity.
- The public-domain/CC0 fixture remains `official=false` and `contractedSinger=false`.
- PASS requires existing evidence with matching SHA-256.
- Character assets never enter synthesis, routing, render-cache identity, or exported PCM.
- All package output must be deterministic and path-traversal/symlink safe.

---

### Task 1: Evidence and dossier validation

**Files:**
- Create: `tools/phase13b/evidence.py`
- Create: `tools/phase13b/voicebank_release.py`
- Create: `tools/phase13b/character_release.py`
- Test: `tests/phase13b/test_evidence.py`
- Test: `tests/phase13b/test_voicebank_release.py`
- Test: `tests/phase13b/test_character_release.py`

**Interfaces:**
- Produces: `validate_evidence_record(record, root) -> list[str]`
- Produces: `evaluate_voicebank_dossier(dossier, root) -> dict`
- Produces: `evaluate_character_dossier(dossier, root) -> dict`

- [ ] Write failing evidence-path/hash tests.
- [ ] Run the tests and verify expected failures.
- [ ] Implement evidence validation.
- [ ] Write failing official voicebank gate tests.
- [ ] Implement voicebank dossier evaluation.
- [ ] Write failing Character 01 gate tests.
- [ ] Implement character dossier evaluation.
- [ ] Run all Phase 13B tests.
- [ ] Commit.

### Task 2: Deterministic Character 01 development assets

**Files:**
- Create: `tools/phase13b/character_assets.py`
- Create: `scripts/generate_phase13b_character_assets.py`
- Test: `tests/phase13b/test_character_assets.py`
- Generate: `assets/character-01/production-development/*`

**Interfaces:**
- Produces: `generate_character_assets(source, output) -> dict`

- [ ] Write failing deterministic output and source-hash tests.
- [ ] Verify RED.
- [ ] Implement image derivation, palette extraction, silhouette, and manifest hashing.
- [ ] Verify GREEN and regenerate checked-in assets.
- [ ] Commit.

### Task 3: Deterministic content packaging and G5 gate

**Files:**
- Create: `tools/phase13b/content_bundle.py`
- Create: `tools/phase13b/release_gate.py`
- Test: `tests/phase13b/test_content_bundle.py`
- Test: `tests/phase13b/test_release_gate.py`

**Interfaces:**
- Produces: `create_development_bundle(...) -> dict`
- Produces: `evaluate_g5(...) -> dict`

- [ ] Write failing deterministic bundle/path-safety tests.
- [ ] Implement deterministic ZIP packaging.
- [ ] Write failing G5 blocked/pass evidence tests.
- [ ] Implement G5 evaluator and CLI.
- [ ] Verify all tests and commit.

### Task 4: Baseline dossiers, templates, and contracts

**Files:**
- Create: `content/phase13b/official-voicebank-01-release-dossier.json`
- Create: `content/phase13b/character-01-release-dossier.json`
- Create: `docs/phase13b/ACCEPTANCE.md`
- Create: `docs/phase13b/MANDATORY_EXTERNAL_WORK_KO.md`
- Create: `docs/phase13b/IMPLEMENTATION_REPORT.md`
- Create: `docs/legal/templates/*`
- Create: `docs/voicebank/templates/*`
- Modify: `docs/STATUS_KO.md`
- Modify: `docs/REMAINING_TASKS_KO.md`
- Modify: `docs/RELEASE_READINESS_KO.md`
- Modify: `README.md`

- [ ] Add baseline BLOCKED dossiers without fake evidence.
- [ ] Add contract, recording, QA, clearance, and approval templates.
- [ ] Update project status and release gates.
- [ ] Run dossier evaluators and confirm BLOCKED.
- [ ] Commit.

### Task 5: Build/test integration and evidence

**Files:**
- Create: `phase13b/CMakeLists.txt`
- Create: `scripts/verify_phase13b_contracts.py`
- Create: `scripts/generate_phase13b_evidence.py`
- Modify: `CMakeLists.txt`
- Generate: `docs/phase13b/evidence/*`

- [ ] Add CTest contract targets.
- [ ] Run full Python Phase 13B tests.
- [ ] Run contract verifier.
- [ ] Generate deterministic development content bundle.
- [ ] Run clean configure/build/CTest.
- [ ] Commit evidence.
- [ ] Package `.git` repository and verify from clean extraction.
