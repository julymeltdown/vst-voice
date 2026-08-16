# ADR 0016: Give playback transport state to the feeder and ring reset to the consumer

**Status:** Accepted
**Date:** 2026-08-16

## Context

Direct UI mutation of feeder state races with a future feeder thread. Producer-side ring clearing also violates SPSC ownership because the consumer owns the read index.

## Decision

UI operations enter a fixed-capacity SPSC control queue. Only the feeder consumes commands and mutates transport state. A reset request uses a monotonic epoch; the audio consumer discards queued frames by moving its own read index, zero-fills the reset block, and acknowledges the epoch.

## Consequences

- A native feeder thread can be attached without changing transport ownership.
- Control queue overflow is explicit rather than blocking.
- Seek/timeline changes wait for callback acknowledgment before new audio is fed.
