# Simple procedural coda timing

Procedural timing-policy revision 3 now allocates one coda in a wholly untimed
syllable, optionally alongside one onset. Clusters and groups containing any
authored timing are not inferred. Source-dependent timing is unchanged.

After resolving following-syllable dependencies, the coda reserves
`max(1 frame, min(60 ms, floor(resolvedSyllableFrames/4)))` from the tail. Its
start is recorded as inferred, and the vowel ends there. A conflict is returned
if no positive nucleus span remains. Onsets retain the existing allocation rule.
The fractions are engineering defaults, not universal measured phonetic durations.

Nasal articulation accepts the resolved inferred coda boundary as well as an
explicit one; exact nucleus binding, own-note bounds and overlap rejection remain.
Articulation-plan revision 4 and timing-policy revision 3 participate in cache
identity. This adds no permission to synthesize unsupported consonant kinds.

Tests cover vowel/coda and onset/vowel/coda timing, manual-ownership preservation,
unresolved clusters, and recalculation after an edited following nucleus. The
actual Japanese `/aN/` case renders and bakes without manual offsets, loads with
typed vowel/nasal boundaries, imports as MarkerReview and recovers without review
approval. Explicit manual timing and overlap rejection remain exercised.

No captured file was missing relative to `session-preservation-WtBGa2` (1,965
files). Existing dirty work was preserved; no staging, commit, push or real
source/music approval occurred. Verification is retained under
`evidence/automatic-coda-2026-09-09/`.

This is an initial timing policy and does not complete articulation or Beta GO.
The full Release build and **5/5 focused CTest suites passed (28.67 seconds)**:
phoneme timing, voice design, snapshots, export and core. `git diff --check` passed.
No new full 120-entry run is claimed; the prior source-closure limitation remains.

Clusters, closures/bursts, richer source-specific durations and independent
language/music qualification remain required.
