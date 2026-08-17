# Phase 10 Implementation Report

## Summary

Phase 10 adds the first real plug-in-format boundary: a loadable CLAP 1.2.10
module named `ProjectSEAM.clap`. It is a host-synchronized, state-backed player
for a pre-rendered one-to-eight-channel Project SEAM vocal result.

This phase intentionally does **not** embed the full editor or run singing
synthesis inside the host audio callback. It establishes lifecycle, state,
ports, transport, automation, dynamic loading, and hard realtime boundaries
before higher-risk DAW integration work.

## Implemented modules

### `seam_clap`

- `PluginSession` validation.
- Deterministic multichannel diagnostic render generation.
- Main-thread linear sample-rate conversion.
- Decibel-to-linear gain conversion.
- `SEAMCLP1` state codec.
- Durable state-file writes.
- Strict bounded state reads and SHA-256 corruption detection.

### `ProjectSEAM.clap`

- CLAP entry and thread-safe factory scanning.
- One stable descriptor: `com.project-seam.render-player`.
- Zero audio inputs and one dynamic main output.
- One through eight output channels.
- State extension with partial stream handling.
- Audio Ports, Parameters, State, Latency, Tail, and Render extensions.
- Host seconds transport, beat/tempo fallback, and free-run mode.
- Deterministic pause silence.
- Sample-accurate Master Gain value events.
- Main-thread activation/resampling and no-allocation process path.

### `seam_clap_host`

A first-party ABI smoke host which loads the built `.clap` binary through
`dlopen` or `LoadLibrary`, obtains `clap_entry`, scans the factory, creates two
instances, exercises partial state streams, scans the output port, activates,
processes four-channel audio, verifies sample-offset automation, checks stopped
transport silence, saves state, and tears the module down.

### `seam_clap_state_tool`

A production-facing command-line bridge converts an existing one-to-eight-channel
WAV export into bounded `SEAMCLP1` state, inspects state metadata, and extracts
state PCM back to WAV:

```text
seam_clap_state_tool pack INPUT.wav OUTPUT.seamclapstate
seam_clap_state_tool inspect INPUT.seamclapstate
seam_clap_state_tool extract INPUT.seamclapstate OUTPUT.wav
```

The dynamic module smoke consumes the state produced by the WAV pack path rather
than a private in-process object. This verifies the actual export-to-plug-in
workflow.

### `seam_phase10_demo`

Produces:

```text
phase10-diagnostic.seamclapstate
phase10-diagnostic-4ch.wav
summary.json
```

The state and WAV are derived from the same deterministic four-channel render.

## Tests

New named tests cover:

- multichannel state round trip;
- Unicode title preservation;
- SHA-256 corruption rejection;
- non-finite PCM rejection;
- durable file round trip;
- sample-rate conversion;
- decibel conversion.

CTest adds a dynamic-module host smoke dependent on the generated Phase 10
state. This catches missing exports, factory or extension regressions, stream
contract failures, channel mismatches, transport bugs, and automation bugs.

## Dependency

The official CLAP public headers are MIT licensed. Project SEAM vendors only a
mechanically consolidated ABI subset required by this phase, based on CLAP
1.2.10 revision `195b42a004144fab0b3cf95e9c067187d15365b7`. Provenance,
license text, source hash, third-party notice, and SPDX relationship are stored
in the repository.

## Validation boundary

The Linux ELF `.clap` module is built and dynamically executed in the current
environment. The C++ plug-in core and host loader contain Windows branches, but
Windows DAW runtime validation remains an operating-system CI/release gate.
A conforming macOS CLAP bundle is deferred.

## Deferred

- CLAP GUI extension and embedded native editor.
- Asynchronous host-side project rendering.
- Live note-event synthesis.
- Hot active-state replacement.
- Third-party CLAP validator and broad DAW matrix.
- VST3 and AU adapters.
- Code signing, notarization, and installers.
- Contracted production human voicebank.

## Verification results

The final Phase 10 source tree was validated with the following results:

```text
Named tests                     128 passed / 0 failed
Debug CTest                     20 / 20 passed
Release CTest                   20 / 20 passed
ASan + UBSan named tests        128 passed / 0 failed
ASan + UBSan Phase 10 CTest      6 / 6 passed
Dynamic Linux module load       passed
Exported clap_entry             passed
Master-only branch policy       passed
Dependency and license audit    passed
Git object integrity            passed
```

The dynamic host smoke observed the following concrete contract:

```json
{
  "pluginId": "com.project-seam.render-player",
  "channels": 4,
  "framesProcessed": 256,
  "automationRatio": 0.501187,
  "stateRoundTrip": true,
  "activeLoadRejected": true,
  "restartRequests": 1,
  "transportPauseSilence": true
}
```

The `0.501187` ratio is the expected linear amplitude for a `-6 dB` parameter
change. The release state-codec benchmark used a four-channel, 48 kHz,
one-second session for eight iterations:

```text
State bytes          768,123
Encode total          28.8035 ms
Decode total          30.5687 ms
```

These numbers are regression evidence from the current Linux environment, not
performance guarantees for every host or processor.

## Character boundary

Character 01 remains the optional product avatar bound to the official
voicebank and standalone product surfaces. Phase 10 deliberately excludes the
character package from:

```text
CLAP persistent state
CLAP audio-port layout
CLAP process callback
PCM content
host transport mapping
Master Gain automation
state checksum identity
```

Changing, hiding, or removing Character 01 therefore cannot change the plug-in
output or invalidate the `SEAMCLP1` state.
