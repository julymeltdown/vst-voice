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

## The fix, implemented

The recommended design was taken: **the contract refuses a stochastic draw at
admission.** Two changes, both small.

`GraphNodeContract` gained an optional `seed`
(`graph_contract.hpp:54`), and the attribute parser now READS AttributeProto field 5
instead of skipping it with the other attributes (`graph_contract.cpp:390`). That
field is the one attribute whose value changes what a graph *does* rather than how
it is shaped, which is why it was the one worth keeping.

`parseNode` then refuses any `RandomNormalLike` with no declared seed, naming the
operator and both repairs in the message (`graph_contract.cpp:462`). Refusing at
admission is deliberate: it is the last point where the defect is cheap to fix,
because the alternative is shipping audio nobody can re-derive.

Seed `0` is treated as a real seed rather than an absent one. That is deliberate too:
refusing zero would make it unusable while every other value worked, which is exactly
the kind of quiet asymmetry this check must not have.

**Mutation-checked in both halves, because a contract check that cannot fail is
decoration.** Disabling the refusal fails the case at the refusal assertion; parsing
the seed and then discarding it fails at the admission assertion. Both halves are
load-bearing, and neither was demonstrated by writing the test alone.

**One assertion of mine was wrong and the test caught it.** The first version located
the seeded node by searching for an input named `noise`, which does not survive the
contract's own normalisation, and the case failed on a node lookup. Locating it by
`seed.has_value()` is the correct predicate. Recorded because a test that passes the
first time has usually not been tested hard enough.

**Existing fixtures were unaffected.** No checked-in test graph declares an unseeded
`RandomNormalLike`, so this closes a real gap rather than invalidating prior evidence.
All 17 pre-existing neural tests passed unchanged before the new case was added.

## What is and is not established

**Established, by reading the current source.** The defect's three sites, the absence
of any seed handling in the neural synthesis layer, the absence of an ONNX Runtime
session seed key, and the exact reason the pitch criterion is `UNRESOLVED`. Each claim
above names the file and line it rests on.

**Not established, and explicitly not claimed.**

- **No neural render was run.** There is still no model: `find . -name '*.onnx'`
  outside test datasets returns nothing, and `DiffSinger-source/checkpoints` holds
  only a `.gitkeep`. So the four differing digests are quoted from the readiness
  register, not reproduced this session.
- **This does not unblock M3.** Determinism was a prerequisite for trustworthy neural
  pitch evidence, not a substitute for having a model, and the model is still
  missing. Both are needed and only one is now done.
- **The exporter is unchanged.** A graph exported with `RandomNormalLike` and no seed
  is now refused at admission rather than rendered. Whoever produces the first
  bundle must export a fixed seed or bind the noise as a graph input; that work is
  outside this repository.

## Why this is listed separately from the external blockers

U42 needs an asset nobody here can manufacture. This needs a decision plus a code
change, both inside the repository's own boundary. Recording it separately keeps the
externally-blocked list honest: U42 is the only item on it that genuinely cannot be
advanced in this repository at all.
