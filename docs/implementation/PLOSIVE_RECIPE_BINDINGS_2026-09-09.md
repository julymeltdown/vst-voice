# Plosive recipe persistence

Added explicit schema-4 `p/t/k` source bindings with style, full-width seed,
bounded spectrum/gain and nominal burst duration. Shared validation rejects
duplicate plosive bindings and plosive/frication ambiguity. Canonical resource
version/hash binding includes all new settings and retains prior schema bytes
when no plosive is present.

Tests cover mixed nasal/frication/plosive roundtrip, file save/reload, immutable
prior resources, duration-driven hash changes, malformed fields/seeds/durations,
schema downgrade, empty schema-4 arrays, unsupported styles and ambiguous sources.
A Designer model test proves unrelated edits, undo/redo and persistence do not
drop plosive data.

This is a persistence prerequisite, not stop-phrase rendering or completed U20.
The next connection is from timed phonemes to the closure/burst source, with
typed candidate anchors and native controls. Full Beta GO remains open.

The full Release build passed. Focused results accompany this report under
`evidence/plosive-recipe-2026-09-09/`. No captured files were missing relative to
`session-preservation-941bLE`; existing dirty work was preserved. No staging,
commits, pushes or source/music approvals occurred.

## Recovered verification evidence

The completed focused CTest run passed all five suites in 27.73 seconds:
Designer 25 cases, voice design 24, performance snapshot 44, export 24,
and aggregate core 842. Suite counts overlap; they are not a unique-test total
or a product-completion percentage. This was not a new full-suite run.
The original CTest output and detailed test log are retained in the evidence
directory above.

A fresh hash comparison against the checkpoint captured at
2026-09-08T23:35:13.814Z found no missing captured files. Only the recipe header,
recipe codec, resource codec and the two corresponding test files differed.
The new format and implementation documents are additional files. This proves
continuity relative to that checkpoint, not that no earlier session lost work.
