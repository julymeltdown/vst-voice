# Voiced procedural onset timing

Procedural timing-policy revision 2 removes the blanket exclusion of voiced
onsets. A wholly untimed syllable with one Onset before a voiced nucleus now
receives the existing bounded default whether its onset is voiced or unvoiced.
Source-dependent timing remains unchanged.

The reservation remains `max(1 frame, min(60 ms, floor(syllableFrames/4)))`.
Onset voicing and nucleus identity are preserved; generated starts remain
separate from authored timing. Previous vowels stop before a generated following
onset. Any authored timing in a syllable disables inference for that syllable.
Codas and consonant clusters remain outside this default. The policy revision
participates in existing procedural render identities to invalidate old caches.

Tests resolve actual Japanese `まな` to voiced `m`/`n` onsets and verify distinct
inferred starts, normal/short-note scaling, preserved manual 90 ms timing and
unchanged tokens. Separate cases keep coda/cluster timing unresolved.

A compiled-performance/articulation regression resolves `ま`, obtains valid
voiced-onset timing, and supplies an explicit nasal-colored `m` pose. Articulation
still rejects it as Unsupported: timing and coloration do not substitute for a
nasal-consonant renderer. This guard must evolve into positive phonetic/render
evidence when that capability is implemented, not simply be removed.

This is a U20 timing prerequisite, not completed consonant synthesis. Nasal
gestures, coda timing, closures/bursts, extended context and all remaining
Full-Scope Beta GO requirements remain open. No approval or release is implied.

The full Release build and **5/5 focused CTest suites passed (31.62 seconds)**:
voice design, phoneme timing, performance snapshots, export and core. This was
not a new full 120-entry run; prior source-closure/release limitations remain.
Focused results are retained under
`evidence/voiced-onset-timing-2026-09-09/`. Existing dirty files were preserved;
comparison against `session-preservation-3v6SUJ` found no missing captured files.
No staging, commits or pushes occurred.
