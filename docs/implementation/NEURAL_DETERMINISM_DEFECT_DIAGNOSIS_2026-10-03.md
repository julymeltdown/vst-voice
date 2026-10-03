# Neural per-request determinism: located defect, exact fix site, and why it is
not an external-asset blocker

Date: 2026-10-03.

## Why this entry exists

The remaining open items were described as needing the user: an ear for the formant
bandwidth, a listening judgement for M2.1, and a rights-cleared voicebank for U42.
That list was incomplete. `BETA_READINESS_ISSUES.md` also records a **neural
determinism defect with concrete closure criteria that is fixable in this
repository**, and it is the reason the neural pitch-adherence criterion reports
`UNRESOLVED` rather than a measured result.

## The defect

The same project renders differently on every export through the neural backend.
Four separate `seam_neural_worker` processes given byte-identical requests returned
four different audio digests. Within one ONNX Runtime session two consecutive calls
differ; two freshly created sessions return the same first result. The generator is
therefore seeded per session, and the shipped worker completes exactly one request
per process, so per-request determinism is absent even though a single call is
reproducible.

This contradicts the product's own `reproducibility-tolerances/*/neural/pcm-error`
contract: a creator cannot reproduce a take, a reviewer cannot compare candidates on
identical bytes, and no release evidence can be re-derived from a frozen project.

## The two sites, located exactly

**1. The graph has no bindable noise input.** The exported acoustic graph contains a
`RandomNormalLike` node whose only input is `diffusion//ConstantOfShape_output_0`,
and the graph's declared inputs are exactly `tokens`, `durations`, `f0` and `steps`.
There is no noise or seed input to bind, so no amount of worker-side seeding can make
the draw reproducible. `RandomNormalLike` is admitted by the graph contract
(`libs/seam-neural-synthesis/src/graph_contract.cpp:28`).

**2. The contract ignores the seed attribute that would fix it.** ONNX's
`RandomNormalLike` carries an optional `seed` attribute (AttributeProto field 5), and
the contract's attribute parser skips field 5 outright rather than reading it
(`graph_contract.cpp:383`). So a graph exported WITH a fixed seed is admitted exactly
as a graph exported without one, and the build cannot tell a deterministic export from
a stochastic one. That is the gap: the information needed to enforce determinism is
present in the model file and is discarded on the way in.

**3. The worker creates its session with no seed at all.**
`apps/seam-neural-worker/main.cpp:156` constructs `Ort::SessionOptions`, sets only
thread counts and execution mode, and opens the session through a lambda that takes
no seed. There is no `seed` member anywhere in `libs/seam-neural-synthesis/src/`, so
the request carries no seed either.

ONNX Runtime 1.30 exposes no session-level seed configuration key
(`onnxruntime_session_options_config_keys.h` contains no seed or random entry), which
is why the fix has to be at the graph or request level rather than the session level.

## Why the pitch criterion reads UNRESOLVED

`tools/voice_model_training/qualification.py` judges pitch LAST, on purpose. A
determinism failure returns before pitch is ever measured, so the criterion is set
back to `UNRESOLVED` with the reason "not measured: determinism failed on this item"
(`qualification.py:413` through 419). The dossier no longer claims a measured `PASS` it never
measured, which is the honest state, but it means **no neural pitch evidence exists
at all** until determinism is fixed. Fixing determinism is therefore a
prerequisite for M3's acoustic evidence, not a side issue.

## What is and is not established

**Established, by reading the current source.** The defect's three sites, the absence
of any seed handling in the neural synthesis layer, the absence of an ONNX Runtime
session seed key, and the exact reason the pitch criterion is `UNRESOLVED`. Each claim
above names the file and line it rests on.

**Not established, and explicitly not claimed.**

- **The fix is not implemented here.** Two designs are possible: require a fixed
  `seed` attribute on every admitted `RandomNormalLike` and reject a graph without
  one, or add a noise input the request binds. Both change what the exporter must
  produce, which is a contract change to the model family, not a local patch. That
  decision is not mine to make unilaterally.
- **No neural render was run.** There is still no model: `find . -name '*.onnx'`
  outside test datasets returns nothing, and `DiffSinger-source/checkpoints` holds
  only a `.gitkeep`. So the four differing digests are quoted from the readiness
  register, not reproduced this session.
- **This does not unblock M3.** Determinism is a prerequisite for trustworthy neural
  pitch evidence, not a substitute for having a model. Both are needed.

## Why this is listed separately from the external blockers

U42 needs an asset nobody here can manufacture. This needs a decision plus a code
change, both inside the repository's own boundary. Recording it separately keeps the
externally-blocked list honest: U42 is the only item on it that genuinely cannot be
advanced in this repository at all.
