# Nasal timing display repair

The shared phoneme lane was compiling procedural timing correctly but then
replacing coda geometry with a fixed 28-pixel source estimate. That affected
inferred codas, manually started procedural codas with derived ends, and
standalone syllabic `N` notes.

Resolved procedural Onset/Coda spans now bypass the estimated-box fallback.
Onsets still end at their nucleus when no explicit end exists; codas use the
compiled end. A sole nucleus-free syllabic `N` displays its actual note-owned
span. The sample/source-dependent estimate path remains unchanged.

Regression checks at 48 and 192 pixels per quarter note prove:

- An inferred `/aN/` coda starts at 440 ms and ends at 500 ms, adjoining the
  preceding vowel rather than overlapping it with a 28-pixel estimate.
- Boundary hit-testing selects the resolved coda start.
- A manual 400 ms start remains authored, not inferred or estimated.
- Standalone `ん` fills its note's span without inventing a vowel.
- Switching back to a sample-based track restores the source-dependent estimate.

This is model-geometry and hit-testing evidence, not a new live visual/IME or
accessibility qualification. Full Beta GO remains open. The full Release build
passed, and **5/5 focused suites passed (23.24 seconds)**: core, timing,
snapshots, U2 and U3 standalone. No new full release-matrix run is claimed.
Verification logs are retained under
`evidence/nasal-timing-display-2026-09-09/`.

No previously captured files were missing against `session-preservation-hnL1uy`.
Existing dirty work was preserved; no staging, commit or push occurred.
