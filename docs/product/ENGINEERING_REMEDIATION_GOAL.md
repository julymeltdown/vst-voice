# Project SEAM Engineering Remediation Goal

**Status:** ACTIVE

**Baseline:** `master` through the current
`codex/external-beta-completion` working tree

**Review source:** full-depth review run
`20260825-214722-d503dbd4`

## Goal statement

Produce a tracked, reproducible, fail-closed Project SEAM engineering
candidate by resolving all 18 independently validated P0/P1 review findings,
proving the result from a clean checkout, and keeping External Beta promotion
blocked until real rights, signing, target-machine, host, and cohort evidence
exists.

The outcome is an engineering candidate that can safely enter external release
execution. It is not an `EXTERNAL_BETA_READY`, Usable Alpha, Release Candidate,
or General Availability claim.

## Candidate, evidence, and attestation boundary

The remediation closes through three immutable objects with different roles:

1. Candidate commit `C` contains product source, tests, schemas, the blank
   evidence template, and no generated proof that claims to verify `C`.
2. Evidence bundle `E` is generated outside the candidate source tree after a
   clean checkout of `C`. It binds every gate, raw-log digest, manual journey,
   and review lane to the full SHA of `C` and to one immutable review-base SHA.
3. Attestation commit `A` is optional and documentation-only. It identifies
   `C`, the external locator of `E`, and the SHA-256 of `E`; it never replaces
   `C` as the product source identity.

The canonical record shape is
`docs/product/engineering-remediation-evidence.schema.json`. The checked-in
`engineering-remediation-evidence-template.json` is intentionally incomplete
and cannot pass `tools/engineering_remediation_evidence.py`. A sealed record
must live outside `C`, use portable paths relative to its evidence root, and
pass validation with the containing commit supplied. Validation rejects a
record whose candidate SHA equals its containing commit, preventing
self-certification.

The durable evidence locator published by `A` has the form
`engineering-remediation://<candidate-sha>/<evidence-sha256>` and resolves to
an immutable CI or release artifact. Raw log and review locators inside `E`
are relative paths whose bytes must match their recorded SHA-256 values.

## Why this goal exists

The current dirty workspace builds and passes its local test suite, but the
review found three classes of release-blocking problems:

1. release and host gates can accept self-reported or non-PASS evidence;
2. the tracked patch does not contain every source file required by its build;
3. project recovery, export replacement, voicebank identity, release audio,
   and package identity contain validated correctness or data-integrity defects.

Passing tests in the existing dirty workspace cannot compensate for those
failures.

## In scope

### G1 - Release truth and reproducibility

- Require each External Beta requirement to reference matching `PASS` evidence
  for the same requirement, candidate, artifact stage, platform, and surface.
- Track every source, header, schema, script, fixture, and workflow input needed
  by the configured build and tests.
- Derive application, CLAP, wrapper, installer, archive, and evidence versions
  from one generated release identity.
- Make host certification hash and verify the actual installed no-link artifact
  instead of accepting caller-supplied paths and hashes.

Review findings: `#1`, `#2`, `#22`, `#24`, `#26`.

### G2 - Durable project and export transactions

- Classify old export-owned files from the previous validated receipt so
  removed stems do not reappear.
- Never delete a predictable backup or staging sibling without validating its
  ownership.
- Journal export publication and reconcile every interrupted phase before
  exposing a committed destination.
- Bind autosave lineage to the exact durable project bytes.
- Open recovered autosaves as new dirty copies that require Save As and cannot
  overwrite the last durable original through ordinary Save.
- Make trackless project creation and reopen behavior consistent.

Review findings: `#6`, `#7`, `#8`, `#9`, `#10`, `#11`.

### G3 - Audible runtime truth and bounded preview memory

- Permit valid backing-only projects to render and play without requiring a
  vocal selection.
- Keep stale-audio truth visible while an older publication remains audible.
- Replace full-render preview copies with shared immutable PCM or chunk
  ownership and measure the declared five-minute workload.
- Prohibit callback-clock fallback in Release mode and expose
  `AUDIO_UNAVAILABLE` when physical audio cannot open or start.
- Preserve the real requested-device error through transactional rollback and
  never call `Result::error()` on a successful result.

Review findings: `#5`, `#12`, `#14`, `#19`, `#20`.

### G4 - Voicebank identity and recording containment

- Reject different synthesis content under an already installed voicebank ID
  and version; only exact same-hash reinstall is idempotent.
- Convert manifest-controlled take names to portable basenames and prove the
  destination is a direct child of the recording directory on Windows and
  POSIX paths.

Review findings: `#13`, `#25`.

## Non-goals

This goal does not fabricate, substitute, or locally mark PASS for:

