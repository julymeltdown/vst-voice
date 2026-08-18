# Project SEAM Phase 12C — Live Voicebank Articulation and Runtime Validation Design

**Date:** 2026-08-18  
**Status:** Approved design  
**Branch policy:** `master` only  
**Current baseline:** Phase 12B, Project schema 5, production multi-track/multi-region renderer, 1–8-channel CLAP output

## 1. Purpose

Phase 12C closes the remaining quality and runtime gaps before Project SEAM can be considered feature-complete. It has two responsibilities:

1. Replace the technical single-vowel live sampler with a Voicebank-driven live articulation engine.
2. Produce real Linux validation evidence while making Windows, macOS, and commercial DAW validation explicit mandatory release gates that remain `NOT_RUN` until executed on the actual target platform and host.

Phase 12C does **not** claim that Windows, macOS, VST3, AU, signing, notarization, installers, commercial DAWs, or Official Voicebank 01 are complete.

## 2. Scope

### 2.1 In scope

- Voicebank-driven attack, transition, sustain, release, and legato live note synthesis.
- CLAP note events, CLAP note expression, MIDI 1 dialect, pitch bend, velocity, pressure, brightness, and expression gain.
- Realtime-safe voice allocation, deterministic voice stealing, and de-click envelopes.
- Official `clap-validator` 0.4.1 execution against the release CLAP module.
- Linux sample-rate, buffer-size, channel-count, render-mode, transport, and event matrices.
- A real two-hour Linux playback/edit soak run.
- 1,000 GUI open/close lifecycle cycles.
- Render cancellation storm, state-load/save repetition, voice-stealing stress, and Voicebank mutation/relink tests.
- ASan, UBSan, TSan, realtime allocation probes, leak/handle/thread accounting, NaN/Inf checks, and stale-publication checks.
- Windows and macOS runtime harness source and CI contracts.
- Commercial DAW certification checklists, result schemas, and release-gate integration.
- Separate Korean and English mandatory future validation documents and a machine-readable matrix.

### 2.2 Explicitly out of scope

- VST3 and AU binaries or validator passes.
- Windows Authenticode signing.
- Apple Developer ID signing, notarization, and stapling.
- Clean-OS installer execution.
- Actual commercial DAW certification in hosts not installed in the execution environment.
- Contracted and recorded Official Voicebank 01.
- Full lyrical live synthesis from arbitrary text during note-on events.
- Network services or cloud rendering.

## 3. Architectural decisions

### 3.1 New module boundaries

```text
libs/seam-live-voice/
├── articulation-planner
├── unit-source-cache
├── voice-allocator
├── live-voice-state
├── live-unit-renderer
├── expression-state
├── midi1-decoder
└── de-click-envelope

tools/
├── seam-clap-validation-runner
├── seam-realtime-probe
├── seam-phase12c-matrix-runner
├── seam-phase12c-soak-runner
├── seam-gui-lifecycle-runner
└── seam-host-certification-recorder
```

The CLAP plug-in owns no Voicebank parsing logic in its audio callback. Main-thread or worker code resolves the exact Voicebank and publishes immutable live resources. `seam-live-voice` receives immutable unit audio and metadata through a small interface.

### 3.2 Live resource publication

```text
Voicebank Resolver
→ exact ID + version + content hash
→ articulation inventory validation
→ attack/transition/sustain/release audio decode
→ bounded immutable LiveVoicebankResources
→ three-slot publication
→ audio thread read-only handle
```

Publication rules:

- Missing or untrusted Voicebank produces explicit silence and a diagnostic.
- Active audio never reads files, parses manifests, or allocates.
- A new Voicebank revision becomes active only at a block boundary.
- The old resource remains alive until all readers release it.
- Resource publication is bounded; no unbounded queue is allowed.

## 4. Voicebank articulation model

### 4.1 Unit roles

Existing `UnitKind` values map to live articulation roles as follows:

