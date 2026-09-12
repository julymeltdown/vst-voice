# Procedural candidate v3 — nasal gestures

Candidate v3 extends the v2 20-field shape with a third marker kind, `nasal`.
It retains `planned-articulated-gestures` and `approval: unapproved`. It must
contain at least one nasal and one oral-vowel gesture; frication is optional.
An explicit exception permits all-syllabic-`N` material, with exactly one nasal
gesture per note. No other nucleus-free consonant sequence is admitted.
Vowel-only material still exports v1. Vowel/frication material without a nasal
gesture still exports v2. A nasal marker cannot be downgraded to v2 or relabeled
as an oral vowel to pass validation.

Nasal symbols are the engine's explicit `m`, `n`, `ng`, and `N` identifiers.
Each requires an exact phone/style recipe pose, an explicit nasal resonance model,
positive nasal coupling, and a valid tract at the candidate's actual sample rate.
This is source/gesture binding, not an assertion that those sounds are recognized
correctly by listeners. Distinct phonetic qualities require appropriately designed
per-phone models and independent evaluation.

## Rendering and timing

The nasal consonant path uses voiced excitation through the configured nasal
resonance and antiresonance, multiplied by nasal coupling. The oral output is
closed: unlike vowel coloration, no oral-bank output is mixed into a nasal
consonant. The stored oral bands remain structurally validated but do not shape
the nasal output. Adjacent voiced nasal/vowel gestures use independent-bank
crossfades without resetting the excitation phase.

Onsets use a resolved explicit or inferred start and end no later than their
same-note vowel nucleus. Codas require a resolved post-nucleus start; the vowel
must end without overlap. Missing/unresolved starts, unsupported bindings,
inconsistent voicing/nucleus keys, overlap and out-of-note context reject.
For a sole Japanese `N` token on a note, the note's fallback time span is used
without inventing a vowel nucleus. Timing-policy revision 3 allocates a bounded
tail for one coda in a wholly untimed syllable. Cluster allocation, other
nucleus-free consonant sequences, closures and bursts remain unsupported.

Articulation-plan revision is 4, articulated-stream revision is 4, and sustained
renderer revision is 9. Revision fields and the full recipe identity remain in
render/cache/export provenance. Candidate schema, recipe schema and algorithm
revisions are separate contracts.

All existing size, marker-bound, Float32/mono, digest, cancellation, immutable
lineage and producer-review requirements remain. Imported takes stay MarkerReview;
neither a new kind nor a successful bake authorizes source use, distribution,
musical approval or Beta GO.
