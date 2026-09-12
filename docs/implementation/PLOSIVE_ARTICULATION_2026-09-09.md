# Plosive articulation, candidate bake and producer import

The schema-4 p/t/k bindings now drive the actual procedural rendering path.
This is a completed integration increment, not completion of U20 or Beta GO.

## Implemented behavior

- Articulation preparation resolves the selected style's explicit stop binding,
  validates a same-note vowel nucleus, and allocates the authored nominal burst
  at the end of the onset. The remaining onset is silent closure. Invalid timing,
  insufficient closure room, ambiguous sources and unsafe actual-rate spectra reject.
- The aperiodic lane renders the finite seeded burst rather than a sustained
  frication envelope. Voicing is gated during the stop. Checkpoint copies,
  cancellation rollback, reset and prefix replay preserve deterministic samples.
- Typed Plosive markers survive snapshot projection, scheduler chunks and cache
  hits. Render identity includes the new source and algorithm revisions.
- Candidate v4 exports the distinct kind and plosive revision, validates its
  recipe-bound duration and spectrum, and enters producer import/recovery as
  MarkerReview. No approval or rights decision is synthesized.

## Verification

The full Release build passed. Four focused CTest suites passed in 26.00 seconds:
voice design, performance snapshot, export and aggregate core. This is not a new
full 120-suite run and is not a release or host-matrix qualification.

New and extended checks cover:

- p/t/k plan admission at 8, 48 and 96 kHz; k closure/burst/vowel output;
- silent closure, nonzero finite burst and vowel, seeking inside the burst,
  reset/checkpoint equality and cancellation without advancing state;
- oversized burst rejection, conflicting bindings and stale frozen-recipe rejection;
- scheduler chunk boundaries inside the burst, exact full/chunk PCM and cache markers;
- actual Japanese ka render, Float32 bake, candidate-v4 load, producer import and
  recovery, with exact rendered-versus-baked PCM;
- missing/invalid source kind, schema downgrade, invalid plosive revision and a
  span with no closure room rejected by the candidate decoder.

Evidence is retained under `evidence/plosive-articulation-2026-09-09/`.
The retained ka engineering fixture is 24,000 frames at 48 kHz (0.5 seconds):
closure [0,2400), burst [2400,2880), vowel [2880,24000). Its WAV SHA-256 is
`7c792d525758e47dd86337f337ad215cbdc26d0a7464d6a7fad8ec0e1c070310`.
These spectra are test parameters, not an independently recognized or musically
approved female voicebank.

## Remaining scope and preservation

Native plosive source controls, richer articulation (including voiced stops,
affricates and geminates), coarticulation and independent listening qualification
remain unfinished. The complete Full-Scope Beta GO plan remains mandatory.
No new whole roadmap unit is accepted by this increment.

Hash comparison against `session-preservation-m8yx7I` found no missing captured
files. Existing dirty work was retained; no staging, commit or push occurred.
This establishes continuity from that checkpoint, not proof against historical
session loss. See [candidate v4](../formats/PROCEDURAL_CANDIDATE_V4.md).
