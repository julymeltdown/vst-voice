# ADR 0006: Generated Phonemes with Canonical User Overrides

- Status: Accepted
- Date: 2026-08-16

## Decision

Generated phoneme sequences are derived state. The canonical project stores lyrics, notes, and only the user's explicit phoneme symbol/timing/lock overrides.

## Rationale

A phonemizer implementation will change over time. Persisting every generated token would make upgrades ambiguous and produce duplicated sources of truth. Storing only user intent allows the current phonemizer to regenerate its base plan while preserving deliberate edits.

## Consequences

- `PhonemeKey` is stable within a note by ordinal.
- a lyric change may orphan an override; the phonemizer emits a warning instead of silently applying it to another token;
- project schema 2 adds `phonemeOverrides` to each vocal region;
- schema 1 projects migrate with an empty override collection;
- removing a note removes its overrides and undo restores them at their original positions.
