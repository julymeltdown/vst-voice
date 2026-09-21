"""Production-equivalence probe for the flatness/level component ablation.

Before the completed flatness pair cells may be reused as the (0, 0) and
(lambda, lambda) corners of the split-coefficient ablation, this probe
demonstrates ACTUAL production loss/gradient equivalence rather than relying
on the algebraic argument alone. It reconstructs the exact training setup of
the completed pair (same e8 parent, admitted dataset snapshot, conditioning,
seed, optimizer, source order), then replays the first N admitted updates:

- corner (0, 0): DiffSingerDDPMUnvoicedFlatnessObjective(weight 0) replayed in
  lockstep with DiffSingerDDPMUnvoicedFlatnessComponentsObjective(0, 0);
- corner (lambda, lambda): the single-weight objective at the calibrated
  lambda replayed in lockstep with the split objective at (lambda, lambda).

Within each step the Torch RNG state is captured before the single-weight
update and restored before the split-weight update, so both consume identical
timestep/noise draws. The single-weight replay's per-step records are also
compared bitwise against the recorded production steps.json, binding the
replay to the actual captured draws rather than a recomputation.

Equivalence claim per corner (all must hold for every replayed update):
loss, gradientNorm, sourceId and every draw field match the recorded
production step bitwise; the split objective's step record matches the
single-weight record bitwise; and post-update model parameters and AdamW
state tensors are torch.equal between the two lockstepped models.

Diagnostic only: no promotion, qualification, or release eligibility.
"""
import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import time

from .__main__ import assemble_dataset, load_config, load_dataset_inputs, publish_new
from .batches import iter_supervised_batches
from .checkpoint import load_local_checkpoint
from .diffsinger_objective import (DiffSingerDDPMUnvoicedFlatnessObjective,
                                  DiffSingerDDPMUnvoicedFlatnessComponentsObjective)
from .optimization import acoustic_training_step
from .train import REVISION, initialize_checkpoint, load_targets, model_settings


def _sha_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _param_sha(model) -> str:
    digest = hashlib.sha256()
    for parameter in model.parameters():
        digest.update(parameter.detach().cpu().numpy().tobytes())
    return digest.hexdigest()


def _optimizer_sha(optimizer) -> str:
    import torch
    digest = hashlib.sha256()
    for state in optimizer.state_dict()["state"].values():
        for key in sorted(state):
            value = state[key]
            if isinstance(value, torch.Tensor):
                digest.update(key.encode())
                digest.update(value.detach().cpu().numpy().tobytes())
    return digest.hexdigest()


def _params_equal(model_a, model_b) -> bool:
    import torch
    params_a = list(model_a.parameters())
    params_b = list(model_b.parameters())
    return (len(params_a) == len(params_b)
            and all(torch.equal(a, b) for a, b in zip(params_a, params_b)))


def _optimizer_equal(opt_a, opt_b) -> bool:
    import torch
    state_a = opt_a.state_dict()["state"]
    state_b = opt_b.state_dict()["state"]
    if set(state_a) != set(state_b):
        return False
    for key in state_a:
        if set(state_a[key]) != set(state_b[key]):
            return False
        for name in state_a[key]:
            va, vb = state_a[key][name], state_b[key][name]
            if isinstance(va, torch.Tensor) or isinstance(vb, torch.Tensor):
                if not (isinstance(va, torch.Tensor) and isinstance(vb, torch.Tensor)
                        and torch.equal(va, vb)):
                    return False
            elif va != vb:
                return False
    return True


def _build_model(settings, hparams_value, vocabulary_size, warm_start, receipt_sha):
    """Reproduce train.py's construction order: seed, model, optimizer, then
    the verified warm-start load. Returns (model, optimizer, rng_state)."""
    import numpy as np
    import torch
    from utils.hparams import hparams
    hparams.clear()
    hparams.update(hparams_value)
    from modules.toplevel import DiffSingerAcoustic
    torch.set_num_threads(1)
    torch.manual_seed(settings["seed"])
    model = DiffSingerAcoustic(vocab_size=vocabulary_size + 1, out_dims=settings["_melBins"])
    optimizer = torch.optim.AdamW(model.parameters(), lr=settings["learningRate"])
    state, previous = load_local_checkpoint(warm_start, receipt_sha256=receipt_sha)
    metadata = dict(trainingConfigurationSha256=settings["_trainingSha256"],
                    assemblyConfigurationSha256=settings["_datasetSha256"],
                    targetInventorySha256=settings["_targetsSha256"],
                    configuration=hparams_value, settings=settings["_raw"],
                    revision=settings["_revision"],
                    torchVersion=str(torch.__version__), numpyVersion=np.__version__,
                    vocabulary=settings["_vocabulary"], singerQualified=False)
    initialize_checkpoint(model, optimizer, state, previous, metadata=metadata,
                          profile=settings["_profile"], receipt_sha256=receipt_sha,
                          warm_start=True)
    return model, optimizer, torch.get_rng_state()


