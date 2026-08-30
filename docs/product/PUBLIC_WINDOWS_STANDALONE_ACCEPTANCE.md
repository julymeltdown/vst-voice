# Project SEAM Public Windows Standalone Acceptance Contract

**Canonical machine mirror:**
[`public-windows-standalone-acceptance.json`](public-windows-standalone-acceptance.json)

**Evidence schema:**
[`public-windows-standalone-evidence.schema.json`](public-windows-standalone-evidence.schema.json)

**Current result:** `BLOCKED`

This contract is the Windows x64 public acquisition-to-export journey. It is a
separate target contract from Apple Silicon `UA-001` through `UA-020`. A UA
record, macOS artifact, source test, CI result, or internal validator cannot
satisfy a `PW-###` row, and a PW record cannot satisfy a UA row.

## Mandatory requirements

- [ ] **PW-001 — Acquire:** Download the exact public Windows installer from the signed direct-download channel.
- [ ] **PW-002 — Trust:** Verify Authenticode identity, trusted timestamp, installer SHA-256, and candidate lineage before execution.
- [ ] **PW-003 — Clean Install:** Install on a clean supported Windows x64 snapshot without a terminal or developer dependency.
- [ ] **PW-004 — Launch:** Launch the installed standalone from the Start menu and reach a responsive editor.
- [ ] **PW-005 — Public Documents:** Accept the exact public EULA and privacy IDs, versions, and digests; display support and security destinations.
- [ ] **PW-006 — Voicebank:** Install or resolve the exact rights-cleared signed bank without an engineering fixture or Character dependency.
- [ ] **PW-007 — New Project:** Create a named project with tempo, sample rate, channels, and exact bank selection.
- [ ] **PW-008 — Authoring:** Add vocal structure and enter at least 30 seconds of notes and Japanese lyrics.
- [ ] **PW-009 — Detailed Edit:** Inspect generated phonemes and units, edit phoneme, unit, pitch, and seam data, and preserve overrides.
- [ ] **PW-010 — Production Audio:** Hear sample-concatenative audio for the visible project, not fallback or demo audio.
- [ ] **PW-011 — WASAPI Transport:** Play, pause, stop, seek, loop, and recover from declared physical-device changes without stale audio or automatic resume.
- [ ] **PW-012 — Save:** Save the project atomically to a user-selected path.
- [ ] **PW-013 — Reopen:** Quit, reopen the saved project, and recover the exact bank and materially identical audio.
- [ ] **PW-014 — Crash Recovery:** Recover a dirty project after forced termination without overwriting the last durable user copy.
- [ ] **PW-015 — Bank Recovery:** Detect a missing or changed bank and relink the exact ID, version, and content hash without silent substitution.
- [ ] **PW-016 — Master Export:** Export a final-quality master WAV to a user-selected destination.
- [ ] **PW-017 — Stem Export:** Export at least one vocal stem WAV without publishing a partial destination.
- [ ] **PW-018 — External Verification:** Open exports in an external Windows player and verify duration, channels, audible content, and hashes.
- [ ] **PW-019 — Lifecycle:** Complete repair, update interruption, rollback, uninstall, and reinstall while preserving user projects and removing stale binaries.
- [ ] **PW-020 — Stability and Access:** Complete the declared 30-minute authoring session with physical audio and keyboard/Narrator checks, zero data loss, zero underruns, and no unbounded memory growth.

## Evidence policy

Each PASS row carries a `PW-###` evidence record with `platform: windows`,
`architecture: x86_64`, the exact candidate lineage, artifact root, installed
tree, operator and machine identities, trusted time, a safe repository-relative
raw-evidence path, and the lowercase SHA-256 of those bytes. The gate may be
`PASSED` only when all twenty rows pass and every row matches the contract's
single top-level `candidateLineageId`, `artifactRootSha256`, and
`installedTreeSha256`.

Run:

```bash
python3 scripts/verify_public_windows_standalone_contract.py --root .
```

The checked-in mirror contains no target evidence. All rows remain `NOT_RUN`
and the gate remains `BLOCKED` until real Windows target execution occurs.
