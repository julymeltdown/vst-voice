# Project SEAM Phase 6 Implementation Report

Phase 6 adds a persisted multichannel project-routing graph and carries that graph through offline mixing, the dedicated feeder thread, the SPSC handoff, and the device callback contract.

## Implemented scope

- Project JSON schema 4 with 1–8 channel buses, sends, device routes, and per-track output matrices.
- Deterministic acyclic routing validation and topological processing.
- Mono and interleaved multichannel clips.
- Track/clip gain, mute/enable, solo, pan-derived matrices, bus gain, bus mute, bus solo, and downstream sends.
- Correct bus-solo dependency semantics: upstream dependencies and downstream output paths remain audible without enabling sibling buses.
- Preallocated bus workspaces and interleaved device output.
- Frame-oriented multichannel SPSC ring with consumer-owned reset epochs.
- Dedicated multichannel playback feeder and service thread.
- End-of-timeline stop behavior for non-looping playback.
- Allocation-free callback deinterleaving into 1–8 device channel views.
- Linux PulseAudio and deterministic threaded output adapters generalized to 1–8 channels.
- Four-channel WAV, schema-4 project, and callback evidence from `seam_phase6_demo`.

## Compatibility

The former mono/stereo `PlaybackTimeline`, feeder, and callback processor remain available. Schemas 1–3 migrate to a stereo master bus and equal-power mono routing.

## Explicit exclusions

Phase 6 does not implement signed `.seambank` distribution or Windows/macOS native platform adapters. Those are the next sequential phases.
