# Plosive source editing in Voice Designer

The native Designer now exposes creation/removal and four numeric controls for
each plosive source in the selected pose's style: burst center frequency,
bandwidth, gain and nominal duration in milliseconds. This connects the recipe
and rendering work to editable application state without hand-editing JSON.

## Interaction contract

- In Designer, Command/Control-I opens plosive creation; Shift with the same
  shortcut removes the selected plosive source. The macOS form names this
  operation explicitly and accepts p/t/k plus an existing vowel style.
- Command/Control-Shift-R edits the selected plosive's full-width decimal seed.
  Frication seed editing remains available when a frication control is selected.
- The existing numeric controls support stepping, pointer gestures and semantic
  value edits. The new rows precede nasal controls and participate in paging.
- Add/remove/seed accessibility actions bind the current Designer epoch and
  recipe revision. Session validation rejects stale targets, invalid symbols,
  absent styles, ambiguous bindings, invalid durations and noncanonical seeds.
- Changes use existing immutable-resource history and save/reopen behavior.
  Creating a source begins with a 10 ms burst and the current global seed;
  later source seed edits do not change the global seed or other noise sources.

The initial spectrum is an unqualified starting point, not a phonetic preset.
The existing vowel audition still auditions vowels; the frication-only audition
does not silently substitute for stop audition. Dedicated plosive audition and
full native interaction qualification remain open. The existing generation-job
path can render a compatible stop/vowel phrase from the edited recipe.

## Verification and limits

Full Release build passed. Four focused suites passed in 26.57 seconds:
Designer, voice design, export and aggregate core. The added session test covers
creation, duplicate/missing-style rejection, stale revisions, full unsigned seed
precision, unrelated-source preservation, duration/filter edits, invalid edit
rollback, removal, undo/redo and asynchronous save/reopen.

This is not a fresh full-suite or live UI PASS. Studio was launched against an
engineering producer fixture, but the UI tool could not attach to the standalone
executable by name or path. That test process was stopped; no recording occurred.
macOS dialog and application code compiled; Windows/Linux dialog availability
and native input/accessibility behavior are not established by these tests.

Logs are retained under `evidence/plosive-controls-2026-09-09/`. Comparison against
`session-preservation-LGrCNu` found no missing captured files. Existing dirty work
was preserved, with no staging, commit or push. No whole roadmap unit, singer
quality, source rights or full Beta GO acceptance is claimed.
