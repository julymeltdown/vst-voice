# Pilot 01 declared coverage against the pilot recipe

Engineering measurement, not a musical one. It records which of the pilot
inventory's own declared classes the pilot recipe can actually prepare, and why the
rest cannot. No listener heard anything, and nothing here is a release decision. The
ten palatalized classes were rendered and collected afterwards through the ordinary
campaign path; that run is recorded in
`docs/implementation/INTEGRATED_SINGER_EXECUTION.md`.

## Reproduce

```sh
python3 tools/voicebank-script-generator/main.py --draft \
  --profile assets/pilots/seam-pilot-01/profile.json --json-output /tmp/pilot-inventory.json
python3 -m tools.external_beta.voicebank_production prepare-draft \
  --inventory /tmp/pilot-inventory.json --project-id seam-pilot-01 \
  --operator-id producer --output /tmp/pilot-definition.json
build/release/seam_voicebank_cli init-production /tmp/pilot-ws /tmp/pilot-definition.json \
  DEFINITION_SHA256 producer 2026-09-14T00:00:00Z
# The pilot's own maximal recipe: custom phrase mode adds every class the pilot has.
build/release/seam_singer_pilot /tmp/pilot-max phrase 'あ:60'
build/release/seam_voicebank_cli inspect-generation-coverage \
  /tmp/pilot-ws /tmp/pilot-max/baseline-recipe.json assets/pilots/seam-pilot-01/coverage-report.json
```

This measurement's recipe is schema nine (`3a2b5d61...`) against the pilot producer
`9ac6387d...`.

## Result

| Measure | Value |
|---|---|
| Assignments inspected | 1026 |
| Prepared (snapshot compiled) | **948** |
| Refused | **78** |
| Declared coverage keys (style x key) | 342 |
| Keys with a prepared class | 316 |
| Phones declared | 41 |
| Phones covered by a prepared class | 35 |
| Coverage kinds declared | 8 |
| Kinds with any prepared class | 5 (cv, vc, vv, sustain, special) |
| Kinds entirely refused | 3 (release, glottal-attack, breath) |

Prepared phones: `N a b by ch d e f fy g gy h hy i k ky m my n ny o p py r ry s sh t ts u v vy w y z`.
Missing phones: `R br cl glottal j pau`.

Prepared assignments, by kind: cv 435, vc 435, vv 60, sustain 15, special 3.

## Why the 78 refusals happen

| Refusals | Cause | Nature |
|---|---|---|
| 30 | `requires a supported voiced articulation model` for `j` | Missing model: a voiced affricate needs a prevoiced closure and voiced frication, and this build refuses rather than substituting an unvoiced pair |
| 42 | `Inventory phone sequence is not supported by the Japanese score adapter` for `R`, `glottal` and the `br` sequences | Adapter gap |
| 6 | `has no explicit frication or released-stop source` for `pau` and `cl` | Inventory question: a pause and a closure are events, not recorded units |

## What changed since the first measurement

The first measurement of this report refused 528 assignments. Three repairs closed 450 of
them, and each kept the phone's identity rather than rewriting the syllable:

1. **Explicit hint roles (210).** A consonant written after a vowel in a hint became a coda
   with its own resolved start instead of an onset with nothing to attach to.
2. **Frication bindings (150).** `sh`, `h` and `f` are declared unvoiced frication, and `z` and
   `v` are declared voiced frication with the same-phone resonance pose the voiced source
   requires. `fy` also became voiceless, which it always was: the phonemizer listed `hy` as
   voiceless and had left `fy` out of that list.
3. **Palatalized consonants (300).** `ky gy hy py by my ny ry fy vy` are declared in recipe
   schema nine as a base consonant's release carried through the palatal pose named after the
   palatalized phone itself: `ky` takes its release from `k` and its colour from the `ky` pose, so a
   bank has a `ky` unit rather than a relabelled `k`.

## Consequences for the next steps

1. 78 of 1026 assignments still cannot prepare, and none of them needs a repaired compiler:
   a voiced affricate source (`j`), two adapter symbols (`R`, `glottal`) and the closure series
   (`br`, `pau`, `cl`).
2. A campaign over the whole inventory still cannot be planned, so the rendered preflight runs
   over a renderable subset; the palatalized subset used for this evidence is retained under
   `build/pilot-01/palatalized-campaign` with its workspace beside it.
3. Every prepared class is declared source parameters and spectra, not phonetic qualification.
   Being able to prepare a `ky` unit says nothing about whether it sounds like a singer.
