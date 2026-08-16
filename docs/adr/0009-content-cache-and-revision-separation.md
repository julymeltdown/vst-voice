# ADR 0009: Separate PCM content identity from project revision

## Status

Accepted in Phase 3.

## Context

A project revision identifies freshness, while a PCM cache key identifies audio content. Including revision in a cache payload prevents valid reuse when an unrelated edit increments the project revision.

## Decision

`CachedPcm` contains only sample rate, absolute start frame, and immutable samples. Scheduler requests and stale-audio publications carry revision separately. A completion may reuse an existing content entry and publish it under a newer project revision.

## Consequences

- unchanged phrase audio survives unrelated project revisions;
- stale detection remains explicit in the scheduler/publication layer;
- the cache format is independent of editor history;
- callers must never infer freshness from a PCM object alone.
