# Voice recipe v2 — frication source identity

Recipes with explicit nasal resonance/antiresonance models use [schema 3](VOICE_RECIPE_V3.md),
including when they also contain frication poses. Schema-2 identity remains unchanged.

Schema 2 extends `com.project-seam.voice-recipe` with a nonempty `frications` array. Schema-1 fields retain their meaning. Recipes without frication still serialize as schema 1 with their original canonical bytes; schema-2 input with an empty array rejects rather than silently downgrading.

Each frication pose has exactly `phone`, `style`, `seed`, `centerHz`, `bandwidthHz` and `gain`. Phone/style text is bounded and unique as a pair; the style must also occur among vowel poses. Seed is a canonical decimal unsigned-64-bit string. Center is finite 80–16,000 Hz, bandwidth 20–16,000 Hz, center/bandwidth Q is 0.25–20, and gain is 0–0.25 linear amplitude. At most 64 poses are permitted. Existing byte/node/depth/string limits remain active; unknown/missing/duplicated fields reject. Actual-rate Nyquist restrictions are validated by the DSP source, without silent retuning.

## Resource binding

Frication-bearing recipes freeze with resource version `2`; vowel-only recipes retain `1`. Canonical JSON hashing covers all seeds, spectral settings, gains, phones and styles. Decode verifies version/content agreement along with byte hash and recipe ID. `ArticulatedStream` requires every frication gesture to match its frozen recipe's phone/style/configuration exactly.

Schema-1-only readers cannot consume schema 2, and there is no lossy implicit conversion. Deliberately removing all frication poses returns to schema 1. Neither version grants approval or rights, nor establishes that a phone label is intelligible. Production snapshot adoption, mixed-gesture marker/bake formats, native frication controls and listening qualification remain open.

## Verification

A pre-change generated v1 fixture hash (`37db127d94902b90880aa9643a4eefb2d816c309564b193650a42c47b830f171`) verifies byte/identity preservation. Tests also cover full-width seeds, v2 save/reload, changed-setting hash invalidation, version mismatch, empty/duplicate/missing fields, invalid styles/seeds and recipe/plan mismatch. Strict Debug voice-design build and all 11 cases pass (2.32 seconds); strict Release design/export builds and 11 design plus 18 integration cases pass (2.12 seconds total). The old resource-version guard initially rejected v2 and was corrected explicitly. `git diff --check` passes.