| Live role | Preferred unit kinds | Fallback order |
|---|---|---|
| Attack | `Cv`, `Vcv`, `Glottal`, `Special` | sustain onset |
| Transition | `Vcv`, `Vc`, `Vv`, `Cc` | short crossfade |
| Sustain | `Sustain`, vowel-bearing `Cv`/`Vcv` | rejected if no stable loop exists |
| Release | `Release`, `Vc`, `Special` | de-click envelope |
| Breath | `Breath` | none |

A unit is eligible only when its style, phones, root pitch, enabled state, marker order, loop bounds, and content hash are valid.

### 4.2 Articulation plan

For every live voice, `ArticulationPlanner` produces a bounded plan:

```text
AttackSegment
Optional TransitionSegment
SustainSegment
Optional ReleaseSegment
```

Each segment stores:

- immutable audio reference;
- source start/end frames;
- stable/loop/release markers;
- root MIDI key;
- pitch ratio limits;
- gain;
- fade length;
- renderer policy;
- diagnostic identity.

The live path does not invoke the offline PSOLA, Spectral, or Stretch renderer. It uses a realtime bounded sample-loop renderer with linear interpolation and envelopes. The offline production renderer remains authoritative for exported and transport-synchronised phrase audio.

### 4.3 Legato

Legato is triggered when a note-on arrives on the same channel or note-expression voice before the previous voice reaches release.

```text
Current sustain
→ eligible V-V / V-C-V transition
→ target sustain
```

If a transition unit is unavailable, the engine performs a bounded equal-power crossfade and records a `transition-fallback` diagnostic counter. It never performs file lookup or unit selection inside the callback.

## 5. Note-event and expression contract

### 5.1 CLAP dialects

The note input port advertises:

```text
CLAP_NOTE_DIALECT_CLAP
CLAP_NOTE_DIALECT_MIDI
```

MIDI MPE and MIDI 2 remain unadvertised in Phase 12C.

### 5.2 CLAP note expression mapping

| CLAP expression | Live parameter | Range and behavior |
|---|---|---|
| `VOLUME` | per-note gain | clamped to `0.0..4.0` linear |
| `PAN` | stereo position | clamped to `-1.0..1.0` |
| `TUNING` | pitch offset | semitone value converted to ratio |
| `VIBRATO` | vibrato depth | `0..1`, bounded cents modulation |
| `EXPRESSION` | articulation intensity | gain and attack firmness |
| `BRIGHTNESS` | lightweight spectral tilt surrogate | bounded high-frequency emphasis in the live path |
| `PRESSURE` | breath/tension blend | bounded gain and noise balance |

Unsupported expression IDs are ignored without failing the process block.

### 5.3 MIDI 1 mapping

| MIDI message | Mapping |
|---|---|
| Note On | live voice allocation |
| Note Off | release |
| Pitch Bend | channel pitch bend, default ±2 semitones |
| Channel Pressure | pressure expression |
| CC 1 | vibrato depth |
| CC 7 | channel gain |
| CC 10 | pan |
| CC 11 | expression gain |
| CC 74 | brightness |
| CC 64 | sustain pedal |
| All Notes Off / All Sound Off | bounded release / immediate choke |

Running status and malformed packet handling are the host's responsibility because CLAP delivers complete `clap_event_midi_t` events. Invalid data bytes are ignored and counted.

## 6. Voice allocation and de-click behavior

- Maximum live voices: 32.
- Allocation order: free voice → released voice with lowest envelope → oldest quietest voice.
- Stealing never reuses a voice sample discontinuously.
- A stolen voice enters a 64-sample or 1.5 ms de-click ramp, whichever is longer at the active sample rate.
- The incoming voice begins with a matching attack ramp.
- All envelopes are precomputed or calculated with fixed-cost arithmetic.
- No voice owns heap memory.
- The callback performs no locks and no reference-count destruction.

## 7. Linux validation design

### 7.1 Full process matrix

The core matrix is the Cartesian product:

