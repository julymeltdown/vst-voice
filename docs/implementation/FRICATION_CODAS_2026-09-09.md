# Final frication gestures

Explicit unvoiced frication bindings now support coda position as well as onset.
The resolved start must lie at or after its same-note nucleus; the preceding
vowel must end without overlap. Coda end comes from compiled timing. Missing
sources, inconsistent nuclei, unsupported voicing and invalid spans still reject.
The existing simple-coda timing policy can allocate an untimed single coda.

The frication lane supplies sustained band-shaped noise and its existing edge
envelope, not a stop closure/burst. Voicing is gated during the coda. Released
stops retain their distinct silent closure. Articulation plan/stream revisions
advance to 7 and the procedural renderer revision to 12 for cache invalidation.

## Verification

Full Release build and four focused suites passed in 30.06 seconds: voice design,
performance snapshot, export and aggregate core. Extended tests compare released
p/t/k and sustained s codas, terminal zero, replay inside the final gesture,
overlap rejection and unsupported symbols.

The actual English `aa1 s` export uses a 24,000-frame 48 kHz phrase, with the
vowel [0,21120) and frication [21120,24000). The candidate reloads as schema 2
with a typed final Frication marker and exact PCM equality to the render. Unlike
the released-stop comparison, the initial part of the coda contains noise.
The retained WAV SHA-256 is
`ecd347534ec3c154b5849bfa4222cc295dbecc0b3758a76edd48d6df665fa304`.

Logs and the generated bake are in `evidence/frication-coda-2026-09-09/`. These
are engineering fixtures, not independently recognized pronunciation or a
qualified female voicebank. Voiced fricatives, richer clusters/coarticulation,
language-specific realization and the full Beta GO scope remain unfinished.
This was not a full-suite run or a new whole-roadmap-unit acceptance.

No files were missing relative to `session-preservation-HIOkxr`. Existing dirty
work was retained without staging, committing or pushing.
