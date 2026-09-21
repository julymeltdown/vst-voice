"""Bounded gradient-only diagnostic for the flatness+level auxiliary.

READ-ONLY: loads three frozen checkpoints (e8 parent, control, treatment),
runs a teacher-forced DDPM forward on fixed training phrases at fixed
timesteps with identical explicit noise, and reports per-component loss and
gradient structure plus the effect of the export-time clean-latent clamp.
No optimizer step, parameter/buffer update, training, checkpoint write or
objective change. Eval-mode, single process, CPU, cooperative time cap.

This measures instantaneous teacher-forced gradients and a clamp
counterfactual; it is NOT the accumulated AdamW update and NOT free-running
sampler evidence.
"""
import argparse
import hashlib
import json
import math
import subprocess
import sys
import time
from pathlib import Path

from .__main__ import assemble_dataset, load_config, load_dataset_inputs, publish_new
from .batches import iter_supervised_batches
from .checkpoint import load_local_checkpoint
from .train import REVISION, load_targets, model_settings

PHRASES = ("procedural-song-00001", "procedural-song-00002", "procedural-song-00006")
TIMESTEPS = (0, 495, 999)
SEED = 933
TIME_CAP_SECONDS = 900

# Predeclared parameter groups for per-group gradient analysis.
PARAMETER_GROUPS = ("encoder", "diffusion.denoise_fn", "diffusion")


def _step_tensors(batch, device):
    """Identical tensor construction to the training/calibration step."""
    import torch
    target = batch["melTargets"]
    count = target.shape[0]
    loss_mask = batch.get("lossMask", [True] * count)
    columns = batch["columns"]
    inputs = {name: torch.tensor([columns[key]], dtype=dtype, device=device) for name, key, dtype in
              (("phoneIds", "phoneId", torch.int64), ("f0Hz", "f0Hz", torch.float32),
               ("voiced", "voiced", torch.bool), ("rest", "rest", torch.bool),
               ("slur", "slur", torch.bool))}
    inputs["breathiness"] = torch.tensor([columns["breathiness"]], dtype=torch.float32, device=device)
    inputs["midi"] = torch.tensor([[0 if v is None else v for v in columns["midi"]]],
                                  dtype=torch.int64, device=device)
    inputs["tokens"] = torch.tensor([batch["tokens"]], dtype=torch.int64, device=device)
    inputs["mel2ph"] = torch.tensor([batch["mel2ph"]], dtype=torch.int64, device=device)
    expected = torch.tensor(target.copy(), dtype=torch.float32, device=device)[None, :, :]
    weights = torch.tensor(columns["validSamples"], dtype=torch.float32, device=device)[None, :, None] / batch["hopSize"]
    weights *= torch.tensor(loss_mask, dtype=torch.float32, device=device)[None, :, None]
    return inputs, expected, weights


def _forward(model, inputs, target, t, noise):
    """Replicate _ddpm_forward with an explicit timestep and noise tensor."""
    import torch
    diffusion = model.diffusion
    use_breathiness = getattr(model.fs2, "use_breathiness_embed", False)
    condition = model.fs2(inputs["tokens"], mel2ph=inputs["mel2ph"], f0=inputs["f0Hz"],
                          **({"breathiness": inputs["breathiness"]} if use_breathiness else {}))
    cond = condition.transpose(1, 2)
    spec = diffusion.norm_spec(target).transpose(-2, -1)
    if diffusion.num_feats == 1:
        spec = spec[:, None, :, :]
    x_t = diffusion.q_sample(x_start=spec, t=t, noise=noise)
    predicted = diffusion.denoise_fn(x_t, t, cond)
    return diffusion, spec, x_t, predicted


