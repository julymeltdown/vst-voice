"""Frozen-weight free-running 10-step sampler trace for the flatness pair.

READ-ONLY diagnostic: runs the production DiffusionExportWrapper DDIM
trajectory on the two e9 checkpoints over three admitted training phrases,
first proving the instrumented loop reproduces the uninstrumented wrapper
bit-for-bit under the same RNG, then recording per-step clean-estimate
saturation, flatness/level error and next-state summaries, plus a local
hard-clamped-auxiliary gradient-support comparison on the captured x_t.

No optimizer step, weight change, sampler/clamp change, checkpoint write, or
sweep. This is a PyTorch export-wrapper diagnostic; it does not claim ONNX
bitwise parity.
"""
import argparse
import hashlib
import json
import math
import statistics
import subprocess
import sys
import time
from pathlib import Path

from .__main__ import assemble_dataset, load_config, load_dataset_inputs, publish_new
from .batches import iter_supervised_batches
from .checkpoint import load_local_checkpoint
from .train import REVISION, load_targets, model_settings

PHRASES = ("procedural-song-00001", "procedural-song-00002", "procedural-song-00006")
SEED = 933
STEPS = 10
TIME_CAP_SECONDS = 900
CLAMP_MIN, CLAMP_MAX = -1.0, 1.0
SILENT_LEVEL_FLOOR = -11.5


def _sha(b):
    return hashlib.sha256(b).hexdigest()


def _step_tensors(batch, device):
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
    inputs["tokens"] = torch.tensor([batch["tokens"]], dtype=torch.int64, device=device)
    inputs["mel2ph"] = torch.tensor([batch["mel2ph"]], dtype=torch.int64, device=device)
    inputs["durations"] = torch.tensor(
        [[batch["mel2ph"].count(i + 1) for i in range(len(batch["tokens"]))]],
        dtype=torch.int64, device=device)
    expected = torch.tensor(target.copy(), dtype=torch.float32, device=device)[None, :, :]
    weights = torch.tensor(columns["validSamples"], dtype=torch.float32, device=device)[None, :, None] / batch["hopSize"]
    weights *= torch.tensor(loss_mask, dtype=torch.float32, device=device)[None, :, None]
    return inputs, expected, weights


def _log_flat(z):
    import torch
    m = z.shape[-1]
    return z.mean(dim=-1) - (torch.logsumexp(z, dim=-1) - math.log(m))


def _level(z):
    import torch
    m = z.shape[-1]
    return torch.logsumexp(z, dim=-1) - math.log(m)


def _denorm(diffusion, x):
    """x [1,1,M,T] -> log-mel [1,T,M]."""
    return diffusion.denorm_spec(x)[:, 0].transpose(1, 2)


def _instrumented_step_ddim(diffusion, x, t, interval, condition):
    """Copy of DiffusionExportWrapper._step_ddim that also returns internals."""
    import torch
    a_t = diffusion.alphas_cumprod[t].reshape(1, 1, 1, 1)
    a_prev = diffusion.alphas_cumprod[
        torch.clamp(t - interval, min=0)].reshape(1, 1, 1, 1)
    noise_pred = diffusion.denoise_fn(x, t, cond=condition)
    clean_pre = (x - (1. - a_t).sqrt() * noise_pred) / a_t.sqrt()
    clean_post = torch.clamp(clean_pre, CLAMP_MIN, CLAMP_MAX)
    noise_red = (x - a_t.sqrt() * clean_post) / (1. - a_t).sqrt()
    x_next = a_prev.sqrt() * clean_post + (1. - a_prev).sqrt() * noise_red
    return x_next, noise_pred, clean_pre, clean_post, noise_red


def _aux_scalar(diffusion, x_t, t, condition, ref_mel, elig_uv, weights, clamp):
    """Auxiliary flatness+level scalar on the clean estimate of a captured x_t.

    clamp=False supervises the unbounded clean estimate (current objective);
    clamp=True supervises the hard-clamped estimate, whose zero derivative in
    saturated regions removes the restoring gradient - the support loss this
    probe measures.
    """
    import torch
    import torch.nn.functional as F
    noise_pred = diffusion.denoise_fn(x_t, t, cond=condition)
    x0 = diffusion.predict_start_from_noise(x_t, t=t, noise=noise_pred)
    if clamp:
        x0 = torch.clamp(x0, CLAMP_MIN, CLAMP_MAX)
    zh = _denorm(diffusion, x0)
    bins = ref_mel.shape[-1]
    flat = F.smooth_l1_loss(_log_flat(zh), _log_flat(ref_mel)[None],
                          reduction="none")[:, :, None].expand(-1, -1, bins)
    level = F.smooth_l1_loss(_level(zh), _level(ref_mel)[None],
                             reduction="none")[:, :, None].expand(-1, -1, bins)
    mask = elig_uv[None, :, None].to(zh.dtype)
    alpha_bar = diffusion.alphas_cumprod[t].view(-1, 1, 1)
    aux = (flat + level) * mask * alpha_bar
    return (aux * weights).sum() / (weights.sum() * bins)


