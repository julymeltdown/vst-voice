# Phase 13A Distribution and Certification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish reproducible VST3/AUv2 wrapper builds, fail-closed signing and installer pipelines, and evidence-backed target-OS/DAW certification gates without representing unexecuted external validation as PASS.

**Architecture:** CLAP remains the canonical implementation. Pinned `clap-wrapper` projects the same CLAP binary into VST3 and AUv2 on their supported target systems. A release-manifest and mandatory-validation registry separate implementation state from actual validator, signing, installer, OS, and DAW results.

**Tech Stack:** C++20, CMake 3.25+, Python 3, GitHub Actions, clap-wrapper 0.15.1, Steinberg VST3 SDK 3.8.1, Apple AudioUnitSDK 1.4.0, NSIS 3.12 with zlib compression, pkgbuild/productbuild, codesign/notarytool, Steinberg validator, auval.

**Spec:** `docs/REMAINING_TASKS_KO.md`, `docs/RELEASE_READINESS_KO.md`, and `docs/phase12c/MANDATORY_TARGET_OS_AND_DAW_VALIDATION_KO.md`.

## Global Constraints

- Work only on `master`; never create another local branch.
- CLAP remains canonical; VST3/AUv2 must not create a second synthesis implementation.
- Runtime dependencies must remain commercially permissive.
- A source file, build job, or unsigned artifact is never a runtime/validator/signing/install PASS.
- Missing credentials or target tools fail closed and remain `NOT_RUN` or `BLOCKED`.
- Windows/macOS and commercial DAW tests are mandatory before Beta/RC/GA according to the validation registry.

---

### Task 1: Mandatory release validation registry

- [x] Add Phase 13A matrix, result schema, and gate validator.
- [x] Link G2/G3/G4/G5 gates to Phase 12C and Phase 13A evidence.
- [x] Add tests proving a source-ready row cannot satisfy a runtime PASS requirement.

### Task 2: Pinned wrapper dependency contract

- [x] Pin clap-wrapper 0.15.1, VST3 SDK 3.8.1, and AudioUnitSDK 1.4.0 by full commit.
- [x] Add acquisition/build orchestration that verifies source revision and licence file.
- [x] Keep external SDK trees out of the repository and generated package.

### Task 3: VST3/AUv2 wrapper and validator jobs

- [x] Add target-OS CI builds for VST3 and AUv2 using the canonical CLAP target.
- [x] Add Steinberg validator and `auval` runners that preserve raw logs and machine-readable results.
- [x] Mark local Linux AU execution as `NOT_RUN` rather than fabricating a bundle.

### Task 4: Installer and signing pipelines

- [x] Add deterministic Linux developer package and clean install/uninstall smoke.
- [x] Add NSIS 3.12/zlib installer source, Authenticode signing gate, and Windows clean-install/update/uninstall job.
- [x] Add macOS CLAP/VST3/AU package layout, nested signing, notarization, stapling, PKG creation, and clean-install job.

### Task 5: Commercial host certification recorder

- [x] Add evidence schema and recorder for REAPER, Bitwig, Cubase, Ableton Live, Studio One, FL Studio, Logic Pro, and GarageBand.
- [x] Reject PASS records missing exact OS/host/plugin hashes and evidence paths.
- [x] Keep checked-in baseline rows `NOT_RUN` until actual host execution.

### Task 6: Documentation, tests, and packaging

- [x] Update status, remaining tasks, release gates, README, notices, and SBOM contract.
- [x] Run local contract tests and Linux package smoke.
- [x] Package `.git` and record unresolved external gates explicitly.
