# Pilot 01 declared coverage against the pilot recipe

Engineering measurement, not a musical one. It records which of the pilot
inventory's own declared classes the pilot recipe can actually prepare, and why any of
them cannot. No listener heard anything, and nothing here is a release decision.
Every prepared class is also exercised by the campaign's rendered held-out preflight,
which is the gate that says a class can be produced at all; that evidence is recorded in
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

The campaign claim below needs one more step, because a campaign imports what it renders and an
import requires an authorized source:

```sh
PROJECT_SHA256=$(shasum -a 256 /tmp/pilot-ws/project.json | cut -d' ' -f1)
build/release/seam_voicebank_cli register-source /tmp/pilot-ws $PROJECT_SHA256 pilot-procedural \
  procedural pass yes yes no no assets/pilots/seam-pilot-01/source-declaration.txt \
  LICENSE_SHA256 producer 2026-09-14T00:00:00Z
TAKE_IDS=$(python3 -c "import json;print(' '.join(e['takeId'] for e in json.load(open('assets/pilots/seam-pilot-01/coverage-report.json'))['entries']))")
build/release/seam_voicebank_cli draft-generation-campaign /tmp/pilot-ws \
  /tmp/pilot-max/baseline-recipe.json /tmp/pilot-plan.json $TAKE_IDS
PLAN_SHA256=$(shasum -a 256 /tmp/pilot-plan.json | cut -d' ' -f1)
build/release/seam_voicebank_cli plan-generation-campaign /tmp/pilot-ws /tmp/pilot-plan.json \
  $PLAN_SHA256 /tmp/pilot-campaign
CAMPAIGN_SHA256=$(shasum -a 256 /tmp/pilot-campaign/campaign.json | cut -d' ' -f1)
build/release/seam_voicebank_cli preflight-generation-campaign \
  /tmp/pilot-campaign/campaign.json $CAMPAIGN_SHA256
```

This measurement's recipe is schema eleven (`7688afab...`) against the producer a run of the
block above initializes (`909e69b6...`). Two independent runs produce the same producer and recipe
digests; the producer digest differs from the first measurement's only because that workspace was
initialized at another time. The inventory and the profile are unchanged.

## Result

| Measure | Value |
|---|---|
| Assignments inspected | 1026 |
| Prepared (snapshot compiled) | **1026** |
| Refused | **0** |
| Declared coverage keys (style x key) | 342 |
| Keys with a prepared class | 342 |
| Phones declared | 41 |
| Phones covered by a prepared class | 41 |
| Coverage kinds declared | 8 |
| Kinds with any prepared class | 8 |
| Kinds entirely refused | 0 |

Prepared phones: `N a b by ch d e f fy g gy h hy i j k ky m my n ny o p py r ry s sh t ts u v vy w y z`.
The four declared events (`R`, `glottal`, `cl`, `pau`) and the breath `br` are prepared as well.
Missing phones: none. Missing kinds: none.

Prepared assignments, by kind: cv 450, vc 450, vv 60, sustain 15, release 15, glottal-attack 15,
special 15, breath 3.

## What the last 48 refusals needed, and what the event classes are not

The last 48 refusals were a modelling gap rather than a DSP one: the inventory names five symbols
that are events rather than articulated sounds, and neither the score adapter nor a recipe had a way
to say so. A recipe can now declare a closure (`R`, `glottal`, `cl`, `pau`) or a breath (`br`) as an
event span, and the two symbols the adapter never admitted (`R`, `br`) are part of its inventory.

1. **A closure is exactly silent for the span its role resolves.** The moraic obstruent owns the
   tail of its vowel note (`a R`), a glottal occlusion owns the span before its vowel
   (`glottal a`), and a pause or a standalone closure owns its whole note. The plan and the
   candidate both name the gesture, and the audio is zero rather than a quiet approximation of a
   consonant. Whether a listener accepts that as っ is unmeasured.
2. **A breath is unvoiced noise from its own declared source.** It has no resonance pose, because a
   breath is not shaped by a constriction, and its spectrum is this pilot engineering parameter
   rather than a measured aspiration.
3. **An undeclared event is still refused.** A closure is admitted only for the four symbols whose
   semantics it is, a breath only for `br`, and only in a style the recipe declares, so a symbol
   that a newer build happens to recognise never becomes silence on its own.

## What changed since the first measurement

The first measurement of this report refused 528 assignments. Five repairs closed all of them, and
each one kept the phone's identity rather than rewriting the syllable:

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
5. **The declared event classes (48).** A closure and a breath became explicit recipe declarations
   instead of gaps: the inventory could name `R`, `glottal`, `cl`, `pau` and `br` and nothing could
   render them, because a pause and a closure are events rather than recorded articulations.

## Consequences for the next steps

1. Every assignment prepares, so a campaign over the whole inventory can now be planned: 1026 jobs
   in one plan, and the rendered held-out preflight passes all 38 declared classes with none
   defective. Coverage is still not sound quality; the preflight is the gate that says a class can
   be produced at all, and it is the gate a later acoustic change has to keep passing.
2. The events coverage is the thinnest part of this result. `pau`, `cl` and `R` prepare as silence
   because that is what a closure is, and `br` prepares as declared noise. If a listener later
   judges a moraic obstruent or a breath unacceptable, the repair is a different model for those
   classes rather than a reinterpretation of this one: the report says which class is which.
3. Every prepared class is still declared source parameters and spectra, not phonetic
   qualification. Being able to prepare a `j` or a `cl` unit says nothing about whether it sounds
   like a singer, and no listener has heard any of it.
