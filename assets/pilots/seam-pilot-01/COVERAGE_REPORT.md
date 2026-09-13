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
| Prepared (snapshot compiled) | **498** |
| Refused | **528** |
| Declared coverage keys (style x key) | 342 |
| Keys with a prepared class | 166 |
| Phones declared | 41 |
| Phones covered by a prepared class | 20 |
| Coverage kinds declared | 8 |
| Kinds with any prepared class | 5 (cv, vc, vv, sustain, special) |
| Kinds entirely refused | 3 (release, glottal-attack, breath) |

Prepared phones: `N a b ch d e g i k m n o p r s t ts u w y`.
Missing phones: `R br by cl f fy glottal gy h hy j ky my ny pau py ry sh v vy z`.
Prepared keys, by kind: cv 210, vc 210, vv 60, sustain 15, special 3.

## Why the 528 refusals happen

| Refusals | Cause | Nature |
|---|---|---|
| 300 | `requires a supported voiced articulation model` for z, j, v, gy, ny, by, my, ry, fy, vy | Missing model |
| 186 | `has no explicit frication or released-stop source` for sh, h, f, ky, hy, py, and the closure/pause events | Missing model |
| 42 | `Inventory phone sequence is not supported by the Japanese score adapter` for `R` (release) and `glottal` | Adapter gap |

Every refusal is now a missing model or an unresolvable symbol. That was not true of the
first measurement of this report, which recorded 210 vowel-to-coda assignments refused
with "requires a resolved start and associated nucleus" even though those phones had
models and prepared as onsets. The cause was the explicit phone hint, not the gesture
model: a hint is a bare sequence of symbols and every ordinary consonant's role was
inferred from its symbol alone, so a consonant written after the vowel became an "onset"
with nothing to attach to. Hints now give an ordinary consonant its place in the
syllable, which is what turned those 210 assignments into prepared vowel-to-coda units.

## Consequences for the next steps

1. Just over half the inventory (498 of 1026 assignments) still cannot prepare, entirely
   for absent models and two unresolvable symbols. Those need new source models, not a
   repaired compiler.
2. A campaign over the whole inventory still cannot be planned, so the rendered preflight
   must run over a renderable subset: the 166 prepared coverage keys, or a
   coverage-complete selection across the five kinds that prepare.
3. The classes that prepare now include the vowel-to-coda units the bank was most
   obviously missing, and each renders as two ordered gestures, the vowel and then the
   coda, rather than as one stretched gesture.
