# ADR 0002: Integer Musical Time

- Status: Accepted
- Date: 2026-08-16

## Decision

Store canonical score position and duration as signed 64-bit integer ticks. Convert to seconds and sample frames through a tempo map.

## Rationale

Floating-point seconds accumulate drift and make snapping, exact undo, tempo changes, and format migration harder. Pixel coordinates remain transient.
