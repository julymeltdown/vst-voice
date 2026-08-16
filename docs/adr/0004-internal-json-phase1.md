# ADR 0004: Internal JSON Parser for Phase 1

- Status: Accepted for Phase 1
- Date: 2026-08-16

## Decision

Implement the narrow JSON feature set needed by the project draft instead of adding a runtime dependency before the license intake system exists.

## Consequence

The parser requires fuzzing before hostile input is accepted. A later approved parser may replace the adapter without changing the domain.
