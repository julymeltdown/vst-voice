# ADR 0011: Execute every declared renderer through an explicit backend

**Status:** Accepted
**Date:** 2026-08-16

## Context

Phase 3 could persist `SpectralClassic` and `Stretch` renderer choices but did not implement those backends. It could only reject them or explicitly fall back to Raw. Continuing that arrangement would make renderer metadata misleading and prevent the Unit Lane from showing a complete rendering plan.

Project SEAM also has an unusual product requirement: a cleaner renderer must not silently erase sample boundaries. Renderer state therefore cannot span semantic Unit boundaries unless the user explicitly asks for that behavior.

## Decision

The dispatcher now owns four concrete, inspectable backends:

```text
RawLoopRenderer
ClassicPsolaRenderer
SpectralClassicRenderer
StretchUnitRenderer
```

`SpectralClassicRenderer` transforms only the stable vowel with STFT analysis/resynthesis. The source consonant, transition, and release remain direct sample material. Phase continuity, phase reset, and coarse formant-envelope correction are explicit parameters.

`StretchUnitRenderer` is a deterministic first-party granular implementation. Its state is created and destroyed for each Unit. It cannot carry phase or texture across an incoming sample boundary.

Raw fallback remains available only when the selected backend rejects a specific input and the caller explicitly enables fallback. Dispatch results always expose requested renderer, actual renderer, fallback state, and diagnostic text.

## Consequences

- A project can use all four renderer types in one Phrase.
- The Unit Lane can report actual execution rather than intended metadata.
- Spectral rendering is suitable for quality/fallback work but is not yet claimed to be a low-latency preview renderer.
- Granular stretch keeps audible Unit identity and avoids phrase-level naturalization.
- Future backends must follow the same explicit dispatch and diagnostic contract.