- performer consent or a rights-cleared Beta Voicebank;
- Developer ID, notarization, Authenticode, or timestamp credentials;
- clean target-machine install, upgrade, downgrade, or uninstall evidence;
- physical CoreAudio/WASAPI listening evidence;
- REAPER, Bitwig Studio, Logic Pro, VST3, AUv2, or CLAP target-host evidence;
- VoiceOver or Narrator observation;
- external musician sessions, cohort operations, or closure approvals.

Those rows remain `NOT_RUN` or `BLOCKED` until the corresponding external work
is executed against the exact candidate.

## Required invariants

- A gate cannot pass from source presence, workflow configuration, synthetic
  metadata, an existing record ID, or a caller-supplied digest alone.
- No recovery or replacement operation may destroy the last durable user copy
  or unrelated neighboring content.
- One voicebank `(id, version)` maps to one synthesis content hash.
- Release mode never reports test fallback audio as physical or audible output.
- Realtime callbacks allocate no memory, acquire no locks, and perform no file
  I/O or logging.
- The exact tracked source tree is sufficient to reproduce every local claim.

## Exit criteria

The goal is complete only when all criteria below have current evidence:

- [ ] **E1 - Tracked reproducibility:** a clean checkout or fresh isolated
      worktree configures, builds, and runs the complete registered suite with
      no dependency on untracked files.
- [ ] **E2 - Release-gate fidelity:** tests reject `NOT_RUN`, `BLOCKED`, `FAIL`,
      cross-requirement, cross-platform, cross-stage, and cross-candidate
      evidence reuse.
- [ ] **E3 - Single release identity:** built application and plug-in
      descriptors, wrappers, installers, manifests, and evidence all report the
      same generated version, build ID, and source commit.
- [ ] **E4 - Installed-byte certification:** host PASS fails for nonexistent,
      symlinked, build-tree, mismatched, or self-reported artifacts and passes
      only for the matching installed tree.
- [ ] **E5 - Project durability:** exact-byte lineage, noncanonical JSON,
      recovery Save As, external-change detection, empty project reopen, and
      original-byte preservation tests pass.
- [ ] **E6 - Export durability:** removed-stem, unrelated-canary, unowned
      `.previous`, and process-interruption tests pass for every journal phase.
- [ ] **E7 - Runtime audio truth:** backing-only playback, stale publication,
      Release device failure, device rollback, and no-test-fallback tests pass.
- [ ] **E8 - Bounded preview memory:** the five-minute 48 kHz stereo workload
      stays within the declared memory and copy-byte limits without full-result
      duplication.
- [ ] **E9 - Voicebank safety:** same-version hash collision and Windows/POSIX
      recording-name traversal, reserved-name, and containment tests pass.
- [ ] **E10 - Regression gates:** release CTest, ASan/UBSan, TSan, realtime
      allocation probes, contract verifiers, and `git diff --check` pass.
- [ ] **E11 - Review closure:** a fresh full-depth review finds zero current P0
      or P1 code defects in the remediation diff.
- [ ] **E12 - Truthful status:** acceptance JSON and status documents still
      report external requirements as `NOT_RUN` or `BLOCKED` unless exact
      hash-bound target evidence has actually been added.

## Required deliverables

- tracked source and tests for all remediations;
- clean-checkout build and test evidence;
- adversarial release-gate fixtures;
- export and project fault-injection fixtures;
- built-artifact release-identity inspection;
- installed-artifact host-certification verification;
- updated status and remaining-work documents;
- final full-depth review report tied to the remediated commit SHA.

## Execution order

1. Close `#1` and `#2` first so later green checks are trustworthy and
   reproducible.
2. Repair destructive boundaries: `#7`, `#8`, `#10`, and `#13`.
3. Repair Release audio truth and rollback: `#19` and `#20`.
4. Unify release identity and installed-byte evidence: `#22`, `#24`, and `#26`.
5. Complete remaining project, export, render-state, memory, and recording-path
   findings.
6. Run the full verification matrix from a clean tracked tree.
7. Re-run the full-depth review and update status documents without promoting
   any external gate.

## Progress rule

Engineering remediation progress is measured as completed exit criteria out of
12. External Beta readiness remains a separate binary gate and stays blocked
until every mandatory external row has valid evidence.

## Current working-tree checkpoint

**Engineering remediation progress:** **9 / 12 exit criteria (75.0%)**.

Candidate `da88b1c3030d7b521e49c3a70f4fbdca102a1709` has a clean isolated
checkout proof: source closure passed; Debug and Release each configured, built,
and passed 62 registered CTests; the External Beta, Phase13A, and root Python
suites passed 102, 92, and 68 tests respectively. E1 is therefore satisfied.
E3, E10, and E11 remain open until exact-SHA release-identity inspection,
sanitizer/regression evidence, and the fresh full-depth review are sealed.

This checkpoint is not an attestation. It records verified local behavior only;
it does not change Usable Alpha or External Beta acceptance. External Beta
remains `BLOCKED`, and all unavailable rights, signing, target-machine,
physical-audio, host, accessibility, and cohort evidence remains `NOT_RUN` or
`BLOCKED`.
