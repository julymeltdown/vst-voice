# English producer inventory to editable sample manifest

Normal English producer folders now carry their declared vocabulary and stress
policy into editable sample manifests. Previously the English inventory was
explicit, but `createSampleManifestDraft` discarded that authority and emitted
an exact-matching bank without its contract. A folded inventory's bare `iy`
could therefore not serve dictionary `iy1` through the normal creation path.

## Code and authority

`libs/seam-voicebank-production/src/manifest_draft.cpp` loads the generated
`inventory.json` beside `producer/` while holding the existing producer writer
lock. For style-owned English projects it requires:

- the normal generated inventory/producer layout and a regular inventory file;
- exact inventory regeneration through the existing bounded loader;
- matching project inventory ID, digest and language, plus producer-root identity;
- assignment style/coverage/layer keys that belong to that inventory.

The validated profile's three contract fields populate
`Manifest::languageContract`. The schema-4 codec and matching path introduced in
`d5ecc1f9` preserve them. The inventory is checked again before publication.
Missing, linked, tampered or substituted authority is refused before the new
draft commits. These checks do not assert atomicity against uncooperative file
writes after the final check; the manifest itself is a frozen, project-bound
policy snapshot. Legacy projects without policy authority retain exact matching.
Manual style-owned English layouts must be recreated through the generated
workspace path; this API does not invent a stress policy for them.

CLI `create-sample-draft` and Studio's asynchronous draft worker already call
this shared function, so both use the same authority checks. No new CLI flag,
project schema, source mapping or approval operation is introduced. Missing
assignments stay missing, and marker/pitch estimates remain unreviewed.

The source project's encoded hash already binds its inventory digest; the
manifest hash binds the copied contract. Existing draft descriptor and output
integrity checks remain in force. Japanese/Korean and legacy matching behavior
remain covered by the existing focused lanes.

## Verification

Full dependent Release rebuilds precede checks. Effective focused coverage is
**142 native cases across six CTest lanes**, collected across retained runs:
production draft (23), production review (24), Studio sample review (14),
manifest draft (15), Studio manifest draft (38), actual sample-review CLI (28).
The first three lanes passed initially and stayed source/binary unchanged.
Only affected lanes were rerun after test-only repairs.

Five new cases exercise generated English workspaces, folding and explicit
stress distinction, exact audio-copy identity, missing/tampered/substituted/linked
inventory, an assignment outside its inventory, replacement before commit,
actual CLI creation and refusal, and Studio creation/save/reopen. Synthetic
audio is mono PCM16 at 16 kHz; only one of two planned layers is collected.
Production source qualification, reviews and release eligibility stay absent.

The initial shared fixture violated both generator constraints: a single pitch
layer and a non-increasing requested range. The first test repair corrected only
the range; the second corrected the two-layer requirement and exposed creation
errors directly. The remaining CLI assertion expected exit 1 although the
unchanged command's operational refusal is exit 2; its exact oracle was corrected.
All failed logs, the two earlier fixture versions, the pre-exit-fix CLI test and
fresh-binary timestamps are retained. No production code was changed to satisfy
those failures. Duplicate CTest failure output is not a separate execution.

The [evidence index](ENGLISH_PRODUCER_MANIFEST_2026-10-15.evidence.json) binds six
changed code/test files and seven executing binary hashes. Four final executables
are retained; earlier failed test executables were overwritten. Temporary
synthetic workspace/draft output is cleaned up by the new fixture and is not a
retained production packet. Evidence is focused local consistency, not an
authenticated/hermetic build, full suite or physical acceptance.

## Correction and remaining work

The previous bank-core report incorrectly listed sample-rate handling as a new
implementation gap. It already existed: draft creation sets the rate from the
first decoded asset and rejects mixed rates. The new 16 kHz cases confirm that
behavior; the 48 kHz initializer is only a pre-inspection placeholder. That
report is corrected without changing the audio-rate implementation.

This closes normal English producer-to-editable-manifest policy propagation.
It does not admit the 1,429 original female WAVs or 312 derived SLT speech donors,
approve an alias, manufacture missing `axr`, or qualify singing/pitch/style
coverage. Owner policy, real producer and distinct reviewer, corrected source
attribution review, listening, classical/neural and multilingual resources, AE1/R3
original singer, R9, acoustic tolerances, full candidate checks and physical/DAW
evidence remain open. Exact independent review of this unit is pending.
Usable Alpha remains 0/20 recorded passes, U45 held, External Beta BLOCKED.