def _components(model, inputs, target, t, noise, unvoiced_ids, loss_type):
    """Return (base, flatness, level, extras) per-element [1,T,M] losses."""
    import torch
    import torch.nn.functional as F
    diffusion, spec, x_t, predicted = _forward(model, inputs, target, t, noise)
    x0 = diffusion.predict_start_from_noise(x_t, t=t, noise=predicted)
    z_hat = diffusion.denorm_spec(x0)[:, 0].transpose(1, 2)
    z_ref = target
    bins = target.shape[2]
    error = predicted - noise
    base = (error.abs() if loss_type == "l1" else error.square())[:, 0].transpose(1, 2)
    flat_hat = z_hat.mean(dim=2, keepdim=True) - (
        torch.logsumexp(z_hat, dim=2, keepdim=True) - math.log(bins))
    flat_ref = z_ref.mean(dim=2, keepdim=True) - (
        torch.logsumexp(z_ref, dim=2, keepdim=True) - math.log(bins))
    flatness = F.smooth_l1_loss(flat_hat, flat_ref, reduction="none").expand(-1, -1, bins)
    level_hat = torch.logsumexp(z_hat, dim=2, keepdim=True) - math.log(bins)
    level_ref = torch.logsumexp(z_ref, dim=2, keepdim=True) - math.log(bins)
    level = F.smooth_l1_loss(level_hat, level_ref, reduction="none").expand_as(flatness)
    unvoiced = torch.isin(inputs["phoneIds"], torch.tensor(
        unvoiced_ids, dtype=inputs["phoneIds"].dtype, device=target.device)) & ~inputs["rest"]
    silent = (level_ref.squeeze(2) <= -11.5)
    eligible = unvoiced & ~silent
    mask = eligible[:, :, None].to(target.dtype)
    alpha_bar = diffusion.alphas_cumprod[t].view(-1, 1, 1)
    flat_term = flatness * mask * alpha_bar
    level_term = level * mask * alpha_bar
    extras = dict(diffusion=diffusion, spec=spec, x_t=x_t, predicted=predicted,
                  x0=x0, z_hat=z_hat, z_ref=z_ref, eligible=eligible,
                  voiced=(~unvoiced) & (~inputs["rest"]) & (~silent))
    return base, flat_term, level_term, extras


def _scalar(elem, weights, bins):
    return (elem * weights).sum() / (weights.sum() * bins)


def _grad(parameters, loss):
    import torch
    grads = torch.autograd.grad(loss, parameters, retain_graph=True, allow_unused=True)
    flat = torch.cat([g.reshape(-1) for g in grads if g is not None]) if any(
        g is not None for g in grads) else None
    return grads, flat


def _norm(vec):
    return None if vec is None else float(vec.norm())


def _cosine(a, b):
    if a is None or b is None:
        return None
    na, nb = float(a.norm()), float(b.norm())
    if na == 0 or nb == 0:
        return None  # zero-norm cosine is undefined
    return float((a * b).sum() / (na * nb))