```text
Sample rate  44.1, 48, 88.2, 96, 176.4, 192 kHz
Buffer       16, 32, 64, 128, 256, 512, 1024 frames
Channels     1, 2, 4, 8
Mode         realtime, offline
```

Total base cases: `6 × 7 × 4 × 2 = 336`.

Every base case verifies:

- activation and process lifecycle;
- finite output;
- no frame overrun;
- channel count and silence policy;
- transport play/stop/seek;
- note-on/note-off;
- MIDI 1 pitch bend;
- CLAP note tuning expression;
- state save/load while inactive;
- output checksum and diagnostic counters.

Loop, tempo-change, articulation fallback, voice stealing, and cancellation tests run in focused submatrices to keep the total test duration bounded while preserving deterministic coverage.

### 7.2 Official CLAP validator

- Tool: official `free-audio/clap-validator` release `0.4.1`.
- The tag is resolved to an exact Git commit during implementation and the commit is recorded in the evidence manifest.
- The release CLAP binary is validated.
- Full raw stdout/stderr, tool version, commit, command, exit status, binary SHA-256, and timestamp are stored.
- Missing tool, network failure, build failure, timeout, or non-zero validation result is not converted to PASS.
- Phase 12C engineering completion requires validator `PASS`.

### 7.3 Two-hour soak

The final soak duration is exactly 7,200 seconds. A shortened CI smoke may exist, but it cannot satisfy Phase 12C acceptance.

The soak performs deterministic cycles of:

- play, stop, seek, and loop changes;
- note and technical-lane edits;
- render request cancellation;
- Voicebank relink and exact-hash re-resolution;
- state save/load while inactive;
- live note bursts, pitch bends, expression changes, and voice stealing;
- GUI visibility toggles without full destruction.

Acceptance:

- no crash, hang, underrun, NaN, Inf, stale publication, or invalid cache reuse;
- resident memory after warm-up does not show monotonic unbounded growth;
- thread and file-descriptor counts return to their expected steady-state range;
- audio-thread allocation probe reports zero allocations after activation.

### 7.4 GUI lifecycle test

Exactly 1,000 cycles:

```text
create → set_parent → set_size → show → timer/update → hide → destroy
```

The harness alternates supported sizes and includes Unicode lyric content. Each cycle records window/resource counters. The test fails on a leak, invalid callback, stale timer, host-handle use after destroy, or non-deterministic crash.

### 7.5 Render cancellation storm

- At least 10,000 render revisions.
- A newer revision cancels the previous pending or active request.
- Only the latest revision may publish.
- Cache writes from stale revisions are rejected or quarantined.
- The final PCM hash must equal a clean single render of the final revision.

## 8. Target OS and DAW validation model

### 8.1 Status separation

Implementation state and test result are separate fields.

Implementation state:

```text
NOT_STARTED
SOURCE_READY
CI_CONFIGURED
TARGET_BUILD_PASS
```

Test result:

```text
NOT_RUN
BLOCKED
FAIL
PASS
```

`SOURCE_READY` or `TARGET_BUILD_PASS` never implies runtime `PASS`.

### 8.2 Windows mandatory runtime coverage

Required actual target evidence:

- Win32 child editor lifecycle;
- TSF/IME composition with Korean and Japanese text;
- WASAPI output and input;
- 44.1/48/96/192 kHz coverage subject to device support;
- buffer matrix supported by the host/device;
- CLAP scan and dynamic load;
- state restore and GUI resize;
- process stress and soak;
- installer, update, uninstall, and Authenticode evidence before RC.

### 8.3 macOS mandatory runtime coverage

Required actual target evidence:

- Cocoa child `NSView` lifecycle;
- `NSTextInputClient` composition with Korean and Japanese text;
- CoreAudio input/output;
- `.clap` bundle scan;
- state restore and GUI resize;
- process stress and soak;
- Developer ID signing, notarization, stapling, PKG install/update/uninstall before RC.

### 8.4 Commercial host coverage

