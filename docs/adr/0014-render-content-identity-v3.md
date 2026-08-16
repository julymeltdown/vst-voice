# ADR 0014: Bind cached PCM to selected audio bytes and effective algorithms

**Status:** Accepted
**Date:** 2026-08-16

## Context

The Phase 3/4 content key did not include actual WAV bytes or all renderer settings and included the complete voicebank manifest. It could therefore reuse stale PCM after selected audio changed and invalidate unaffected phrases after unrelated metadata changed.

## Decision

Render Snapshot construction performs phonemization and deterministic Unit selection, resolves only selected audio assets, hashes each selected WAV with SHA-256, and builds a versioned SHA-256 identity from phrase state, selected Unit metadata, effective render/seam settings, render quality, sample rate, style, and generated algorithm revisions.

The immutable snapshot stores the exact phoneme result and Unit plan used by rendering.

## Consequences

- Selected audio or algorithm changes invalidate the correct PCM.
- Unused Unit changes do not invalidate the Phrase.
- Snapshot construction is more expensive because selected files are hashed. A signed install-time content index may optimize this later without changing identity semantics.
- Project revision remains separate from content identity.
