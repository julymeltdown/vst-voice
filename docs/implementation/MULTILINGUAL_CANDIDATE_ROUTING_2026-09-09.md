# English/Korean procedural candidate routing repair

## Root cause and repair

While inspecting the next consonant-articulation work, two Japanese-only vowel
assumptions were found in an already exposed cross-language workflow:

1. `requiresArticulation` treated every symbol outside `a/i/u/e/o` as requiring
   the articulated renderer, even when the shared phonemizer classified it as
   a voiced nucleus. An English `ah1` could render through that path but be
   exported as a mixed-gesture candidate with no frication, which the loader
   correctly rejected as inconsistent with candidate-v2 semantics.
2. Candidate loading independently used the same five-symbol whitelist, rejecting
   valid English/Korean vowel markers even with an exact matching recipe pose.

Both decisions now use `phonemizer::isVowelSymbol`, the vocabulary already used
by score and articulation compilation. Vowel-only English/Korean phrases use
the sustained path and candidate v1; actual mixed frication phrases retain v2.
The loader still requires an exact recipe phone/style pose and now validates its
tract at the actual sample rate, including Nyquist and explicit nasal-model
requirements. Each distinct pose is checked once per candidate, not once per
repeated marker. No arbitrary consonant is accepted as a vowel.

Sustained renderer revision is 8 so the existing snapshot identity invalidates
audio cached under the old routing. Recipe schema/resource identities are not
renumbered; they are separate from renderer and candidate-format versions.

## Reproduction and verification

The new actual render -> bake -> load -> producer-import regression covers:

- English stress-marked `ah1`, with explicit English pronunciation input.
- Korean `어` -> `eo`, resolved by the Korean phonemizer.
- Korean `으` -> `eu`, resolved by the Korean phonemizer.

Each uses a schema-3 nasal recipe, asserts correct vowel routing/candidate v1,
compares loaded Float32 PCM exactly to the Final phrase render, and imports the
candidate through the canonical producer repository. The take must remain
MarkerReview with no review records. Negative cases reject a consonant marker
and a hash-bound recipe whose formants are invalid at the candidate sample rate.

The regression failed after successful rendering/baking before the repair.
Fixing the loader alone exposed the routing problem; both were repaired together.
The source tests retain descriptive loader errors for future diagnosis.

## Scope and preservation

Full Release build passed. Final focused CTest: **5/5 PASS, 31.91 seconds**;
voice design 16/16, snapshot 43/43, export 24/24, core 841/841, CLI workflow 4/4.
`git diff --check` passed. This was not a new full 120-entry run; the preceding
full run and its source-closure failure remain historical evidence.

Snapshot comparison against `session-preservation-4ky6MH` found no missing
captured files. Existing paths changed only for this repair and its format notes.

This fixes reusable-resource production for these supported vowels, not complete
English/Korean singing or pronunciation quality. No additional phoneme inventory,
language/style assignment migration, nasal consonant, closure/burst synthesis,
source approval or Beta GO is claimed. The intended nasal-consonant work remains
open; this upstream workflow defect was addressed first.

Existing dirty work is preserved. No staging, commits, pushes or real source/music
approvals were performed. Verification logs accompany this report under
`evidence/multilingual-candidate-routing-2026-09-09/`.
