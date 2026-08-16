# ADR 0017: Bound untrusted project, voicebank, and cache data before allocation

**Status:** Accepted
**Date:** 2026-08-16

## Context

Project files, manifests, and disposable cache entries can be corrupt or attacker-controlled. Unbounded recursive parsing and allocation from declared frame counts can exhaust memory or stack resources before semantic validation.

## Decision

File reads have product-specific byte limits. JSON parsing has depth, node, string, and collection budgets and uses exact signed integers. PCM cache loading validates actual file size against declared payload before sample allocation. Voicebank asset resolution rejects symlinks and canonical escapes.

## Consequences

- Hostile declarations fail before large allocation.
- Very large legitimate files may require future explicit format or limit revisions.
- Limits are centrally documented and regression tested.