Mandatory hosts are listed in `docs/phase12c/MANDATORY_TARGET_OS_AND_DAW_VALIDATION_KO.md` and the machine-readable matrix. Every host result records exact host version, OS version, plugin SHA-256, test cases, evidence paths, operator, date, and outcome.

No source-level harness can mark a commercial host as PASS.

## 9. Error handling and diagnostics

All Phase 12C tooling emits a structured JSON result and a human-readable log.

Common failure categories:

```text
voicebank-missing
voicebank-untrusted
articulation-inventory-invalid
live-resource-publication-busy
unsupported-note-expression
invalid-midi-event
realtime-allocation
buffer-overrun
nan-or-inf
stale-render-published
validator-not-run
validator-failed
target-runtime-not-run
commercial-host-not-run
```

The plug-in emits silence rather than corrupted audio when live resources are unavailable. The GUI displays a non-blocking diagnostic and exact Voicebank identity.

## 10. Security and resource limits

- Voicebank file access remains outside the callback.
- Maximum live resource decoded PCM: 256 MiB.
- Maximum 32 voices.
- Maximum 8 output channels.
- Maximum 192 kHz.
- Maximum 1024 frames in the mandatory matrix; the existing activation hard limit remains defensive.
- No executable content in Voicebanks.
- Exact content hash and trust receipt validation remain mandatory.
- Validator and external tooling revisions are pinned and recorded.
- Soak and host evidence files are bounded and rotated.

## 11. Documentation deliverables

Phase 12C implementation must create and maintain:

```text
docs/phase12c/ACCEPTANCE.md
docs/phase12c/IMPLEMENTATION_REPORT.md
docs/phase12c/EVIDENCE.md
docs/phase12c/MANDATORY_TARGET_OS_AND_DAW_VALIDATION_KO.md
docs/phase12c/MANDATORY_TARGET_OS_AND_DAW_VALIDATION.md
docs/phase12c/mandatory-validation-matrix.json
```

The mandatory documents are release-gate inputs, not informational appendices.

## 12. Acceptance and release gates

### 12.1 Phase 12C engineering completion

All must pass:

- Voicebank-driven attack/transition/sustain/release.
- Legato and deterministic transition fallback.
- Pitch bend, CLAP note expression, and MIDI 1 dialect.
- De-click voice stealing.
- Official `clap-validator` 0.4.1.
- Linux 336-case process matrix.
- Exact two-hour soak.
- 1,000 GUI lifecycle cycles.
- 10,000-revision cancellation storm.
- ASan, UBSan, and TSan focused suites.
- Realtime allocation probe.
- Master-only branch policy.
- License audit.
- Mandatory future-validation documents and matrix connected to release readiness.

### 12.2 Product maturity gates

```text
G2 Feature Complete
- Phase 12C engineering completion PASS
- Linux validator and stress evidence PASS

G3 Beta
- Windows runtime PASS
- macOS runtime PASS
- REAPER PASS
- Bitwig Studio PASS
- Logic Pro PASS

G4 Release Candidate
- All declared supported DAWs PASS
- Signing/notarization PASS
- Clean-OS installer/update/uninstall PASS
- VST3/AU validators PASS for formats declared supported

G5 General Availability
- Official Voicebank 01 accepted
- EULA and Voicebank licence final
- Mandatory validation unresolved count = 0
```

A Phase 12C completion commit may coexist with Windows/macOS/DAW entries marked `NOT_RUN`, but those entries block G3 and later gates.

## 13. Verification artifacts

Minimum evidence set:

```text
phase12c-verification-matrix.json
phase12c-clap-validator.log
phase12c-clap-validator.json
phase12c-process-matrix.json
phase12c-soak-summary.json
phase12c-soak-timeseries.csv
phase12c-gui-lifecycle.json
phase12c-cancellation-storm.json
phase12c-realtime-probe.json
phase12c-live-articulation.wav
phase12c-live-articulation-spectrogram.png
phase12c-package-verification.txt
```

Every evidence file records the tested commit, binary SHA-256, build type, compiler, OS, CPU architecture, and start/end timestamps.
