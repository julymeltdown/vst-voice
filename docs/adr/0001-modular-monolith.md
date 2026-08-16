# ADR 0001: Modular Monolith

- Status: Accepted
- Date: 2026-08-16

## Decision

Build the desktop editor as a modular monolith with explicit library boundaries.

## Rationale

Musical state, edit history, render invalidation, and audio playback require low-latency in-process coordination. Network services add serialization, availability, and debugging costs without helping a local-first editor.
