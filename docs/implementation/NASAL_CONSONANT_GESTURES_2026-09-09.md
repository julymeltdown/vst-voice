# Initial nasal-consonant gesture implementation

## Delivered

The procedural pipeline now has a distinct voiced nasal gesture, rather than
requiring every voiced sound to be a vowel or rendering a renamed colored vowel.
This advances U20/R1/R3/R4/R20 but does not complete those requirements or qualify
the phonetic naturalness of any voice.

Supported nasal identifiers (`m`, `n`, `ng`, `N`) require explicit active nasal
poses. Their excitation passes through the nasal resonance/antiresonance path
with the oral contribution closed. Per-phone models remain author-controlled;
using identical parameters does not establish distinguishable consonants.

Onsets can use inferred single-onset timing. Codas need explicit non-overlapping
post-nucleus timing. Both bind a same-note vowel nucleus and use the existing
voiced crossfade/phase-continuity mechanism. Nucleus-free nasal notes, automatic
coda/cluster timing, pre-note context and plosive closures/bursts remain open.

The new kind is propagated through snapshot marker extraction, scheduler
validation/cache replay, Final baking, strict candidate v3 loading and producer
lineage recovery. Candidate v1/v2 remain the vowel-only and vowel/frication-only
formats. Wrong symbols, missing models, kind relabeling and v2 downgrade reject.
Imports remain unapproved MarkerReview takes.

## Verification

The earlier Unsupported-renderer guard evolved into a positive `/ma/` render
test, retaining missing-model rejection. New tests establish:

- Nonzero voiced onset PCM and exact whole/chunk/checkpoint/reset reproduction.
- Cancellation rollback without advancing the retained stream.
- Nasal output differs from vowel coloration and is invariant to oral-band edits.
- An explicitly timed `/aN/` coda renders, while untimed/overlapping codas reject.
- Scheduler chunks crossing the nasal/vowel boundary preserve exact PCM and typed
  nasal markers, including cache hits.
- A real `/ma/` Final candidate bakes as v3, loads, imports and recovers with its
  nasal marker intact and no review approval.

Full Release build passed. Six focused suites passed in **29.15 seconds**:
Designer 24/24, voice design 19/19, snapshot 44/44, export 24/24, core 841/841,
CLI workflow 4/4. Raw focused/full results accompany this report under
`evidence/nasal-consonant-2026-09-09/`.

The subsequent full CTest run was **119/120 PASS, 258.39 seconds**. Only tracked
source closure failed, with 405 unindexed required inputs at execution. No staging
was used to conceal it. Later evidence files can change that count.

The actual `/ma/` bake, including project, recipe, candidate metadata and WAV,
is retained under `evidence/nasal-consonant-2026-09-09/ma-bake/`. Its mono Float32
WAV is 0.5 seconds / 24,000 frames at 48 kHz, finite and non-silent, with zero
clipped samples. Peak is 0.0273583680 and RMS 0.0097459918. Audio SHA-256:
`f67deb2dbe7dfdeba74988b94854973157e32f9d6bcec1b73384841dcc3eecff`.
This is generated output evidence, not a listening judgment.

## Preservation and qualification boundary

No captured file was missing relative to `session-preservation-S9ndat` (1,953
files). Existing dirty work was preserved. No staging, commits, pushes or real
source/reviewer approvals occurred. Generated samples are engineering fixtures,
not a finished female singer or native-speaker pronunciation evidence.

The model needs measured acoustic and listener evaluation, source-specific
consonant poses and longer-phrase coarticulation work. Full Beta GO also still
requires the remaining classical/neural, language/resource, host and release
work. See [the exact candidate contract](../formats/PROCEDURAL_CANDIDATE_V3.md).
