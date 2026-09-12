# Voice recipe v1 — initial draft contract

Explicit nasal resonance/antiresonance models now use [schema 3](VOICE_RECIPE_V3.md).
This document's historical coupling-only limitation still applies to schema 1.

Frication-bearing recipes use [schema 2](VOICE_RECIPE_V2.md). Vowel-only schema-1 serialization and resource identity are preserved; schema-1 readers do not receive silently stripped frication settings.

`com.project-seam.voice-recipe`, schemaVersion integer `1`, is declarative design-time data. The codec recognizes the reserved engine contract `seam.source-filter.v1`; this is not proof that its DSP backend is implemented or qualified. A recipe grants no script execution, source permissions, approval, or installed-bank mutation.

The root has exactly eight fields: `formatId`, `schemaVersion`, `id`, `engineId`, `seed`, `phonation`, `modulation`, `poses`. Unknown/missing fields and unsupported schemas/engines reject. IDs and phone/style labels are nonempty strict UTF-8, at most 128 bytes, without ASCII control characters.

`seed` is a canonical unsigned decimal **string**, from `0` through `18446744073709551615`. JSON numbers, signs, overflow and leading zeroes are rejected, preserving all 64 bits across JSON implementations.

| Object/field | Unit and initial implementation bounds |
| --- | --- |
| phonation.openQuotient | 0.05–0.95 |
| phonation.spectralTiltDbPerOctave | -48–0 dB/octave |
| phonation.aspiration | normalized 0–1 |
| modulation.jitterCents | 0–100 cents |
| modulation.shimmerAmount | normalized 0–1 |
| modulation.rateHz | 0–20 Hz; zero denotes disabled modulation |
| poses | 1–64 unique `(phone, style)` pairs |
| pose.nasalCoupling | normalized 0–1 |
| pose.formants | 3–8 frequency-ordered bands |
| band.frequencyHz | 50–16,000 Hz, strictly increasing within a pose |
| band.bandwidthHz | 10–5,000 Hz |
| band.gainDb | -48–24 dB |

Each pose contains exactly `phone`, `style`, `nasalCoupling`, `formants`; each band contains exactly the three fields above. Phonation and modulation contain exactly their three named fields. Every numeric control must be finite. These are bounded draft-contract ranges, not physiological or acoustic-quality guarantees. Rendering must additionally validate the actual sample rate, filter stability and supported phoneme semantics; the codec does not clamp out-of-band filters or invent consonant articulation.

Parsing is limited to 512 KiB, depth 6, 8,192 nodes, 128-byte strings and 64 entries per collection. Encoding is deterministic under the existing JSON writer and validates the recipe first.

All v1 fields are design-time recipe controls. Changes require regeneration of derived candidate material; they are not advertised as implemented runtime expression controls. Score F0 is intentionally absent and must come from the shared performance compiler. Editing this independent object does not edit a song or publish a bank.

The initial oral-resonance DSP interprets bandwidthHz through nominal Q = frequencyHz / bandwidthHz; measured digital bandwidth is not guaranteed to equal this nominal value. Band gainDb values are relative amplitudes normalized across the parallel resonance bank, not an overall output-gain control. Its initial implementation rejects nonzero nasalCoupling rather than ignoring it; nasal data may still be stored in a draft for later supported rendering.

Still required: resource/recipe lineage integration, actual save/reopen application workflow, explicit UI capabilities, complete nasal/interpolated tract behavior, articulation/bake adapters and acoustic acceptance. Initial phonation/oral-tract DSP and internal sustained-vowel preview now exist; see the U19 status record. Test poses and seeds are diagnostic fixtures, not an original female voicebank.

## Frozen procedural resource binding

`freezeVoiceRecipeResource` validates and canonically encodes a draft recipe, then deep-freezes its JSON bytes in a ProceduralSingerResource. Resource ID equals recipe ID, resource version is `1` (this format contract, not an installed-bank version), and contentHash is the SHA-256 of those exact bytes. Draft edits produce a new hash and do not mutate prior frozen data.

`decodeVoiceRecipeResource` verifies typed identity/payload binding, requires resource version `1`, decodes the bounded strict recipe schema, and checks that the embedded recipe ID matches the resource ID. A hash-valid opaque blob is not sufficient. Both entry points support cancellation. The phrase pipeline now has explicit guarded procedural sustained-vowel dispatch; it never treats a procedural resource as a Raw sample bank. Package approval, provenance permission and acoustic qualification do not follow from successful decoding/rendering.

## Local recipe file persistence

`saveVoiceRecipeFile(path, recipe, options, stopToken)` validates and encodes before disk mutation, then uses the shared durable atomic writer. Cancellation is checked before entering the write transaction; no cancellation result is manufactured after a successful commit. Atomic-write errors after replacement retain the shared writer's error semantics; not every possible I/O failure implies the old file remains. Optional backup and fault-injection behavior comes from `AtomicWriteOptions`.

`loadVoiceRecipeResource(path, expectedIdentity, stopToken)` reads at most 512 KiB, strictly decodes v1 and freezes canonical recipe bytes. An optional expected identity must match kind, ID, version and hash exactly. Whitespace-only JSON changes do not change this semantic resource identity. Loading never rewrites or upgrades source files. The shared file reader/writer rejects leaf symlink/non-regular targets; these APIs are for explicitly selected local files, not a hostile-directory sandbox.

File persistence is implemented independently of native project selection, relative-path relinking and recipe lineage. The six voice-design cases pass after strict Debug/Release builds (1.59/0.61 seconds), including save/load identity, preserved seed, immutable old resources, invalid/cancelled/pre-replacement-failed saves, oversized/future input and leaf symlink rejection.
