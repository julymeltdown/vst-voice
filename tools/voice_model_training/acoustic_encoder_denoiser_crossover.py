"""Frozen-weight 2x2 encoder x denoiser crossover at the first DDIM step.

READ-ONLY diagnostic: at the first actual sampler timestep (t=900 under the
10-step schedule, speedup 100) with a shared initial latent, combines each
checkpoint's trained fs2 encoder (conditioning C) with each checkpoint's
denoiser (D) - four combinations per phrase - to separate whether the
first-step control/treatment gap follows encoder conditioning, denoiser
weights, or their interaction. Diagonal combinations reproduce the existing
first-step diagnostics. Hybrids are out-of-training-distribution diagnostic
interventions, not deployable checkpoints or proof of a unique cause.

No gradients, optimizer steps, parameter updates, checkpoint writes or
sampler/clamp changes. Full named state_dict (parameters + buffers) and RNG
are captured before/after each checkpoint's use.
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
SEED = 933
STEPS = 10
TIME_CAP_SECONDS = 900
CLAMP_MIN, CLAMP_MAX = -1.0, 1.0
SILENT_LEVEL_FLOOR = -11.5


def _sha(b):
    return hashlib.sha256(b).hexdigest()


def _tensor_sha(t):
    return _sha(t.detach().cpu().contiguous().numpy().tobytes())


def _state_sha(model):
    parts = []
    for name, tensor in model.state_dict().items():
        parts.append(name.encode())
        parts.append(str(tuple(tensor.shape)).encode())
        parts.append(str(tensor.dtype).encode())
        parts.append(tensor.detach().cpu().contiguous().numpy().tobytes())
    return _sha(b"".join(parts))


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
    return inputs, expected


def _log_flat(z):
    import torch
    m = z.shape[-1]
    return z.mean(dim=-1) - (torch.logsumexp(z, dim=-1) - math.log(m))


def _level(z):
    import torch
    m = z.shape[-1]
    return torch.logsumexp(z, dim=-1) - math.log(m)


def _denorm(diffusion, x):
    return diffusion.denorm_spec(x)[:, 0].transpose(1, 2)


def _metrics(clean_pre, clean_post, x_next, diffusion, ref_mel, elig_uv, elig_v):
    zh_pre = _denorm(diffusion, clean_pre)
    zh_post = _denorm(diffusion, clean_post)
    def _err(z, mask):
        if not mask.any():
            return None
        return float(((_log_flat(z)[0] - _log_flat(ref_mel))[mask]).abs().mean())
    def _lerr(z, mask):
        if not mask.any():
            return None
        return float(((_level(z)[0] - _level(ref_mel))[mask]).abs().mean())
    def _sat(clean, mask):
        if not mask.any():
            return dict(low=None, high=None, total=None)
        sel = clean[0, 0].transpose(0, 1)[mask]
        low = float((sel <= CLAMP_MIN).float().mean())
        high = float((sel >= CLAMP_MAX).float().mean())
        return dict(low=low, high=high, total=low + high)
    return dict(
        satAll=float(((clean_pre <= CLAMP_MIN) | (clean_pre >= CLAMP_MAX)).float().mean()),
        satUv=_sat(clean_pre, elig_uv), satV=_sat(clean_pre, elig_v),
        uvFlatErrPre=_err(zh_pre, elig_uv), uvFlatErrPost=_err(zh_post, elig_uv),
        uvLevelErrPre=_lerr(zh_pre, elig_uv), uvLevelErrPost=_lerr(zh_post, elig_uv),
        vFlatErrPre=_err(zh_pre, elig_v), vFlatErrPost=_err(zh_post, elig_v),
        vLevelErrPre=_lerr(zh_pre, elig_v), vLevelErrPost=_lerr(zh_post, elig_v),
        cleanPreSha256=_tensor_sha(clean_pre), cleanPostSha256=_tensor_sha(clean_post),
        xNextSha256=_tensor_sha(x_next), xNextMean=float(x_next.mean()),
        xNextStd=float(x_next.std()))


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
    sys.path.insert(0, str(checkout))
    from utils.hparams import hparams
    hparams.clear(); hparams.update(model_settings(settings))
    from modules.toplevel import DiffSingerAcoustic
    from .export_adapter import prepare_acoustic_export
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
    profile_obj = next(iter(targets.values()))[0]["profile"]
    checkpoints = {
        "control": ("/Users/lhs/seam-corpus-pauses-2026-09-19-r1/acoustic-flatness-control-e9-r1",
                    "fc73c90f6f451fb686b9d9a8163147d212abe8986430d59959541bb00812c5c4"),
        "treatment": ("/Users/lhs/seam-corpus-pauses-2026-09-19-r1/acoustic-flatness-unvoiced-e9-r1",
                      "a40f2383b68e1a2d62741f6170e4dccf360da72b07d2f7d8e95d0e633603dc76"),
    }
    # Load both deployments once; keep encoders and denoisers separable.
    deployments = {}
    state_hashes = {}
    rng_states = {}
    for name, (ckpt_dir, receipt_sha) in checkpoints.items():
        model = DiffSingerAcoustic(vocab_size=len(vocabulary) + 1, out_dims=80)
        state, receipt = load_local_checkpoint(Path(ckpt_dir), receipt_sha256=receipt_sha)
        model.load_state_dict(state["model"], strict=True)
        model.eval()
        state_hashes[name] = dict(before=_state_sha(model), after=None)
        rng_states[name] = dict(before=_tensor_sha(torch.random.get_rng_state()))
        deployments[name] = prepare_acoustic_export(
            model, configuration=model_settings(settings), acoustic_profile=profile_obj)
        deployments[name].eval()
        state_hashes[name]["trainedModel"] = model  # for post state check
    # Verify schedule/normalization interfaces match between the two deployments.
    d0, d1 = deployments["control"].diffusion, deployments["treatment"].diffusion
    for attr in ("spec_min", "spec_max", "alphas_cumprod", "timestep_factors", "k_step"):
        a, b = getattr(d0, attr, None), getattr(d1, attr, None)
        if isinstance(a, torch.Tensor):
            if not torch.equal(a, b):
                raise SystemExit(f"Deployment diffusion {attr} differs between arms")
        elif a != b:
            raise SystemExit(f"Deployment diffusion {attr} differs between arms")
    # First actual sampler step under the 10-step schedule.
    speedup = max(1, d0.timesteps // STEPS)
    speedup = int(d0.timestep_factors[torch.sum(d0.timestep_factors <= speedup) - 1])
    t900 = int(torch.arange(0, d0.k_step, speedup).flip(0)[0])
    report = dict(formatId="com.project-seam.acoustic-encoder-denoiser-crossover",
                  schemaVersion=1, firstTimestep=t900, speedup=speedup, seed=SEED,
                  phrases=list(PHRASES), readOnly=True, evalMode=True,
                  note="2x2 encoder x denoiser crossover at the first DDIM step; hybrids are out-of-distribution diagnostics, not deployable checkpoints or proof of a unique cause",
                  singerQualified=False, releaseEligible=False, phrases_out=[])
    for phrase in PHRASES:
        if time.monotonic() > deadline:
            report["partial"] = True
            publish_new(args.output / "crossover.json", report)
            print(json.dumps(dict(partial=True))); return 0
        batch = phrase_batches[phrase]
        inputs, expected = _step_tensors(batch, next(iter(
            deployments["control"].parameters())).device)
        ref_mel = expected[0]
        level_ref = _level(ref_mel)
        phone_ids = inputs["phoneIds"][0]
        rest = inputs["rest"][0]
        unvoiced = torch.isin(phone_ids, torch.tensor(unvoiced_ids,
            dtype=phone_ids.dtype, device=phone_ids.device)) & ~rest
        silent = level_ref <= SILENT_LEVEL_FLOOR
        elig_uv = unvoiced & ~silent
        elig_v = (~unvoiced) & (~rest) & (~silent)
        # Shared initial latent.
        torch.manual_seed(SEED)
        x = torch.randn((1, d0.num_feats, d0.out_dims, expected.shape[1]),
                        device=expected.device)
        init_sha = _tensor_sha(x)
        t = torch.full((1,), t900, dtype=torch.long, device=expected.device)
        combos = {}
        for enc in ("control", "treatment"):
            condition = deployments[enc].forward_fs2_aux(
                inputs["tokens"], inputs["durations"], inputs["f0Hz"],
                variances={"breathiness": inputs["breathiness"]}).transpose(1, 2)
            for den in ("control", "treatment"):
                diffusion = deployments[den].diffusion
                a_t = diffusion.alphas_cumprod[t].reshape(1, 1, 1, 1)
                a_prev = diffusion.alphas_cumprod[
                    torch.clamp(t - speedup, min=0)].reshape(1, 1, 1, 1)
                noise_pred = diffusion.denoise_fn(x, t, cond=condition)
                clean_pre = (x - (1. - a_t).sqrt() * noise_pred) / a_t.sqrt()
                clean_post = torch.clamp(clean_pre, CLAMP_MIN, CLAMP_MAX)
                noise_red = (x - a_t.sqrt() * clean_post) / (1. - a_t).sqrt()
                x_next = a_prev.sqrt() * clean_post + (1. - a_prev).sqrt() * noise_red
                key = f"C-{enc}/D-{den}"
                combos[key] = dict(
                    conditionSha256=_tensor_sha(condition),
                    noisePredSha256=_tensor_sha(noise_pred),
                    **_metrics(clean_pre, clean_post, x_next, diffusion,
                               ref_mel, elig_uv, elig_v))
        report["phrases_out"].append(dict(phrase=phrase, initNoiseSha256=init_sha,
                                         timestep=t900, combos=combos))
    for name in checkpoints:
        m = state_hashes[name].pop("trainedModel")
        state_hashes[name]["after"] = _state_sha(m)
        state_hashes[name]["unchanged"] = state_hashes[name]["before"] == state_hashes[name]["after"]
        rng_states[name]["after"] = _tensor_sha(torch.random.get_rng_state())
        rng_states[name]["note"] = "RNG advanced by shared-latent draws; not a mutation check"
    report["stateDictHashes"] = {k: {kk: vv for kk, vv in v.items()} for k, v in state_hashes.items()}
    publish_new(args.output / "crossover.json", report)
    print(json.dumps(dict(done=True)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
