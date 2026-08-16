# Phase 6 Implementation Report

Phase 6 introduces a persisted, acyclic multichannel routing graph between rendered vocal phrases and the physical output device.

## Domain

`ProjectRouting` owns audio buses, bus sends, device output routes, a master bus, and a device channel count. `TrackOutputRoute` maps a track source into a destination bus through an explicit matrix. Project JSON moved to schema 4; schemas 1–3 migrate to the former stereo behavior.

## Rendering

`RoutedPlaybackTimeline` accepts mono or interleaved 1–8 channel PCM clips. It mixes clips into buses, applies bus gain/mute/solo, processes bus sends in deterministic topological order, and maps buses into device channels. `RoutingWorkspace` preallocates bus buffers for a bounded block size.

## Playback

`SpscInterleavedAudioRingBuffer` indexes complete frames rather than individual samples and rejects partial-frame spans. `MultichannelPlaybackFeeder` and its dedicated service preserve the Phase 4.1 control-queue and consumer-owned reset contract, and stop producing after the end of a non-looping timeline. `MultichannelRingBufferAudioProcessor` deinterleaves into the device-provided channel views inside the callback without allocation, mutexes, file I/O, or graph traversal.

## Platform

The deterministic threaded adapter and Linux PulseAudio adapter now accept 1–8 output channels. The former stereo processor and timeline remain available for compatibility.

## Evidence

`seam_phase6_demo` produces a 4-channel WAV in which the vocal bus occupies physical channels 1–2 and the backing bus occupies channels 3–4. The same graph is fed through the interleaved ring and callback processor with zero underflow in the smoke path.