def _grad_norm(params, loss):
    import torch
    grads = torch.autograd.grad(loss, params, retain_graph=False, allow_unused=True)
    vec = torch.cat([g.reshape(-1) if g is not None
                     else torch.zeros(p.numel(), device=loss.device)
                     for g, p in zip(grads, params)]).double()
    return vec


def _trace(diffusion, condition, steps, seed, ref_mel, elig_uv, elig_v, weights):
    """Instrumented DDIM trajectory; returns per-step records and final latent."""
    import torch
    condition = condition.transpose(1, 2)
    torch.manual_seed(seed)
    x = torch.randn((1, diffusion.num_feats, diffusion.out_dims, condition.shape[2]),
                    device=condition.device)
    init_noise_sha = _sha(x.detach().cpu().numpy().tobytes())
    speedup = max(1, diffusion.timesteps // steps)
    speedup = int(diffusion.timestep_factors[
        torch.sum(diffusion.timestep_factors <= speedup) - 1])
    step_range = torch.arange(0, diffusion.k_step, speedup, dtype=torch.long,
                              device=condition.device).flip(0)[:, None]
    records = []
    for t in step_range:
        x_next, noise_pred, clean_pre, clean_post, noise_red = \
            _instrumented_step_ddim(diffusion, x, t, speedup, condition)
        ti = int(t.item())
        zh_pre = _denorm(diffusion, clean_pre)
        zh_post = _denorm(diffusion, clean_post)
        def _err(z, mask):
            if not mask.any():
                return None
            return float(((_log_flat(z)[0] - _log_flat(ref_mel))[mask]).abs().mean())
        def _sat(clean, mask):
            if not mask.any():
                return dict(low=None, high=None)
            sel = clean[0, 0].transpose(0, 1)[mask]  # [frames, bins]
            return dict(low=float((sel <= CLAMP_MIN).float().mean()),
                        high=float((sel >= CLAMP_MAX).float().mean()))
        records.append(dict(
            timestep=ti, alphaBar=float(diffusion.alphas_cumprod[ti]),
            satAll=float(((clean_pre <= CLAMP_MIN) | (clean_pre >= CLAMP_MAX)).float().mean()),
            satUv=_sat(clean_pre, elig_uv), satV=_sat(clean_pre, elig_v),
            uvFlatErrPre=_err(zh_pre, elig_uv), uvFlatErrPost=_err(zh_post, elig_uv),
            uvLevelErrPre=(float(((_level(zh_pre)[0] - _level(ref_mel))[elig_uv]).abs().mean())
                           if elig_uv.any() else None),
            uvLevelErrPost=(float(((_level(zh_post)[0] - _level(ref_mel))[elig_uv]).abs().mean())
                            if elig_uv.any() else None),
            nextStateMean=float(x_next.mean()), nextStateStd=float(x_next.std()),
            nextStateSha256=_sha(x_next.detach().cpu().numpy().tobytes())))
        # Hard-clamped auxiliary gradient-support comparison on captured x_t.
        denoise_params = [p for p in diffusion.denoise_fn.parameters() if p.requires_grad]
        xg = x.detach()
        aux_unc = _aux_scalar(diffusion, xg, t, condition, ref_mel, elig_uv, weights, False)
        g_unc = _grad_norm(denoise_params, aux_unc)
        aux_c = _aux_scalar(diffusion, xg, t, condition, ref_mel, elig_uv, weights, True)
        g_c = _grad_norm(denoise_params, aux_c)
        nu, nc = float(g_unc.norm()), float(g_c.norm())
        cos = (float((g_unc * g_c).sum() / (nu * nc)) if nu and nc else None)
        records[-1].update(clampGradSupportRatio=(nc / nu if nu else None),
                           clampGradCosine=cos,
                           auxUnclamped=float(aux_unc), auxClamped=float(aux_c))
        x = x_next
    return records, x, init_noise_sha, speedup


def _uninstrumented(wrapper, condition, steps, seed):
    import torch
    torch.manual_seed(seed)
    return wrapper.sample_latent(condition, steps)


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
        raise SystemExit("Requires clean pinned trusted checkout")
    import torch
    import numpy as np
    sys.path.insert(0, str(checkout))
    from utils.hparams import hparams
    hparams.clear(); hparams.update(model_settings(settings))
    from modules.toplevel import DiffSingerAcoustic
    from .export_adapter import prepare_acoustic_export
    from .diffusion_export_wrapper import DiffusionExportWrapper
    torch.set_num_threads(1)
    snapshot = assemble_dataset(**inputs_config, now=int(time.time()),
        conditioning_directory=args.conditioning, reuse_conditioning=True)
    if snapshot.get("preparationIssues"):
        raise SystemExit("Dataset admission issues")
    phrase_batches = {}
    for batch in iter_supervised_batches(snapshot, args.conditioning, targets,
            expected_profile_sha256=profile, partition="train", batch_frames=4096,
            context_frames=0):
        if batch["sourceId"] in PHRASES and batch["sourceId"] not in phrase_batches:
            phrase_batches[batch["sourceId"]] = batch
        if len(phrase_batches) == len(PHRASES):
            break
    checkpoints = {
        "control": ("/Users/lhs/seam-corpus-pauses-2026-09-19-r1/acoustic-flatness-control-e9-r1",
                    "fc73c90f6f451fb686b9d9a8163147d212abe8986430d59959541bb00812c5c4"),
        "treatment": ("/Users/lhs/seam-corpus-pauses-2026-09-19-r1/acoustic-flatness-unvoiced-e9-r1",
                      "a40f2383b68e1a2d62741f6170e4dccf360da72b07d2f7d8e95d0e633603dc76"),
    }
    # Real acoustic profile from the captured target inventory.
    profile_obj = next(iter(targets.values()))[0]["profile"]
    report = dict(formatId="com.project-seam.acoustic-sampler-trace", schemaVersion=1,
                  steps=STEPS, seed=SEED, phrases=list(PHRASES), lambdaWeight=lam,
                  readOnly=True, evalMode=True,
                  note="PyTorch export-wrapper DDIM trace; not ONNX bitwise parity; not the AdamW update",
                  singerQualified=False, releaseEligible=False, checkpoints={})
    for ckpt_name, (ckpt_dir, receipt_sha) in checkpoints.items():
        model = DiffSingerAcoustic(vocab_size=len(vocabulary) + 1, out_dims=80)
        state, receipt = load_local_checkpoint(Path(ckpt_dir), receipt_sha256=receipt_sha)
        model.load_state_dict(state["model"], strict=True)
        model.eval()
        param_hash = _sha(b"".join(p.detach().cpu().numpy().tobytes()
                                   for p in model.parameters()))
        deployment = prepare_acoustic_export(model, configuration=model_settings(settings),
                                             acoustic_profile=profile_obj)
        wrapper = DiffusionExportWrapper(deployment.diffusion).eval()
        ckpt_phrases = []
        for phrase in PHRASES:
            if time.monotonic() > deadline:
                report["partial"] = True
                publish_new(args.output / "sampler-trace.json", report)
                print(json.dumps(dict(partial=True))); return 0
            batch = phrase_batches[phrase]
            inputs, expected, weights = _step_tensors(batch, model_diff_device(model))
            ref_mel = expected[0]  # [T,M] reference log-mel
            level_ref = _level(ref_mel)
            phone_ids = inputs["phoneIds"][0]
            rest = inputs["rest"][0]
            unvoiced = torch.isin(phone_ids, torch.tensor(unvoiced_ids,
                dtype=phone_ids.dtype, device=phone_ids.device)) & ~rest
            silent = level_ref <= SILENT_LEVEL_FLOOR
            elig_uv = unvoiced & ~silent
            elig_v = (~unvoiced) & (~rest) & (~silent)
            condition = deployment.forward_fs2_aux(
                inputs["tokens"], inputs["durations"] if "durations" in inputs else None,
                inputs["f0Hz"], variances={"breathiness": inputs["breathiness"]})
            # Reproduction check: instrumented loop must equal the wrapper.
            with torch.no_grad():
                ref_latent = _uninstrumented(wrapper, condition, STEPS, SEED)
            records, traced_latent, init_sha, speedup = _trace(
                deployment.diffusion, condition, STEPS, SEED, ref_mel, elig_uv, elig_v, weights)
            repro = bool(torch.equal(ref_latent, traced_latent))
            ckpt_phrases.append(dict(phrase=phrase, speedup=speedup,
                initNoiseSha256=init_sha, reproducesWrapper=repro,
                steps=records))
        report["checkpoints"][ckpt_name] = dict(
            receiptSha256=receipt_sha, checkpointSha256=receipt["checkpointSha256"],
            parameterHash=param_hash, phrases=ckpt_phrases)
        del model, deployment, wrapper
    publish_new(args.output / "sampler-trace.json", report)
    print(json.dumps(dict(done=True)))
    return 0


def model_diff_device(model):
    return next(model.parameters()).device


if __name__ == "__main__":
    raise SystemExit(main())
