# Pilot 01 generated bank

The inventory campaign ran to completion. This is the first material the producer pipeline has
generated from the pilot inventory rather than from a standalone fixture: 498 takes of dry
procedural audio, committed as unapproved marker-review material. Engineering evidence only.
No listener has judged any of it and nothing here is an approval or a release decision.

## What was run

```sh
# one campaign for the lowest pitch layer, then one for the remaining two
build/release/seam_voicebank_cli draft-generation-campaign PRODUCER RECIPE PLAN_JSON TAKE_ID...
build/release/seam_voicebank_cli plan-generation-campaign PRODUCER PLAN_JSON PLAN_SHA NEW_CAMPAIGN_DIRECTORY
build/release/seam_voicebank_cli preflight-generation-campaign CAMPAIGN_JSON CAMPAIGN_SHA
build/release/seam_voicebank_cli advance-generation-campaign PRODUCER CAMPAIGN_JSON CAMPAIGN_SHA OPERATOR UTC
```

The first campaign selected one renderable coverage key per class at the lowest pitch layer
(166 assignments, 3 batches, campaign `c312a745...`); the second selected the same keys at the
two higher layers (332 assignments, 6 batches, campaign `2b7a1c02...`). Both preflights passed
with 17 held-out phrases produced and none refused before either campaign advanced. Advancement
is resumable and idempotent: repeating it on a completed campaign returned the same
`COLLECTED_UNREVIEWED` status, the same 498 takes and the same durable generation, and no JSON
was edited by hand at any point.

## Result

| Measure | Value |
|---|---|
| Takes committed | **498** |
| Coverage keys | 166, at 3 pitch layers each (60, 66, 72) |
| Kinds | cv 210, vc 210, vv 60, sustain 15, special 3 |
| Take state | 498 MARKER_REVIEW, 0 approved |
| Durable generations | 11 |
| Unit length | 24000 frames (500 ms) at 48000 Hz, every take |
| Peak range | 0.0110 to 0.1571 |
| Nonzero coverage | 90% to 100%, no silent take |
| Missing audio | 0 of 498 |

Per kind: cv peak 0.0160-0.1439 at 90-100% nonzero; vc 0.0160-0.1435 at 90-100%; vv
0.0195-0.0717 at 100%; sustain 0.0110-0.0717 at 100%; special 0.0406-0.1571 at 100%. The 90%
classes are the stops, whose closure is deliberately silent for about 60 ms of each unit.

## Poor-quality contexts to listen to first

Not a defect list and not a threshold: these are the quietest contexts in the bank, measured as
peak amplitude, and they are the ones a listening pass should hear first.

| Context | Layer | Peak | Nonzero |
|---|---|---|---|
| sustain:a | 60 | 0.0110 | 100% |
| sustain:a | 66 | 0.0156 | 100% |
| cv:y:a | 72 | 0.0160 | 100% |
| vc:a:y | 72 | 0.0160 | 100% |
| cv:y:i | 72 | 0.0165 | 100% |
| vc:i:y | 72 | 0.0186 | 100% |
| vv:e:a | 60 | 0.0195 | 100% |
| sustain:e | 60 | 0.0197 | 100% |
| vv:a:e | 60 | 0.0197 | 100% |
| vc:a:w | 72 | 0.0197 | 100% |
| vc:i:w | 72 | 0.0197 | 100% |

Eleven of 498 takes sit below 0.02 peak and every one of them is a vowel-only or glide context,
which fits a model whose loudest classes are the noise-bearing ones rather than proving a defect.
The unsupported side of the list is the coverage report: 528 assignments still cannot prepare at
all, and they are the classes that need new source models.

## Where the evidence is

The producer workspace, its eleven generations, all 498 raw take assets and both campaign
directories are retained under `build/pilot-01/` (build output is not tracked). Audio lives at
`build/pilot-01/producer/assets/raw/<first two hex>/<sha256>.wav`, keyed by each take's
`rawAssetSha256` in `build/pilot-01/producer/project.json`. The rendered preflight report is
`build/pilot-01/campaign-registered/preflight/report.json` and its reading is in
`PREFLIGHT_REPORT.md`.

## What this does not establish

A complete bank is not a usable voicebank. These takes are unapproved marker-review material
with no listening, no review decision, no range or style qualification, no identity assessment
and no package. The recipe remains a diagnostic source-filter model, so "the bank renders" says
nothing about whether it sounds like a singer. Only one style (neutral) and the three pilot pitch
layers exist, and 528 of the inventory's 1026 assignments have no source model at all.

