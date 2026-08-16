# ADR 0019: Own playback production with a dedicated feeder service

**Status:** Accepted  
**Date:** 2026-08-16

## Context

Phase 4.1 assigned mutable transport state to `PlaybackFeeder` and reset ownership to the audio consumer, but tests still called `feedOnce()` directly. A native audio device needs a continuously running producer that cannot block or mutate callback-owned state.

## Decision

Add `PlaybackFeederService`, which owns the only thread allowed to call `PlaybackFeeder::feedOnce()` or `feedToWatermark()`.

UI callers use service proxy methods:

```text
setTimeline
setLoop
setPlaying
seek
```

Each method enqueues the existing fixed-capacity control command and wakes the service. The service keeps a configurable high watermark and sleeps when no production is necessary. The callback remains the sole ring-buffer consumer.

## Consequences

- Native playback has explicit producer and consumer ownership.
- UI code never executes mixing or PCM production.
- Service start/stop is deterministic and idempotence errors are explicit.
- ThreadSanitizer can exercise transport commands while feeder and callback threads run concurrently.
