# Flatness/level component ablation — execution record

Date recorded: 2026-09-24
Frozen protocol: [FLATNESS_COMPONENT_ABLATION_PLAN_2026-09-22.md](FLATNESS_COMPONENT_ABLATION_PLAN_2026-09-22.md)
Protocol SHA-256: `7f518078781ddc3964b41f723e64a8af346085ff857059ccfd3ffc7e88da4f85`

This is a result record for the already-existing local experiment, not a new
training dispatch. The frozen plan said the runs had not launched, but current
state contains both completed e9 outputs and a four-arm evaluation dated
2026-09-22. The plan itself is left unchanged because its exact hash is part of
the evaluation receipt.

## Run and receipt verification

The recorded assessment says Developer 2 cleared exactly the two staged arms.
No matching trainer process was live during this follow-up, and neither output
directory was created or overwritten in this review.

Both new checkpoints were loaded through `tools.voice_model_training.checkpoint.load_local_checkpoint`
against their captured receipt hashes, which also checks the checkpoint byte
hash and the serialized model/optimizer state against the receipt. Each new
arm reports `epochComplete=true`, `coverageVerified=true`, 267 unique sources
and 267 updates on the frozen dataset, and `trainingAdmitted=false` /
`releaseEligible=false`.
The two new training-config hashes match the configs recorded in checkpoint
metadata. Both warm starts bind to the same e8 parent receipt. A local
recomputation compared `(sourceId, timesteps, noiseSha256)` for all 267 ordered
updates across base, combined, flat-only and level-only: zero mismatches in
either new arm. The independent Developer-2 artifact audit additionally
verified per-source frame attribution/coverage and valid-sample totals,
eligibility/frame attribution parity, checkpoint and receipt hashes,
checkpoint-to-export bindings, exported acoustic ONNX hashes, evaluator arm
graph identities, all 160 retained development-mel hashes (eight distinct
draws per source/arm), five replay-capture hashes, current source WAV hashes
and their validation/test partition labels, and the fixed vocoder graph/export
receipt binding.

The 267-step match is data/draw/coverage parity, **not** 267 empirically
equivalent optimizer updates and not equality of learned weights or losses.
The experimental protocol's optimizer-equivalence claim is limited to the 24
empirical optimizer updates at each equal-coefficient corner. No stronger
optimizer-equivalence claim is made here.

| Arm | Training config SHA-256 | Checkpoint receipt SHA-256 |
|---|---|---|
| Flatness-only | `9035426ac73f3c93a0eda02c9c8c37ed71db602895e608fc557d8e62319fae52` | `4a6800fb62dfaee6a5649d652af4266bf5d419f3785cc3bee11448ca723fee05` |
| Level-only | `34005dd69a2954cfeddc23a01f9ed1070577c59b7852f75745116f106f08605f` | `acd8b3c222ac0a4a7f37388c083e4d3d461a69ca391877379403c44dcd2b08a0` |

The four-arm evaluator covers five frozen zero-breathiness development sources
with eight draws each, plus a separately labeled 12-item held-out panel. Its
guardrails are marked complete. The evaluator uses the frozen vocoder digest
`9a733810a69b71973d5c92c84c647a3f115acfb34966599354f2bc65b4f2dd2c`; its
listening status remains `NOT_REVIEWED`.

Artifacts are under
`/Users/lhs/seam-corpus-pauses-2026-09-19-r1/`:

- `flatness-ablation-eval-e9-r1/assessment.json` — SHA-256
  `0023b724ad8cd9d1b866be53abf235f59fa26f7c1c8d94692d25173466446dbe`.
- `flatness-ablation-eval-e9-r1/pair-eval.json` — SHA-256
  `93d7f58e03a1a170f365cc37140230e65891066596370f0df0159de56196ea32`.

## Result and frozen decision

Primary target-relative unvoiced-flatness error (lower is better):

| Base | Combined | Flatness-only | Level-only |
|---:|---:|---:|---:|
| 2.8325 | 3.7984 | 3.9403 | 3.8243 |

Neither isolated term improves on base. Both also fail the frozen unvoiced RMS
ratio range `[0.80, 1.20]` (flatness-only `0.7527`, level-only `0.6103`) and
the voiced-flatness guardrail. Weighted pitch error improves in both isolated
arms, but that does not override the primary failure or other guardrail
failures. The predeclared decision is therefore **STOP this auxiliary family at
the tested coefficient and parent**. Do not launch a layer/weight sweep from
this result. The modestly better combined value than either isolated value is
only an interaction observation; it remains worse than base.

This is a bounded local attribution (one seed, one latent schedule, one epoch,
five development sources), not a unique causal explanation or model
qualification. The assessment correctly leaves `singerQualified=false`,
`releaseEligible=false`, and listening `NOT_REVIEWED`. The held-out panel is
reported separately and is not a replacement for the primary decision.

The independent audit reaggregated the primary values at full precision:
base `2.8324867201`, combined `3.7983669213`, flatness-only `3.9402576699`,
and level-only `3.8242547399`. It also confirmed flatness-only / level-only
unvoiced RMS ratios of `0.752653` / `0.610261`, and voiced-flatness error
regressions of `+0.976067` / `+0.732958`. Pitch improved in both isolated
arms; there were no failed draws, clips or silent outputs. Those facts do not
override the primary development failure or guardrail failures.

## Provenance limitation and review boundary

The saved evaluation records bind model/vocoder graphs and source audio, but do
not include the evaluator source digest, pitch-executable digest, or a complete
runtime identity. This does not invalidate the frozen result receipt, and the
old receipt is not retroactively described as containing those fields. The
evaluator now writes these identities (plus the provenance helper digest) into
schema-version-3 receipts for future runs; the new helper has a dependency-light
local regression test. No future experiment has been dispatched. The
independent audit reviewed saved artifacts and aggregates. It did not rerun
inference, pitch extraction, listening, or historical wall-clock/resource
compliance.

## Next implementation consequence

Keep the tested flatness/level auxiliary disabled at this coefficient. Continue
the neural-singing investigation with a newly specified, independently reviewed
experiment that targets a distinct diagnosed cause; do not tune this loss family
by an unplanned sweep. In parallel, proceed with the remaining code-owned
performance/compiler and usable-song gaps in the approved full-scope plan.
Windows support and GitHub CI remain deferred.

Developer-2 independently reviewed the completed saved artifacts and
**APPROVED the frozen STOP decision and closure at this tested setting only**.
This is not approval to relaunch or expand the experiment, retain either
isolated term, qualify a singer, or release a model. The exact assessment and
pair-evaluation SHA-256 values above were confirmed by that audit.
