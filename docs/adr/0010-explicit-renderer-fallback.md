# ADR 0009: Renderer fallback must be explicit

## Status

Accepted for Phase 3.

## Context

A unit may request Raw, Classic PSOLA, SpectralClassic, or Stretch rendering. Phase 3 implements Raw and Classic PSOLA only. Silently substituting another renderer would make saved projects misleading and make sound changes difficult to diagnose.

## Decision

`UnitRendererDispatcher` returns both requested and actual renderer, a fallback flag, and a diagnostic. Fallback is controlled by an explicit policy flag.

- Classic PSOLA failure may fall back to Raw only when allowed.
- SpectralClassic and Stretch requests return `Unsupported` when fallback is disabled.
- When fallback is enabled, Raw is used and the diagnostic states that the requested renderer is not implemented.
- Cancellation is never converted into a fallback render.

## Consequences

- The project schema can reserve future renderer choices without claiming they already exist.
- Editor and diagnostic views can expose exactly how a unit was rendered.
- Automated rendering remains deterministic.
- Phase 3 documentation must distinguish recognized renderer identifiers from implemented renderer backends.