def _group_grads(model, parameters, loss):
    """Per-parameter-group gradient norms for predeclared groups."""
    import torch
    named = dict(model.named_parameters())
    result = {}
    for group in PARAMETER_GROUPS:
        params = [p for n, p in named.items() if n.startswith(group) and p.requires_grad]
        if not params:
            continue
        grads = torch.autograd.grad(loss, params, retain_graph=True, allow_unused=True)
        total = sum(float(g.square().sum()) for g in grads if g is not None)
        result[group] = math.sqrt(total)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for n in ("training-config", "dataset-config", "targets", "source-root", "conditioning",
              "trusted-checkout", "output"):
        ap.add_argument("--" + n, type=Path, required=True)
    for n in ("training-sha256", "dataset-sha256", "targets-sha256",
              "rights-policy-sha256", "label-policy-sha256"):
        ap.add_argument("--" + n, required=True)
    args = ap.parse_args()
    if args.output.exists():
        raise SystemExit("Output must be new")
    args.output.mkdir(mode=0o700)
    deadline = time.monotonic() + TIME_CAP_SECONDS
    settings = load_config(args.training_config, args.training_sha256)
    aux = settings["auxiliaryObjective"]
    lam = float(aux["weight"])
    inputs_config = load_dataset_inputs(args.dataset_config, args.dataset_sha256,
        args.source_root, rights_anchor=args.rights_policy_sha256,
        label_anchor=args.label_policy_sha256)
    targets, profile = load_targets(args.targets, args.targets_sha256)
    labels = load_config(inputs_config["label_config"], inputs_config["label_hash"])
    vocabulary = labels["vocabulary"]
    token_ids = {s: i + 1 for i, s in enumerate(vocabulary)}
    unvoiced_ids = [token_ids[s] for s in aux["unvoicedSymbols"]]
    checkout = args.trusted_checkout.resolve(strict=True)
    def git(*a):
        return subprocess.check_output(["git", "-C", str(checkout), *a], text=True, timeout=10).strip()
    if git("rev-parse", "HEAD") != REVISION or git("status", "--porcelain"):
        raise SystemExit("Probe requires the clean pinned trusted checkout")
    import torch
    import numpy as np
    sys.path.insert(0, str(checkout))
    from utils.hparams import hparams
    hparams.clear(); hparams.update(model_settings(settings))
    from modules.toplevel import DiffSingerAcoustic
    torch.set_num_threads(1)
    snapshot = assemble_dataset(**inputs_config, now=int(time.time()),
        conditioning_directory=args.conditioning, reuse_conditioning=True)
    if snapshot.get("preparationIssues"):
        raise SystemExit("Dataset admission issues")
    # Collect the three fixed training phrases once.
    phrase_batches = {}
    for batch in iter_supervised_batches(snapshot, args.conditioning, targets,
            expected_profile_sha256=profile, partition="train", batch_frames=4096,
            context_frames=0):
        if batch["sourceId"] in PHRASES and batch["sourceId"] not in phrase_batches:
            phrase_batches[batch["sourceId"]] = batch
        if len(phrase_batches) == len(PHRASES):
            break
    missing = [p for p in PHRASES if p not in phrase_batches]
    if missing:
        raise SystemExit(f"Missing training phrases: {missing}")
    checkpoints = {
        "e8-parent": ("/Users/lhs/seam-corpus-pauses-2026-09-19-r1/acoustic-breathiness-r1/epoch-000008",
                      "04f72263b700804cc4557879208def3ca4157d68955dea516b179140f755007e"),
        "control": ("/Users/lhs/seam-corpus-pauses-2026-09-19-r1/acoustic-flatness-control-e9-r1",
                    "fc73c90f6f451fb686b9d9a8163147d212abe8986430d59959541bb00812c5c4"),
        "treatment": ("/Users/lhs/seam-corpus-pauses-2026-09-19-r1/acoustic-flatness-unvoiced-e9-r1",
                      "a40f2383b68e1a2d62741f6170e4dccf360da72b07d2f7d8e95d0e633603dc76"),
    }
    report = dict(formatId="com.project-seam.acoustic-gradient-probe", schemaVersion=1,
                  seed=SEED, timesteps=list(TIMESTEPS), phrases=list(PHRASES),
                  lambdaWeight=lam, readOnly=True, evalMode=True,
                  note="instantaneous teacher-forced gradients; not the AdamW update; clamp counterfactual is not free-running sampler evidence",
                  singerQualified=False, releaseEligible=False, checkpoints={})
    bins = None
    for ckpt_name, (ckpt_dir, receipt_sha) in checkpoints.items():
        model = DiffSingerAcoustic(vocab_size=len(vocabulary) + 1, out_dims=80)
        state, receipt = load_local_checkpoint(Path(ckpt_dir), receipt_sha256=receipt_sha)
        model.load_state_dict(state["model"], strict=True)
        model.eval()
        # Hash model state before and after to prove read-only.
        before = hashlib.sha256(b"".join(
            p.detach().cpu().numpy().tobytes() for p in model.parameters())).hexdigest()
        parameters = [p for p in model.parameters() if p.requires_grad]
        device = parameters[0].device
        ckpt_rows = []
        for phrase in PHRASES:
            batch = phrase_batches[phrase]
            inputs, expected, weights = _step_tensors(batch, device)
            bins = expected.shape[2]
            for ts in TIMESTEPS:
                if time.monotonic() > deadline:
                    report["partial"] = True
                    publish_new(args.output / "gradient-probe.json", report)
                    print(json.dumps(dict(partial=True))); return 0
                t = torch.full((1,), ts, dtype=torch.long, device=device)
                torch.manual_seed(SEED)  # identical noise across checkpoints
                noise = torch.randn_like(
                    model.diffusion.norm_spec(expected).transpose(-2, -1)[:, None, :, :])
                base_e, flat_e, level_e, ex = _components(
                    model, inputs, expected, t, noise, unvoiced_ids, settings["loss"])
                base_s = _scalar(base_e, weights, bins)
                flat_s = _scalar(flat_e, weights, bins)
                level_s = _scalar(level_e, weights, bins)
                aux_s = flat_s + level_s
                combined_s = base_s + lam * aux_s
                # Reconstruction check: combined objective == base + lam*(flat+level)
                # and weight-0 == base.
                recon_ok = bool(torch.isclose(combined_s,
                    _scalar(base_e + lam * (flat_e + level_e), weights, bins), atol=1e-6))
                w0_ok = bool(torch.isclose(base_s,
                    _scalar(base_e + 0.0 * (flat_e + level_e), weights, bins), atol=1e-9))
                _, g_base = _grad(parameters, base_s)
                _, g_flat = _grad(parameters, flat_s)
                _, g_level = _grad(parameters, level_s)
                _, g_comb = _grad(parameters, combined_s)
                # Combined should equal g_base + lam*(g_flat+g_level)
                recon_grad = (g_base + lam * (g_flat + g_level)) if all(
                    g is not None for g in (g_base, g_flat, g_level)) else None
                comb_cos = _cosine(g_comb, recon_grad)
                groups = _group_grads(model, parameters, combined_s)
                # Clamp counterfactual on the clean estimate.
                x0 = ex["x0"]
                sat = float(((x0.abs() >= 1.0)).float().mean())
                x0c = torch.clamp(x0, -1.0, 1.0)
                zh = ex["z_hat"]; zr = ex["z_ref"]
                zh_c = model.diffusion.denorm_spec(x0c)[:, 0].transpose(1, 2)
                elig = ex["eligible"]; voiced = ex["voiced"]
                def _fl(z):
                    return z.mean(dim=2, keepdim=True) - (
                        torch.logsumexp(z, dim=2, keepdim=True) - math.log(bins))
                uv_err_pre = float((_fl(zh) - _fl(zr)).abs()[elig].mean()) if elig.any() else None
                uv_err_post = float((_fl(zh_c) - _fl(zr)).abs()[elig].mean()) if elig.any() else None
                ckpt_rows.append(dict(phrase=phrase, timestep=ts,
                    alphaBar=float(ex["diffusion"].alphas_cumprod[ts]),
                    baseLoss=float(base_s), flatnessLoss=float(flat_s),
                    levelLoss=float(level_s), auxLoss=float(aux_s),
                    combinedLoss=float(combined_s),
                    reconstructionMatches=recon_ok, weight0MatchesBase=w0_ok,
                    baseGradNorm=_norm(g_base), flatGradNorm=_norm(g_flat),
                    levelGradNorm=_norm(g_level), combinedGradNorm=_norm(g_comb),
                    auxToBaseRatio=(_norm(g_flat + g_level) / _norm(g_base)
                                    if g_base is not None and g_flat is not None
                                    and g_level is not None and _norm(g_base) else None),
                    lambdaAuxToBase=(lam * _norm(g_flat + g_level) / _norm(g_base)
                                     if g_base is not None and g_flat is not None
                                     and g_level is not None and _norm(g_base) else None),
                    cosBaseFlat=_cosine(g_base, g_flat),
                    cosBaseLevel=_cosine(g_base, g_level),
                    cosFlatLevel=_cosine(g_flat, g_level),
                    cosCombinedVsReconstructed=comb_cos,
                    groupGradNorms=groups,
                    cleanSaturationFraction=sat,
                    uvLogFlatErrPreClamp=uv_err_pre,
                    uvLogFlatErrPostClamp=uv_err_post))
        after = hashlib.sha256(b"".join(
            p.detach().cpu().numpy().tobytes() for p in model.parameters())).hexdigest()
        report["checkpoints"][ckpt_name] = dict(
            receiptSha256=receipt_sha, checkpointSha256=receipt["checkpointSha256"],
            stateHashBefore=before, stateHashAfter=after,
            stateUnchanged=before == after, rows=ckpt_rows)
        del model
    publish_new(args.output / "gradient-probe.json", report)
    print(json.dumps(dict(done=True, checkpoints=len(report["checkpoints"]))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
