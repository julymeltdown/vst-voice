# Phase 6 Acceptance — Multichannel Project Routing

- [x] Project schema 4 persists buses, bus sends, device routes, and track output matrices.
- [x] Routing graph validates matrix dimensions and rejects cycles.
- [x] Mono and interleaved multichannel source PCM can be routed through 1–8 channel buses.
- [x] Track/clip and bus gain, mute, solo, and downstream send processing are deterministic.
- [x] Bus solo includes required upstream dependencies and downstream device paths without enabling sibling buses.
- [x] Device routes map buses to 1–8 physical output channels.
- [x] Interleaved SPSC ring preserves complete frames and consumer-owned reset semantics.
- [x] Dedicated multichannel feeder supports play, pause, seek, loop, end-of-timeline stop, and watermark filling.
- [x] Audio callback processor writes independent channels without allocation or locks.
- [x] Threaded and PulseAudio device adapters accept up to eight output channels.
- [x] Four-channel WAV and project evidence are produced by a real routing graph.
- [x] Existing mono/stereo playback path remains compatible.
