# ADR 0013: Treat rendered PCM as bounded disposable data

**Status:** Accepted
**Date:** 2026-08-16

## Context

A content-addressed render cache can grow indefinitely during editing. Rendered PCM is reproducible from the project and voicebank, so unbounded storage is not justified. At the same time, eviction must not affect canonical project data.

## Decision

`PcmCache` now accepts independent limits for:

```text
maximum in-memory payload bytes
maximum on-disk payload bytes
maximum on-disk entry count
```

The memory tier tracks access order and evicts least-recently-used entries until the byte budget is met. The disk tier tracks file modification time and removes oldest entries until both disk constraints are met. Cache payloads remain versioned, checksummed, finite-value validated, and atomically replaced.

The cache reports current usage and eviction counters. The project file never embeds or depends on cache survival.

## Consequences

- Long editing sessions cannot grow cache storage without a configured bound.
- Deleting the cache remains safe.
- A payload larger than the memory budget can exist on disk without being retained in memory.
- Future UI can expose cache usage and a manual prune command using the existing API.
