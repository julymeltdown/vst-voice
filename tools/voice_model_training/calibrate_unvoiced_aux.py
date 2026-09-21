"""Calibrate the unvoiced clean-mel auxiliary weight on training-only phrases.

This is a gradient-only diagnostic: it loads a trusted local checkpoint, runs
the auxiliary adapter's identical forward under a forked RNG, measures base and
auxiliary gradient norms on a bounded set of training phrases, and reports the
single weight that would make the initial auxiliary gradient roughly ten
percent of the base. No checkpoint, optimizer or global RNG state is mutated,
and the emitted weight is a trial setting for one paired experiment, not an
acceptance threshold or a trained-model claim. It also inspects the predicted
clean reconstruction at fixed timesteps so a denoising failure can be told
apart from a sampling/export failure before any update is spent.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess
import sys
import time

from .__main__ import assemble_dataset, load_config, load_dataset_inputs, publish_new
from .batches import iter_supervised_batches
from .checkpoint import load_local_checkpoint
from .diffsinger_objective import (DiffSingerDDPMUnvoicedSpectralObjective,
                                   DiffSingerDDPMUnvoicedFlatnessObjective,
                                   UNVOICED_AUXILIARY_KIND, UNVOICED_FLATNESS_KIND)
from .train import REVISION, initialize_checkpoint, load_targets, model_settings

FIXED_TIMESTEP_FRACTIONS = (0.0, 0.25, 0.5, 0.75, 1.0)


def _step_tensors(batch: dict, device):
    """Reproduce the tensor construction of the training step exactly."""
    import torch
    target = batch["melTargets"]
    count = target.shape[0]
    loss_mask = batch.get("lossMask", [True] * count)
    columns = batch["columns"]
    inputs = {name: torch.tensor([columns[key]], dtype=dtype, device=device) for name, key, dtype in
              (("phoneIds", "phoneId", torch.int64), ("f0Hz", "f0Hz", torch.float32),
               ("voiced", "voiced", torch.bool), ("rest", "rest", torch.bool), ("slur", "slur", torch.bool))}
    inputs["breathiness"] = torch.tensor([columns["breathiness"]], dtype=torch.float32, device=device)
    inputs["midi"] = torch.tensor([[0 if value is None else value for value in columns["midi"]]],
                                  dtype=torch.int64, device=device)
    inputs["frameOffset"] = batch.get("frameOffset")
    inputs["phraseAnalysisFrames"] = batch.get("phraseAnalysisFrames")
    inputs["tokens"] = torch.tensor([batch["tokens"]], dtype=torch.int64, device=device)
    inputs["mel2ph"] = torch.tensor([batch["mel2ph"]], dtype=torch.int64, device=device)
    expected = torch.tensor(target.copy(), dtype=torch.float32, device=device)[None, :, :]
    weights = torch.tensor(columns["validSamples"], dtype=torch.float32, device=device)[None, :, None] / batch["hopSize"]
    weights *= torch.tensor(loss_mask, dtype=torch.float32, device=device)[None, :, None]
    return inputs, expected, weights


def _grad_norm(parameters, loss):
    import torch
    grads = torch.autograd.grad(loss, parameters, retain_graph=True, allow_unused=True)
    total = 0.0
    for grad in grads:
        if grad is not None:
            total += float(grad.square().sum())
    return math.sqrt(total)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("training_config", "dataset_config", "targets", "source_root", "conditioning",
                 "trusted_checkout", "warm_start", "output"):
        parser.add_argument("--" + name.replace("_", "-"), type=Path, required=True)
    for name in ("training_sha256", "dataset_sha256", "targets_sha256",
                 "rights_policy_sha256", "label_policy_sha256", "warm_start_receipt_sha256"):
        parser.add_argument("--" + name.replace("_", "-"), required=True)
    parser.add_argument("--phrases", type=int, default=8)
    args = parser.parse_args()
    try:
        if not 1 <= args.phrases <= 8:
            raise ValueError("Calibration must use between one and eight training phrases")
        if args.output.exists() or args.output.is_symlink() or not args.output.parent.is_dir():
            raise ValueError("Calibration output must be a new path in an existing parent")
        settings = load_config(args.training_config, args.training_sha256)
        auxiliary = settings.get("auxiliaryObjective")
        if (not isinstance(auxiliary, dict)
                or auxiliary.get("kind") not in (UNVOICED_AUXILIARY_KIND, UNVOICED_FLATNESS_KIND)):
            raise ValueError("Calibration requires a schema-3 auxiliary objective configuration")
        hparams_value = model_settings(settings)
        inputs_config = load_dataset_inputs(args.dataset_config, args.dataset_sha256, args.source_root,
                                            rights_anchor=args.rights_policy_sha256,
                                            label_anchor=args.label_policy_sha256)
        targets, profile = load_targets(args.targets, args.targets_sha256)
        labels = load_config(inputs_config["label_config"], inputs_config["label_hash"])
        vocabulary = labels.get("vocabulary")
        if not isinstance(vocabulary, list) or not 1 <= len(vocabulary) <= 4096:
            raise ValueError("Calibration requires a bounded captured vocabulary")
        dimensions = [record.get("profile", {}).get("bins") for record, _ in targets.values()]
        if any(type(b) is not int or not 1 <= b <= 512 for b in dimensions) or len(set(dimensions)) != 1:
            raise ValueError("Acoustic targets must share a bounded mel dimension")
        checkout = args.trusted_checkout.resolve(strict=True)
        def git(*arguments):
            return subprocess.check_output(["git", "-C", str(checkout), *arguments], text=True, timeout=10).strip()
        if git("rev-parse", "HEAD") != REVISION or git("status", "--porcelain"):
            raise ValueError("Calibration requires a clean trusted checkout at the pinned revision")
        import torch
        import numpy as np
        sys.path.insert(0, str(checkout))
        from utils.hparams import hparams
        hparams.clear()
        hparams.update(hparams_value)
        from modules.toplevel import DiffSingerAcoustic
        torch.set_num_threads(1)
        torch.manual_seed(settings["seed"])
        model = DiffSingerAcoustic(vocab_size=len(vocabulary) + 1, out_dims=dimensions[0])
        optimizer = torch.optim.AdamW(model.parameters(), lr=settings["learningRate"])
        metadata = dict(trainingConfigurationSha256=args.training_sha256,
                        assemblyConfigurationSha256=args.dataset_sha256,
                        targetInventorySha256=args.targets_sha256,
                        configuration=hparams_value, settings=settings, revision=REVISION,
                        torchVersion=str(torch.__version__), numpyVersion=np.__version__,
                        vocabulary=vocabulary, singerQualified=False)
        state, previous = load_local_checkpoint(args.warm_start,
                                                receipt_sha256=args.warm_start_receipt_sha256)
        initialize_checkpoint(model, optimizer, state, previous, metadata=metadata, profile=profile,
                              receipt_sha256=args.warm_start_receipt_sha256, warm_start=True)
        token_ids = {symbol: index + 1 for index, symbol in enumerate(vocabulary)}
        missing = [symbol for symbol in auxiliary["unvoicedSymbols"] if symbol not in token_ids]
        if missing:
            raise ValueError("Auxiliary unvoiced symbols are absent from the captured vocabulary")
        objective_cls = (DiffSingerDDPMUnvoicedSpectralObjective
                         if auxiliary["kind"] == UNVOICED_AUXILIARY_KIND
                         else DiffSingerDDPMUnvoicedFlatnessObjective)
        objective = objective_cls(
            settings["loss"], weight=0.0,
            unvoiced_ids=[token_ids[symbol] for symbol in auxiliary["unvoicedSymbols"]])
        snapshot = assemble_dataset(**inputs_config, now=int(time.time()),
                                    conditioning_directory=args.conditioning, reuse_conditioning=True)
        if (snapshot.get("schemaVersion") != 3 or snapshot.get("preparationIssues") != []
                or snapshot.get("sourcePermissionsAdmitted") is not True
                or snapshot.get("labelsAdmitted") is not True):
            raise ValueError("Calibration requires admitted sources, labels and complete partitions")
        batches = iter_supervised_batches(snapshot, args.conditioning, targets,
                                          expected_profile_sha256=profile, partition="train",
                                          batch_frames=4096, context_frames=0)
        parameters = [p for p in model.parameters() if p.requires_grad]
        device = parameters[0].device
        model.eval()
        rows, seen = [], set()
        for batch in batches:
            if len(rows) >= args.phrases:
                break
            if batch["sourceId"] in seen:
                continue
            inputs, expected, weights = _step_tensors(batch, device)
            unvoiced = torch.isin(inputs["phoneIds"], torch.tensor(
                objective.unvoiced_ids, dtype=inputs["phoneIds"].dtype, device=device)) & ~inputs["rest"]
            coverage = int(unvoiced.sum())
            if not coverage:
                continue
            seen.add(batch["sourceId"])
            with torch.random.fork_rng(devices=[]):
                torch.manual_seed(settings["seed"])
                base_elem, aux_elem = objective.components(model, inputs, expected)
            denominator = weights.sum() * expected.shape[2]
            base_scalar = (base_elem * weights).sum() / denominator
            aux_scalar = (aux_elem * weights).sum() / denominator
            base_norm = _grad_norm(parameters, base_scalar)
            aux_norm = _grad_norm(parameters, aux_scalar)
            rows.append(dict(sourceId=batch["sourceId"], unvoicedFrames=coverage,
                             draw=objective.last_draw,
                             baseLoss=float(base_scalar), auxiliaryLoss=float(aux_scalar),
                             baseGradientNorm=base_norm, auxiliaryGradientNorm=aux_norm,
                             lambdaForTenPercent=(0.1 * base_norm / aux_norm if aux_norm > 0 else None)))
        if not rows:
            raise ValueError("No training phrase with unvoiced coverage was found")
        ratios = [row["lambdaForTenPercent"] for row in rows if row["lambdaForTenPercent"] is not None]
        if not ratios:
            raise ValueError("Auxiliary gradient is zero on every selected phrase")
        ratios.sort()
        middle = len(ratios) // 2
        weight = ratios[middle] if len(ratios) % 2 else (ratios[middle - 1] + ratios[middle]) / 2
        # Fixed-timestep clean-reconstruction inspection on the first selected phrase.
        inspections = []
        batches = iter_supervised_batches(snapshot, args.conditioning, targets,
                                          expected_profile_sha256=profile, partition="train",
                                          batch_frames=4096, context_frames=0)
        first = rows[0]["sourceId"]
        for batch in batches:
            if batch["sourceId"] != first:
                continue
            inputs, expected, weights = _step_tensors(batch, device)
            diffusion = model.diffusion
            condition = model.fs2(inputs["tokens"], mel2ph=inputs["mel2ph"], f0=inputs["f0Hz"],
                                  breathiness=inputs["breathiness"])
            cond = condition.transpose(1, 2)
            spec = diffusion.norm_spec(expected).transpose(-2, -1)[:, None, :, :]
            with torch.no_grad(), torch.random.fork_rng(devices=[]):
                torch.manual_seed(settings["seed"])
                noise = torch.randn_like(spec)
                for fraction in FIXED_TIMESTEP_FRACTIONS:
                    step = min(diffusion.k_step - 1, int(round(fraction * (diffusion.k_step - 1))))
                    t = torch.full((1,), step, dtype=torch.long, device=device)
                    x_t = diffusion.q_sample(x_start=spec, t=t, noise=noise)
                    predicted = diffusion.denoise_fn(x_t, t, cond)
                    x0 = diffusion.predict_start_from_noise(x_t, t=t, noise=predicted)
                    z_hat = diffusion.denorm_spec(x0)[:, 0].transpose(1, 2)
                    inspections.append(dict(timestep=step,
                                            alphaBar=float(diffusion.alphas_cumprod[step]),
                                            cleanMelL1=float((z_hat - expected).abs().mean())))
            break
        publish_new(args.output, dict(
            formatId="com.project-seam.unvoiced-aux-calibration", schemaVersion=1,
            trainingConfigurationSha256=args.training_sha256,
            datasetSha256=snapshot["datasetSha256"],
            sourceReceiptSha256=args.warm_start_receipt_sha256,
            sourceCheckpointSha256=previous["checkpointSha256"],
            seed=settings["seed"], phrases=rows, fixedTimestepInspection=inspections,
            calibratedWeight=weight, singerQualified=False, releaseEligible=False))
        print(json.dumps(dict(calibratedWeight=weight, phrases=len(rows))))
        return 0
    except (ValueError, OSError, RuntimeError, ImportError, RecursionError, subprocess.SubprocessError) as error:
        print(str(error)[:256], file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
