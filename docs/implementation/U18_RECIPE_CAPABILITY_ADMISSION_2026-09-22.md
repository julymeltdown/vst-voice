# U18 recipe source-capability admission repair

Date: 2026-09-22. Baseline: `8820bc4343ba735fab1bb0a9abb772103d15021e`.
This closes the reproduced admission defect and strengthens recipe persistence
evidence. It does not complete U18, qualify a singer, or advance Beta GO.

## Defect and production change

`decodeVoiceRecipeResource` formerly associated source opt-outs with schemas 5
and 6. Schemas 7–11 were admitted unconditionally even when they retained the
restricted older sources. A pure voiced-stop schema-6 recipe also unnecessarily
required the voiced-frication flag.

Schema admission remains exactly 1–11. After bounded decoding and identity
agreement, the reader now inspects actual bindings independently of version:

- A frication with explicit `voicingGain` requires voiced-frication permission.
- A plosive with explicit `voicedClosure` requires voiced-stop permission.
- A voiced affricate requires both: prevoiced closure and voiced-frication tail.
- Palatalized bindings inherit a base that validation requires inside the same
  recipe, so the base scan enforces its permission too.
- Restrictions cover the whole resource, including unselected styles and
  unscored sources. Ordinary vowel/nasal/approximant phonation requires neither.

The reader returns `Unsupported` with the disabled source family, rather than
dropping a source or reinterpreting it. Planner and both stream entrypoints already
call this reader, so they share its boundary. The public header documents scope.
No canonical encoding, schema identity, default permission, DSP algorithm or
renderer revision changes. Restricted-call admission intentionally changes.

## Tests and failure evidence

The earlier dirty regression was preserved and extended, not removed.
`build/release/Testing/recipe-capability-matrix-red.log` records 40 passing and
three failing cases before the production fix: later-schema bypass, independent
flag matrix (first mismatch: pure schema 6 with frication disabled), and entrypoint
parity. The new eleven-schema persistence control passed before the repair.

New coverage checks all schemas 1–11 and four permission combinations, with
appropriate retained older source families; five restricted source constructions
through decoder/planner/direct stream/recipe stream, including schema-11 variants;
positive non-silent output and exact admissible-path PCM equality; and file
roundtrips with maximum uint64 seed, extra style, modulation, unchanged reads,
immutable frozen resources and stale expected-identity rejection after edits.
Additional controls render ordinary vowels/nasals/approximants with both flags
disabled and reject a restricted source present only in an unused style.

One added test initially failed strict compilation because its phoneme ordinal
cast used uint32 rather than the declared uint16. The test-only cast was corrected
(the fixture has at most one preceding phone); no warning was disabled.

## Verification

Fresh strict builds followed by:

| Configuration | Tests | Result |
|---|---|---|
| Release | voice design 44, Designer 38, final completeness 8, original singer song 4, formants 17 | Five CTest targets PASS, 22.93 seconds |
| Debug | voice design 44, Designer 38 | Two CTest targets PASS, 26.63 seconds |

Logs: `build/{release,debug}/Testing/recipe-capability-final.log`; detailed case
output is in each build's `Testing/Temporary/LastTest.log` at this checkpoint.
Existing duplicate-library linker warnings remain. `git diff --check` passed.
The full CTest inventory and installed/native UI journeys were not rerun here.
Tracked-source closure will be checked at integration; parallel P2 work is kept
outside this commit.

The persistence matrix exercises file/resource APIs, not eleven-schema native
Designer UI journeys. The existing Designer suite separately covers save/reopen,
undo/edit ownership, external conflict and installed-resource protection. Neither
implies completion of every U18 requirement or its U9 dependency.

## Review and next integration

A bounded subagent source review found no blocking defect; its two suggested
resource-wide/ordinary-phonation controls were subsequently added and passed.
Review by the existing independent reviewer task is pending at this checkpoint.

The full objective remains active. Next work is safe native interchange review:
real AppKit wiring, stale-document protection, complete bounded loss accounting,
and then embedded-editor/actual external exchange acceptance. These do not wait
for unresolved singer-quality or Windows evidence.
