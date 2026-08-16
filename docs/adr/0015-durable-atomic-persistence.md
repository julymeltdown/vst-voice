# ADR 0015: Use one durable atomic replacement primitive for canonical files

**Status:** Accepted
**Date:** 2026-08-16

## Context

Removing the target before renaming a temporary file creates a data-loss window. Stream flush alone does not provide durable file or directory ordering.

## Decision

Project and Manifest persistence use a shared platform primitive that writes a same-directory temporary, durably flushes it, atomically replaces the target, and durably flushes the parent where supported. Existing canonical targets are copied to a durable backup before replacement. Fault injection is a first-class test feature.

Disposable PCM cache entries use the same replacement primitive without backup.

## Consequences

- Failure before replacement preserves the old target.
- Failure after replacement can report an error while the new target exists; the old generation remains in the backup.
- Platform-specific durability behavior is isolated in `seam-core`.
