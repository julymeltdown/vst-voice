# CLAP Plug-in Runtime Architecture

## Product boundary

The standalone editor remains the authoring and singing-synthesis authority.
Phase 10 introduces a CLAP render-player boundary:

```text
SEAM Editor / render CLI
        |
        | bounded `SEAMCLP1` state
        v
ProjectSEAM.clap
        |
        | host transport + master-gain automation
        v
1–8 channel host output
```

The plug-in does not invoke the phonemizer, Unit selector, PSOLA, spectral
renderer, Voicebank filesystem, Character package, or `.seambank` installer on
the host audio thread. This is intentional: all expensive or filesystem-based
work remains outside `process()`.

## CLAP lifecycle

```text
DSO load
  -> clap_entry.init
  -> factory scan
  -> create_plugin
  -> plugin.init
  -> state.load while inactive
  -> audio-port scan
  -> activate(host sample rate, block bounds)
  -> start_processing
  -> process blocks
  -> stop_processing
  -> deactivate
  -> destroy
  -> clap_entry.deinit
```

Entry initialization is reference-counted under a process-local mutex because
CLAP 1.2 permits matched repeated `init`/`deinit` calls in wrapper scenarios.
The mutex is never used from `process()`.

## State and activation

`PluginSession` owns source-rate interleaved PCM and immutable product metadata.
On activation, the state is linearly resampled on the main thread to the host
sample rate. One through eight channels are preserved.

A state load while active is rejected and `host.request_restart()` is issued.
This conservative contract avoids lock-taking, reference-count destruction, or
session reclamation on the audio thread. Hosts normally restore state before
activation; future asynchronous hot replacement requires a separately audited
main/audio ownership protocol.

## Audio-thread contract

`process()` performs only:

- output pointer validation;
- bounded event-list traversal;
- host-position conversion;
- contiguous PCM lookup;
- planar output writes;
- scalar gain conversion and multiplication;
- atomic master-gain publication.

It performs no dynamic allocation, file I/O, state decoding, resampling,
voicebank access, project traversal, mutex locking, logging, or character work.

## Transport

Priority:

1. CLAP seconds timeline;
2. CLAP beats timeline plus tempo;
3. free-running process cursor when no transport is supplied.

When a transport exists without `IS_PLAYING`, output is silence. Position is
interpreted at the beginning of the block. Sample-accurate transport events
inside a block are deferred; parameter events are sample accurate.

## Parameter

Phase 10 exposes one stable parameter:

```text
ID        0x534D4701
Name      Master Gain
Range     -60 dB .. +6 dB
Default   0 dB
Flags     AUTOMATABLE | REQUIRES_PROCESS
```

Input events are sorted by the host. The processor applies each value at
`event.header.time` and writes subsequent samples with the new gain.

## Ports

- Inputs: zero.
- Outputs: one main 32-bit-capable port.
- Channel count: state-defined, one through eight, fixed during activation.
- Port type: mono for one, stereo for two, unspecified arbitrary audio for
  three through eight.

## Character boundary

Character 01 is not part of the CLAP render state and never changes PCM,
transport, port count, automation, state digest, or host classification. A
future GUI may present Character 01 as optional product identity under the
existing Full/Minimal/Off rules, but the Phase 10 module is headless.


## Authoring-to-plug-in state bridge

The Phase 10 module does not open Project or Voicebank files. A separate
non-realtime tool performs the product boundary conversion:

```text
Standalone multichannel WAV export
→ seam_clap_state_tool pack
→ bounded SEAMCLP1 state
→ host state stream
→ inactive plug-in state load
→ activation-time resampling
→ realtime playback
```

The same tool can inspect and extract state. This keeps all WAV parsing, state
allocation, validation, and durable file writes outside `process()`.