def _step_record(result):
    row = dict(sourceId=result["sourceId"], loss=result["loss"],
               gradientNorm=result["gradientNorm"], lossFrames=result["lossFrames"],
               validSamples=result["validSamples"])
    if result.get("draw") is not None:
        row["draw"] = result["draw"]
    return row


def _replay_corner(name, settings, build, batches, recorded_steps, vocabulary_size,
                   single_objective, split_objective):
    """Lockstep-replay the first len(batches) admitted updates under both
    objectives and compare bitwise against each other and the recorded run."""
    import torch
    model_a, opt_a, rng0 = build()
    model_b, opt_b, _ = build()
    steps, all_equal = [], True
    rng = rng0
    for index, batch in enumerate(batches):
        recorded = recorded_steps[index] if index < len(recorded_steps) else None
        results = {}
        for tag, model, opt, objective in (("single", model_a, opt_a, single_objective),
                                           ("split", model_b, opt_b, split_objective)):
            torch.set_rng_state(rng)
            results[tag] = acoustic_training_step(
                model, opt, deepcopy(batch), vocabulary_size=vocabulary_size,
                objective=objective, objective_id=objective.objective_id)
        rng = torch.get_rng_state()
        row_a, row_b = _step_record(results["single"]), _step_record(results["split"])
        step = dict(index=index, sourceId=row_a["sourceId"],
                    singleLoss=row_a["loss"], splitLoss=row_b["loss"],
                    singleGradientNorm=row_a["gradientNorm"],
                    splitGradientNorm=row_b["gradientNorm"],
                    singleParamSha256=_param_sha(model_a),
                    splitParamSha256=_param_sha(model_b),
                    singleOptimizerSha256=_optimizer_sha(opt_a),
                    splitOptimizerSha256=_optimizer_sha(opt_b),
                    recordsBitwiseEqual=row_a == row_b,
                    parametersBitwiseEqual=_params_equal(model_a, model_b),
                    optimizerBitwiseEqual=_optimizer_equal(opt_a, opt_b),
                    recordedMatch=(row_a == recorded))
        if recorded is not None:
            step["recordedLoss"] = recorded["loss"]
            step["recordedGradientNorm"] = recorded["gradientNorm"]
        equal = (step["recordsBitwiseEqual"] and step["parametersBitwiseEqual"]
                 and step["optimizerBitwiseEqual"] and step["recordedMatch"])
        step["bitwiseEquivalent"] = bool(equal)
        all_equal = all_equal and equal
        steps.append(step)
    return dict(corner=name, updates=len(steps), bitwiseEquivalent=bool(all_equal),
                steps=steps)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ("training-config", "dataset-config", "targets", "source-root",
                 "conditioning", "trusted-checkout", "warm-start", "control-steps",
                 "treatment-steps", "output"):
        ap.add_argument("--" + name, type=Path, required=True)
    for name in ("training-sha256", "dataset-sha256", "targets-sha256",
                 "warm-start-receipt-sha256", "rights-policy-sha256",
                 "label-policy-sha256"):
        ap.add_argument("--" + name, required=True)
    ap.add_argument("--updates", type=int, default=24)
    ap.add_argument("--epoch-dataset-sha256", required=True,
                    help="datasetSha256 recorded in the completed run's epoch receipt")
    args = ap.parse_args()
    if args.output.exists():
        raise SystemExit("Output must be new")
    if not 1 <= args.updates <= 267:
        raise SystemExit("Updates must be within the recorded epoch length")

    import sys
    sys.path.insert(0, str(args.trusted_checkout))
    settings = load_config(args.training_config, args.training_sha256)
    hparams_value = model_settings(settings)
    inputs = load_dataset_inputs(args.dataset_config, args.dataset_sha256, args.source_root,
                                 rights_anchor=args.rights_policy_sha256,
                                 label_anchor=args.label_policy_sha256)
    targets, profile = load_targets(args.targets, args.targets_sha256)
    labels = load_config(inputs["label_config"], inputs["label_hash"])
    vocabulary = labels["vocabulary"]
    token_ids = {symbol: index + 1 for index, symbol in enumerate(vocabulary)}
    auxiliary = settings["auxiliaryObjective"]
    unvoiced_ids = [token_ids[symbol] for symbol in auxiliary["unvoicedSymbols"]]
    lam = float(auxiliary["weight"])

    snapshot = assemble_dataset(**inputs, now=int(time.time()),
                                conditioning_directory=args.conditioning,
                                reuse_conditioning=True)
    if snapshot["datasetSha256"] != args.epoch_dataset_sha256:
        raise SystemExit("Admitted dataset differs from the recorded epoch identity")
    batches_iter = iter_supervised_batches(snapshot, args.conditioning, targets,
                                           expected_profile_sha256=profile,
                                           partition="train", batch_frames=4096,
                                           context_frames=0)
    batches = []
    for index, batch in enumerate(batches_iter):
        if index >= args.updates:
            break
        batches.append(batch)

    control_steps = json.loads(args.control_steps.read_text())["steps"]
    treatment_steps = json.loads(args.treatment_steps.read_text())["steps"]
    if len(control_steps) < args.updates or len(treatment_steps) < args.updates:
        raise SystemExit("Recorded steps.json shorter than requested replay")

    dimensions = [record.get("profile", {}).get("bins")
                  for record, _ in targets.values()]
    bound = dict(settings, _raw=settings, _melBins=dimensions[0],
                 _profile=profile, _vocabulary=vocabulary,
                 _trainingSha256=args.training_sha256,
                 _datasetSha256=args.dataset_sha256,
                 _targetsSha256=args.targets_sha256,
                 _revision=REVISION)

    def build():
        return _build_model(bound, hparams_value, len(vocabulary), args.warm_start,
                            args.warm_start_receipt_sha256)

    import torch
    corners = [
        _replay_corner("zero", bound, build, batches, control_steps,
                       len(vocabulary),
                       DiffSingerDDPMUnvoicedFlatnessObjective(
                           settings["loss"], 0.0, unvoiced_ids=unvoiced_ids),
                       DiffSingerDDPMUnvoicedFlatnessComponentsObjective(
                           settings["loss"], 0.0, 0.0, unvoiced_ids=unvoiced_ids)),
        _replay_corner("lambda", bound, build, batches, treatment_steps,
                       len(vocabulary),
                       DiffSingerDDPMUnvoicedFlatnessObjective(
                           settings["loss"], lam, unvoiced_ids=unvoiced_ids),
                       DiffSingerDDPMUnvoicedFlatnessComponentsObjective(
                           settings["loss"], lam, lam, unvoiced_ids=unvoiced_ids)),
    ]
    report = dict(
        formatId="com.project-seam.acoustic-ablation-equivalence", schemaVersion=1,
        updatesPerCorner=args.updates, corners=corners,
        equivalent=all(corner["bitwiseEquivalent"] for corner in corners),
        lambdaCoefficient=lam,
        bindings=dict(
            trainingConfigurationSha256=args.training_sha256,
            assemblyConfigurationSha256=args.dataset_sha256,
            targetInventorySha256=args.targets_sha256,
            datasetSha256=snapshot["datasetSha256"],
            warmStartReceiptSha256=args.warm_start_receipt_sha256,
            controlStepsSha256=_sha_file(args.control_steps),
            treatmentStepsSha256=_sha_file(args.treatment_steps)),
        rngPolicy=("per-update: torch.get_rng_state() captured before the "
                   "single-weight update and restored before the split-weight "
                   "update so both consume identical timestep/noise draws"),
        torchVersion=str(torch.__version__),
        singerQualified=False, releaseEligible=False)
    publish_new(args.output, report)
    print(json.dumps(dict(done=True, equivalent=report["equivalent"],
                          output=str(args.output))))


if __name__ == "__main__":
    raise SystemExit(main())
