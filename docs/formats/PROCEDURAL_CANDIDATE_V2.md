# Procedural candidate v2 — planned articulated gestures

Nasal-consonant gestures use [candidate v3](PROCEDURAL_CANDIDATE_V3.md). V2 remains
the vowel/frication-only contract and rejects nasal markers.

Status: implemented for explicit recipe-bound oral-vowel/frication baking and strict producer ingestion. Not acoustic alignment, source qualification or approval.

## Version selection and semantics

Vowel-only baking continues to emit candidate v1, with its unchanged four-field markers and `planned-vowel-gestures` semantics. A phrase rendered by the articulated backend emits v2 and `planned-articulated-gestures`. V2 requires both an oral-vowel and a frication gesture; it is not an alternate encoding for vowel-only material. Unsupported versions, mixed semantics, missing/unknown fields and unknown gesture kinds reject.

Both versions use `formatId: com.project-seam.procedural-candidate` and `approval: unapproved`. The same Final phrase pipeline produces candidate PCM; baking is mono Float32, region-local without leading song silence or track gain/pan. Gesture bounds describe intended source activity, not measured phoneme boundaries, intelligibility or reviewed coverage. A frication label specifies a recipe source request, not proof that listeners recognize that consonant.

## Root fields

V2 has exactly 20 fields:

- `formatId`, `schemaVersion` (2), `approval`, `markerSemantics` as above.
- `audioSha256`, `renderContentHash`, `renderAbi`.
- `sampleRate`, `frameCount`, `scoreOriginFrame`.
- `recipeId`, `recipeVersion`, `recipeHash`, `style`.
- `proceduralRevision`: `ArticulatedStream` algorithm revision, not the vowel renderer revision.
- `compilerRevision`: shared score-performance compiler revision.
- `articulationPlanRevision`, `fricationRevision`, `fricationStreamRevision`: the plan, aperiodic source and scheduled frication lane revisions.
- `markers`.

All revision fields are positive uint32 integers. They record generating algorithms; their presence is not a signature or evidence of successful reproduction. Recipe ID/version/hash must match the verified supplied resource exactly. The render content hash also binds recipe, project/pronunciation, output window, style, rate and relevant DSP/compiler identity through the snapshot factory.

## Markers

Each marker has exactly `key`, `phone`, `startFrame`, `endFrame`, and `kind`. Kind is exactly `oral-vowel` or `frication`.

- Keys use the canonical nonzero 16-lowercase-hex note ID followed by `:` and canonical decimal ordinal below 16,384. Duplicate keys reject.
- Frames are integer, candidate-relative, half-open ranges. They must be positive-length, ordered, nonoverlapping and within the WAV frame count. Gaps are allowed. Exported full-candidate markers are never clipped.
- Oral vowels are a/i/u/e/o with a matching recipe pose in the selected style.
- Frication requires an exact phone/style binding in the frozen recipe. Its source configuration must support the candidate sample rate. Settings come from that hashed recipe, not independent metadata values.
- V2 cannot silently drop its kind field or downgrade a frication marker to v1.

Metadata parsing does not verify that the audio sounds like a named phone. It also does not reconstruct the original score's nucleus relationships from marker labels. Production snapshot preparation validates those relationships before generation; downstream review remains necessary.

## Bounds and producer lifecycle

Existing v1 resource limits remain: 4 MiB metadata, depth 4, 100,000 JSON nodes, 256-byte strings, at most 16,384 collection entries; rates 8–384 kHz; 1–32 Mi frames; nonnegative score origin with origin plus frames at most 2^52. Audio ingestion checks owned bounded bytes, SHA-256, one unambiguous IEEE Float32 format, mono dimensions/rate and finite samples. Cancellation produces no approved or partially published take.

The existing producer import transaction retains the exact metadata and canonical recipe as immutable procedural lineage, under interprocess writer locking and expected-generation checks. Takes enter MarkerReview; neither marker nor pitch review is inferred. Recovery reparses the same strict format. Manual boundary edits remain separate metadata revisions; they preserve gesture kinds and the original candidate metadata. This format adds no source-strategy, licensing, review or installation bypass.

## Verification boundary

The integration fixture bakes explicit `s`/`a` source gestures, loads the exact WAV, checks typed bounds and revisions, imports through the canonical producer repository, recovers the same markers, and edits the onset through the Studio controller while preserving immutable lineage and unreviewed state. Malformed versions/semantics/kinds/bindings/styles/revisions and incomplete mixed inventory reject. V1 regression tests remain in the same export suite. Synthetic fixture source approval flags are test scaffolding only, not qualified production-source evidence.
