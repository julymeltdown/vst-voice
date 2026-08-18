# Project SEAM Usable Alpha Acceptance Contract

**Canonical gate:** This document is the authoritative English product gate for the first locally usable Project SEAM standalone application.

**Current result:** `BLOCKED`

Project SEAM reaches **Usable Alpha** only when one person can complete every mandatory requirement below on an Apple Silicon Mac without a command-line interface and without a DAW. A phase implementation, CI workflow, source-ready platform adapter, release gate, or passing module test does not substitute for this user journey.

## Current limitation

The current native standalone is a demo shell. `apps/seam-editor-native/main.cpp` constructs a hard-coded project through `makeDemoProject()` and a sine-wave playback source through `makeDemoTimeline()`. The visible notes therefore do not yet drive the production sample-concatenative renderer in the standalone application.

Phase 12C, Phase 13A, and Phase 13B provide engineering, validation, distribution, and content/IP gates. They do **not** prove standalone usability and cannot mark this contract as passed.

## Mandatory requirements

- [ ] **UA-001 — Launch:** Launch `Project SEAM.app` from Finder and reach a responsive editor.
- [ ] **UA-002 — New Project:** Create a project with a name, tempo, sample rate, output-channel count, and exact voicebank selection.
- [ ] **UA-003 — Authoring Structure:** Add at least one vocal track and one vocal region.
- [ ] **UA-004 — Musical Input:** Enter at least 30 seconds of notes and Japanese lyrics.
- [ ] **UA-005 — Generated Detail:** See generated phonemes and selected source units for the visible project.
- [ ] **UA-006 — Phoneme Edit:** Move at least one phoneme boundary and preserve the override.
- [ ] **UA-007 — Unit Edit:** Select a different unit variant or renderer and hear the resulting change.
- [ ] **UA-008 — Pitch Edit:** Add, move, and delete a pitch point.
- [ ] **UA-009 — Seam Edit:** Change at least one seam parameter on a selected boundary.
- [ ] **UA-010 — Production Audio:** Hear production sample-concatenative audio corresponding to the visible project, not a demo oscillator.
- [ ] **UA-011 — Transport:** Play, pause, stop, seek, and loop without stale audio from an earlier edit.
- [ ] **UA-012 — Save:** Save the project to a user-selected path.
- [ ] **UA-013 — Reopen:** Quit the application and reopen the saved project.
- [ ] **UA-014 — Audio Parity:** Hear materially identical audio after reopening the same project and exact voicebank.
- [ ] **UA-015 — Recovery:** Recover a dirty project after a forced termination using autosave.
- [ ] **UA-016 — Voicebank Relink:** Detect a missing voicebank and relink the exact ID, version, and content hash without silent substitution.
- [ ] **UA-017 — Master Export:** Export a final-quality master WAV.
- [ ] **UA-018 — Stem Export:** Export at least one vocal stem WAV.
- [ ] **UA-019 — External Verification:** Open the exported WAV in an external player and verify duration, channel count, and audible content.
- [ ] **UA-020 — Stability Session:** Work for 30 minutes with zero audio underruns, zero data-loss defects, and no unbounded memory growth.

## Quantitative targets

- Cold launch to responsive editor: less than 3 seconds on the target M3 Max with the demo voicebank already indexed.
- Two-second phrase edit to audible Preview: median below 150 ms and p95 below 400 ms at 48 kHz.
- Piano-roll interaction: 60 FPS target with 10,000 notes; no ordinary selection, pan, or zoom frame above 50 ms.
- Audio callback: zero dynamic allocations and zero locks in instrumented builds.
- Playback: zero underflow frames during the 30-minute acceptance session at 48 kHz and 128 frames.
- Project save: below 1 second for a five-minute, four-track project, excluding backing-media copy time.
- Autosave UI stall: below 50 ms; serialization and durable writes execute away from the UI thread.
- Export: continuous progress, cancellation, and atomic destination publication; no partial requested destination.
- Memory: no monotonic growth above 100 MiB over the warmed baseline during the acceptance session.

## Evidence policy

The machine-readable mirror is [`usable-alpha-acceptance.json`](usable-alpha-acceptance.json). Every mandatory item has a stable `UA-###` ID. A row may be marked `PASS` only when it contains at least one safe repository-relative evidence path and the exact lowercase SHA-256 of that file. The gate may be `PASSED` only when all twenty mandatory requirements are `PASS`.

Run:

```bash
python3 scripts/verify_usable_alpha_contract.py --root .
```
