# Pilot 01 declared coverage against the pilot recipe

Engineering measurement, not a musical one. It records which of the pilot
inventory's own declared classes the pilot recipe can actually prepare, and why the
rest cannot. No listener heard anything, and nothing here is a release decision.
The classes the report calls prepared were also rendered and collected through the
ordinary campaign path; that evidence is recorded in
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

This measurement's recipe is schema ten (`b066403c...`) against the pilot producer
`9ac6387d...`.

## Result

| Measure | Value |
|---|---|
| Assignments inspected | 1026 |
| Prepared (snapshot compiled) | **978** |
| Refused | **48** |
| Declared coverage keys (style x key) | 342 |
| Keys with a prepared class | 326 |
| Phones declared | 41 |
| Phones covered by a prepared class | 36 |
| Coverage kinds declared | 8 |
| Kinds with any prepared class | 5 (cv, vc, vv, sustain, special) |
| Kinds entirely refused | 3 (release, glottal-attack, breath) |

Prepared phones: `N a b by ch d e f fy g gy h hy i j k ky m my n ny o p py r ry s sh t ts u v vy w y z`.
Missing phones: `R br cl glottal pau`.

Prepared assignments, by kind: cv 450, vc 450, vv 60, sustain 15, special 3.

## Why the 48 refusals happen

| Refusals | Cause | Nature |
|---|---|---|
| 42 | `Inventory phone sequence is not supported by the Japanese score adapter` for `R`, `glottal` and the `br` sequences | Adapter gap |
| 6 | `has no explicit frication or released-stop source` for `pau` and `cl` | Inventory question: a pause and a closure are events, not recorded units |

## What changed since the first measurement

The first measurement of this report refused 528 assignments. Four repairs closed 480 of
them, and each kept the phone's identity rather than rewriting the syllable:

1. **Explicit hint roles (210).** A consonant written after a vowel in a hint became a coda
   with its own resolved start instead of an onset with nothing to attach to.
2. **Frication bindings (150).** `sh`, `h` and `f` are declared unvoiced frication, and `z` and
   `v` are declared voiced frication with the same-phone resonance pose the voiced source
   requires. `fy` also became voiceless, which it always was: the phonemizer listed `hy` as
   voiceless and had left `fy` out of that list.
3. **Palatalized consonants (300).** `ky gy hy py by my ny ry fy vy` are declared as a base
   consonant's release carried through the palatal pose named after the palatalized phone
   itself, so a bank has a `ky` unit rather than a relabelled `k`.
4. **The voiced affricate (30).** `j` is a prevoiced closure, its burst and a voiced frication
   tail in one gesture. Its closure carries the excitation the score supplies, which is what an
   unvoiced affricate's closure does not do, and the tail adds noise on top of that voicing.

## Consequences for the next steps

1. 48 of 1026 assignments still cannot prepare, and none of them needs a repaired compiler: the
   two adapter symbols with the breath sequence (`R`, `glottal`, `br`) and the closure series (`pau`,
   `cl`), which the inventory treats as units even though a pause and a closure are events.
2. A campaign over the whole inventory still cannot be planned, so the rendered preflight runs
   over a renderable subset. The palatalized and voiced-affricate subsets used for that evidence
   are retained under `build/pilot-01/palatalized-{ws,campaign}` and
   `build/pilot-01/voiced-affricate-{ws,campaign}`.
3. Every prepared class is declared source parameters and spectra, not phonetic qualification.
   Being able to prepare a `j` unit says nothing about whether it sounds like a singer.
