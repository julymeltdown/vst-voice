"""Held-out acoustic-only diagnostics; no vocoder, optimization or musical approval."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

from .__main__ import assemble_dataset, load_config, load_dataset_inputs, publish_new
from .batches import iter_supervised_batches
from .checkpoint import load_local_checkpoint
from .export import export_identity
from .train import REVISION, load_targets


def require_validation_sources(snapshot, source_ids):
    validation = {source for group in snapshot["bindings"]["split"]["groups"]
                  if group["partition"] == "validation" for source in group["sourceIds"]}
    if (not 1 <= len(source_ids) <= 32 or len(set(source_ids)) != len(source_ids)
            or not set(source_ids) <= validation):
        raise ValueError("Diagnostic sources must be distinct validation items, never training or test")


def phrase_durations(batch):
    alignment, tokens = batch["mel2ph"], batch["tokens"]
    if (batch["frameOffset"] != 0 or len(alignment) != batch["phraseAnalysisFrames"]
            or not alignment or any(type(x) is not int or not 1 <= x <= len(tokens) for x in alignment)
            or alignment != sorted(alignment)):
        raise ValueError("Reconstruction requires a complete ordered phrase")
    return [alignment.count(index + 1) for index in range(len(tokens))]


def mel_metrics(predicted, target, pause_mask):
    import numpy as np
    predicted, target = np.asarray(predicted), np.asarray(target)
    pause_mask = np.asarray(pause_mask)
    if (target.ndim != 2 or not target.size or predicted.shape != target.shape
            or pause_mask.dtype != np.bool_ or pause_mask.shape != (target.shape[0],)
            or not np.isfinite(predicted).all() or not np.isfinite(target).all()):
        raise ValueError("Acoustic diagnostic requires equal finite TF matrices and a frame mask")
    error = predicted.astype(np.float64) - target.astype(np.float64)
    return dict(frames=target.shape[0], bins=target.shape[1],
                meanAbsoluteError=float(np.abs(error).mean()),
                rootMeanSquareError=float(np.sqrt(np.square(error).mean())),
                pauseFrames=int(pause_mask.sum()),
                pauseMeanAbsoluteError=float(np.abs(error[pause_mask]).mean()) if pause_mask.any() else None,
                nonPauseMeanAbsoluteError=float(np.abs(error[~pause_mask]).mean()) if (~pause_mask).any() else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("checkpoint", "profile", "dataset-config", "source-root", "conditioning",
                 "targets", "trusted-checkout", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    for name in ("receipt-sha256", "profile-sha256", "dataset-sha256", "targets-sha256",
                 "rights-policy-sha256", "label-policy-sha256"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--source-id", action="append", required=True)
    parser.add_argument("--steps", type=int, default=32)
    parser.add_argument("--seed", type=int, default=937)
    args = parser.parse_args()
    try:
        if (not 1 <= args.steps <= 1000 or not 0 <= args.seed < 2**32
                or not 1 <= len(args.source_id) <= 32 or len(set(args.source_id)) != len(args.source_id)):
            raise ValueError("Select 1..32 distinct validation sources and bounded sampling settings")
        if args.output.exists() or args.output.is_symlink() or not args.output.parent.is_dir():
            raise ValueError("Diagnostic output must be new")
        profile = load_config(args.profile, args.profile_sha256)
        state, receipt = load_local_checkpoint(args.checkpoint, receipt_sha256=args.receipt_sha256)
        configuration, vocabulary = export_identity(state, profile)
        run = receipt["metadata"]["run"]
        if (run["assemblyConfigurationSha256"] != args.dataset_sha256
                or run["targetInventorySha256"] != args.targets_sha256):
            raise ValueError("Evaluation inputs differ from checkpoint training identities")
        inputs = load_dataset_inputs(args.dataset_config, args.dataset_sha256, args.source_root,
            rights_anchor=args.rights_policy_sha256, label_anchor=args.label_policy_sha256)
        snapshot = assemble_dataset(**inputs, now=int(time.time()),
            conditioning_directory=args.conditioning, reuse_conditioning=True)
        if (snapshot["preparationIssues"] or snapshot["datasetSha256"] != receipt["metadata"]["datasetSha256"]
                or snapshot["vocabulary"] != vocabulary):
            raise ValueError("Fresh dataset admission differs from trained checkpoint")
        require_validation_sources(snapshot, args.source_id)
        targets, profile_hash = load_targets(args.targets, args.targets_sha256)
        checkout = args.trusted_checkout.resolve(strict=True)
        def git(*values):
            return subprocess.check_output(["git", "-C", str(checkout), *values], text=True, timeout=10).strip()
        if git("rev-parse", "HEAD") != REVISION or git("status", "--porcelain"):
            raise ValueError("Evaluation requires the clean pinned trusted checkout")
        import torch
        from .export_adapter import prepare_acoustic_export
        from .diffusion_export_wrapper import DiffusionExportWrapper
        sys.path.insert(0, str(checkout))
        from utils.hparams import hparams
        hparams.clear()
        hparams.update(configuration)
        from modules.toplevel import DiffSingerAcoustic
        torch.set_num_threads(1)
        model = DiffSingerAcoustic(vocab_size=len(vocabulary) + 1, out_dims=profile["bins"])
        model.load_state_dict(state["model"], strict=True)
        deployment = prepare_acoustic_export(model, configuration=configuration, acoustic_profile=profile)
        sampler = DiffusionExportWrapper(deployment.diffusion).eval()
        results = []
        with torch.no_grad():
            for batch in iter_supervised_batches(snapshot, args.conditioning, targets,
                    expected_profile_sha256=profile_hash, partition="validation", batch_frames=4096):
                source = batch["sourceId"]
                if source not in args.source_id:
                    continue
                durations = phrase_durations(batch)
                variances = {}
                if configuration.get("use_breathiness_embed", False):
                    variances["breathiness"] = torch.tensor([batch["columns"]["breathiness"]], dtype=torch.float32)
                condition = deployment.forward_fs2_aux(
                    torch.tensor([batch["tokens"]], dtype=torch.long),
                    torch.tensor([durations], dtype=torch.long),
                    torch.tensor([batch["columns"]["f0Hz"]], dtype=torch.float32), variances=variances)
                # Stable per-source seed makes selection order irrelevant.
                seed = (args.seed + int(hashlib.sha256(source.encode()).hexdigest()[:8], 16)) % 2**32
                torch.manual_seed(seed)
                predicted = sampler(condition, args.steps)[0].numpy()
                pauses = [vocabulary[batch["tokens"][phone - 1] - 1] in ("pau", "SP", "sil")
                          for phone in batch["mel2ph"]]
                results.append(dict(sourceId=source, seed=seed, targetSha256=batch["targetSha256"],
                                    **mel_metrics(predicted, batch["melTargets"], pauses)))
        if {row["sourceId"] for row in results} != set(args.source_id) or len(results) != len(args.source_id):
            raise ValueError("Selected validation phrases were not covered exactly once")
        if time.time() >= snapshot["expiresAt"]:
            raise ValueError("Dataset review expired during evaluation")
        report = dict(formatId="com.project-seam.acoustic-reconstruction", schemaVersion=1,
            checkpointReceiptSha256=args.receipt_sha256, checkpointSha256=receipt["checkpointSha256"],
            datasetSha256=snapshot["datasetSha256"], targetsSha256=args.targets_sha256,
            profileSha256=profile_hash, upstreamRevision=REVISION, torchVersion=str(torch.__version__),
            partition="validation", steps=args.steps, seed=args.seed, items=results,
            conditioning="captured-label-alignment-and-measured-f0", sampler="bounded-clean-deployment-torch",
            unit="natural-log-mel-amplitude", vocoderEvaluated=False, onnxRuntimeEvaluated=False,
            singerQualified=False, releaseEligible=False)
        publish_new(args.output, report)
        print(json.dumps(report, sort_keys=True))
        return 0
    except (ValueError, OSError, RuntimeError, ImportError, KeyError, subprocess.SubprocessError) as error:
        print(str(error)[:512], file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
