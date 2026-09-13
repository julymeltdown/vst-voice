# Pilot 01 declared coverage against the pilot recipe

Engineering measurement, not a musical one. It records which of the pilot
inventory's own declared classes the pilot recipe can actually prepare, and why the
rest cannot. No audio was rendered, no listener heard anything, and nothing here is
a release decision.

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

## Result

| Measure | Value |
|---|---|
| Assignments inspected | 1026 |
| Prepared (snapshot compiled) | **288** |
| Refused | **738** |
| Declared coverage keys (style x key) | 342 |
| Keys with a prepared class | 96 |
| Phones declared | 41 |
| Phones covered by a prepared class | 20 |
| Coverage kinds declared | 8 |
| Kinds with any prepared class | 4 (cv, vv, sustain, special) |
| Kinds entirely refused | 4 (vc, release, glottal-attack, breath) |

Prepared phones: `N a b ch d e g i k m n o p r s t ts u w y`.
Missing phones: `R br by cl f fy glottal gy h hy j ky my ny pau py ry sh v vy z`.
Prepared keys, by kind: cv 210, vv 60, sustain 15, special 3.

## Why the 738 refusals happen

| Refusals | Cause | Nature |
|---|---|---|
| 300 | `requires a supported voiced articulation model` for z, j, v, gy, ny, by, my, ry, fy, vy | Missing model |
| 186 | `has no explicit frication or released-stop source` for sh, h, f, ky, hy, py, and the closure/pause events | Missing model |
| 210 | `requires a resolved start and associated nucleus` / `needs a resolved start and a same-note vowel nucleus` | **Structural** |
| 42 | `Inventory phone sequence is not supported by the Japanese score adapter` for `R` (release) and `glottal` | Adapter gap |

The third row is the important one. Those 210 refusals are not missing sounds: they
are vowel-to-coda placements (`vc`) of phones whose models already exist and prepare
happily as onsets (`t k p b d g s ch ts n m r w y`). The articulation compiler refuses
them because a gesture after the nucleus cannot resolve its own start and associated
nucleus in the current model. Every one of the 450 `vc` assignments is refused, and
so is every `release`, `glottal-attack` and `breath` assignment.

So the pilot inventory is not generatable end to end for two independent reasons, and
only one of them is "the recipe lacks a pose". The larger near-term blocker is the
coda/context model, which the implementation plan already lists as M1.P2 items 2 and 3
(phrase context beyond the owning note; ordered spans separated from a bounded
transition plan).

## Consequences for the next steps

1. Any campaign over this inventory cannot be planned as a whole: the plan refuses at
   the first class that cannot prepare, and 72% of the assignments cannot.
2. A held-out preflight of the whole inventory is therefore impossible today. The
   coverage report is the retained defect list until the coda/context work lands.
3. The singable subset today is onset-consonant syllables over the 20 prepared phones,
   vowel-to-vowel and sustain units, and the syllabic nasal. Coda, release,
   glottal-attack and breath coverage all wait on the same structural repair.

