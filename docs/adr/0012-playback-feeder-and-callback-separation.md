# ADR 0012: Mix outside the real-time callback and feed preallocated PCM

**Status:** Accepted
**Date:** 2026-08-16

## Context

The Phase 3 SPSC ring proved ordering and wrap-around, but there was no production-shaped path from cached phrases and backing audio to an audio callback. Mixing project clips, resolving loop boundaries, seeking, or loading cached PCM inside the callback would violate the real-time contract.

## Decision

Playback is split into three layers:

```text
PlaybackTimeline
  absolute-frame clips, gain, fades, vocal/backing mix

PlaybackFeeder
  non-real-time block mixing, loop handling, seek, watermark fill

RingBufferAudioProcessor
  callback-safe ring read, gain, stereo duplication, zero-fill, counters
```

The feeder owns a preallocated scratch buffer. The callback processor performs no file access, no project traversal, no allocation, and no lock acquisition. An underflow is represented by deterministic zero-fill and a counter rather than undefined data.

## Consequences

- Cached vocal phrases and backing clips can be auditioned through one timeline.
- Loop and seek logic are testable without a physical device.
- Native iPlug2 device attachment remains an adapter task rather than an engine rewrite.
- The current transport is mono internally and duplicates to two callback channels. True multichannel routing remains deferred.
