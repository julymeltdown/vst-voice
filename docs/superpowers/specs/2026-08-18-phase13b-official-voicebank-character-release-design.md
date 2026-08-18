# Phase 13B Official Voicebank and Character Release Engineering Design

## Status

Approved by the user through the instruction to proceed with the next phase after Phase 13A.

## Goal

Build the fail-closed release-engineering layer for Official Voicebank 01 and Character 01 without fabricating a performer contract, completed recording sessions, trademark clearance, production character ownership, or public release approval.

## Scope

Phase 13B implements:

1. a versioned Official Voicebank release dossier;
2. a versioned Character release dossier;
3. deterministic inventory and asset audits;
4. evidence-file SHA-256 verification;
5. independent voicebank, character, and combined product release gates;
6. deterministic blocked release-candidate archives for review;
7. explicit mandatory future-validation documents and machine-readable matrices;
8. repository CI/CTest integration.

Phase 13B does not claim completion of:

- a signed performer contract;
- directed commercial recording and retake closure;
- full target-language phoneme coverage;
- listening approval by named reviewers;
- final public character name;
- trademark, domain, or social-account clearance;
- final production 3D topology, LODs, expressions, animations, or key art;
- character artist assignment or IP-transfer evidence;
- Official Voicebank 01 release approval.

## Architecture

The implementation is a Python release-engineering subsystem under `tools/phase13b`. It reads data-only JSON dossiers and existing Project SEAM voicebank/character assets. The tools never mutate source audio or character art. They generate deterministic audit reports and candidate ZIP archives. All PASS outcomes require complete evidence objects whose referenced files exist and match recorded SHA-256 values.

The voicebank and character gates remain independent. A combined G5 product release gate passes only when both component gates pass and the Phase 12C/13A mandatory external validation matrix contains no unresolved mandatory target.

## Status model

Implementation and runtime/release validation are separate:

```text
Implementation state:
NOT_STARTED | SOURCE_READY | CI_CONFIGURED | TARGET_BUILD_PASS

Validation result:
NOT_RUN | BLOCKED | FAIL | PASS
```

No implementation state is equivalent to PASS.

## Evidence contract

Every PASS assertion must include:

- evidence type;
- evidence file path relative to repository or dossier root;
- SHA-256 of the evidence file;
- executed or approved timestamp;
- reviewer/approver identity;
- result `PASS`;
- optional notes.

Missing files, absolute paths, traversal paths, symlinks, empty files, or hash mismatch fail the gate.

## Official Voicebank gate

Mandatory categories:

- performer contract and rights review;
- recording session logs and hardware calibration;
- complete recording-script and unit inventory coverage;
- retake closure;
- acoustic marker and pitch-mark QA;
- Raw/PSOLA/Spectral/Stretch listening QA;
- deterministic signed `.seambank` and install receipt;
- performer/character separation statement;
- product-owner approval;
- legal approval.

A dossier with `contractedSinger=false` or `official=false` can never pass.

## Character gate

Mandatory categories:

- final public name;
- trademark/domain/social clearance;
- first-party or assigned ownership evidence;
- final front/side/back turnaround;
- production topology and UV source;
- runtime LODs;
- facial expressions and state portraits;
- animation set;
- final key art;
- merchandise/usage rules;
- performer/character separation statement;
- product-owner approval;
- legal approval.

Current concept and blockout assets may be packaged only as a blocked review candidate.

## Deterministic candidate archives

The candidate builder writes ZIP entries in canonical lexical order with the fixed timestamp `1980-01-01 00:00:00`. It includes component dossiers, audit reports, referenced evidence, notices, and a generated candidate manifest. `releaseEligible` is derived from the gate and cannot be provided by the caller.

## Mandatory future validation

Actual external work is mandatory and remains a release blocker:

- performer selection and signed contract;
- directed recording, retakes, and acceptance;
- full target-language voicebank inventory;
- named listening reviewers;
- artist/IP assignment;
- final character naming and clearance;
- actual trademark/domain/social searches by the responsible release team;
- actual signed `.seambank` install validation;
- Phase 12C/13A target OS, validator, installer, and commercial DAW evidence.

## Acceptance

Phase 13B engineering implementation is complete when:

- all unit tests and source-contract tests pass;
- current public-domain/demo voicebank is correctly blocked from official release;
- current Character 01 concept package is correctly blocked from commercial release;
- a fully synthetic test dossier with complete hashed evidence can pass each gate;
- tampered or missing evidence fails;
- candidate archives are deterministic;
- combined product gate cannot bypass unresolved Phase 12C/13A mandatory validation;
- the release-readiness report lists all unresolved external blockers.

Phase 13B product release acceptance remains BLOCKED until real external evidence is supplied.
