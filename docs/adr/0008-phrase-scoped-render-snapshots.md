# ADR 0008: Use immutable phrase-scoped render snapshots

## Status

Accepted in Phase 3.

## Context

Background workers must never traverse mutable editor state. Hashing an entire project also causes unrelated edits to invalidate every phrase cache entry.

## Decision

A render request owns an immutable copy containing only the selected track and phrase plus the global musical-time events that can affect that phrase. Presentation state and unrelated phrases are normalized or removed. The snapshot includes the voicebank manifest, selected style, sample rate, quality, and engine version.

## Consequences

- workers observe a coherent revision;
- unrelated phrase and presentation edits retain cache keys;
- phrase-affecting tempo, pitch, unit, and seam edits invalidate correctly;
- snapshot creation has an explicit copy/serialization cost;
- voicebank-manifest changes currently invalidate all phrases using that bank, even if the changed unit is unrelated.
