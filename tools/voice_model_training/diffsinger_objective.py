"""DDPM adapters for the inspected DiffSinger training output; no model loading."""


class DiffSingerDDPMObjective:
    """Whole-phrase, non-shallow, base-conditioning pilot adapter.

    Source contract: openvpi/DiffSinger 336cf01b57f2ad44c6b37a79cf33993043291759,
    training/acoustic_task.py and modules/losses/diff_loss.py. Source inspection
    and interface tests do not prove a trained model or licensed weight bundle.
    """
    def __init__(self, loss_type: str):
        if loss_type not in ("l1", "l2"):
            raise ValueError("DDPM loss must be l1 or l2")
        self.loss_type = loss_type
        self.objective_id = f"diffsinger-ddpm-{loss_type}-whole-phrase-v2"

    def __call__(self, model, inputs, target):
        import torch
        if (getattr(model, "diffusion_type", None) != "ddpm"
                or getattr(model, "use_shallow_diffusion", None) is not False):
            raise ValueError("Adapter requires a non-shallow DDPM model")
        encoder = getattr(model, "fs2", None)
        flags = ("use_lang_id", "use_energy_embed", "use_voicing_embed",
                 "use_tension_embed", "use_key_shift_embed", "use_speed_embed", "use_spk_id")
        if any(getattr(encoder, flag, None) is not False for flag in flags):
            raise ValueError("Model requires conditioning not provided by the base DDPM adapter")
        if ("tokens" not in inputs or "mel2ph" not in inputs
                or type(inputs.get("frameOffset")) is not int or inputs["frameOffset"] != 0
                or type(inputs.get("phraseAnalysisFrames")) is not int
                or inputs["phraseAnalysisFrames"] != target.shape[1]):
            raise ValueError("DDPM duration conditioning requires the entire phrase")
        use_breathiness = getattr(encoder, "use_breathiness_embed", None)
        if use_breathiness not in (False, True):
            raise ValueError("Model breathiness embedding declaration is invalid")
        breathiness = inputs.get("breathiness")
        if (not isinstance(breathiness, torch.Tensor) or breathiness.shape != inputs["f0Hz"].shape
                or breathiness.dtype != torch.float32 or breathiness.device != target.device
                or not torch.isfinite(breathiness).all() or torch.any(breathiness < 0)
                or torch.any(breathiness > 1)):
            raise ValueError("DDPM breathiness conditioning must be finite normalized [0, 1]")
        if not use_breathiness and torch.any(breathiness != 0):
            raise ValueError("Base DDPM model cannot silently discard breathiness supervision")
        output = model(inputs["tokens"], mel2ph=inputs["mel2ph"], f0=inputs["f0Hz"],
                       gt_mel=target, infer=False,
                       **({"breathiness": breathiness} if use_breathiness else {}))
        pair = getattr(output, "diff_out", None)
        if getattr(output, "aux_out", None) is not None or not isinstance(pair, tuple) or len(pair) != 2:
            raise ValueError("Unexpected DDPM training outputs")
        predicted, noise = pair
        shape = (1, 1, target.shape[2], target.shape[1])
        if any(not isinstance(value, torch.Tensor) or tuple(value.shape) != shape
               or value.dtype != torch.float32 or value.device != target.device
               or not torch.isfinite(value).all() for value in pair) or noise.requires_grad:
            raise ValueError("Invalid DDPM noise prediction or target")
        error = predicted - noise
        loss = error.abs() if self.loss_type == "l1" else error.square()
        return loss[:, 0].transpose(1, 2)


UNVOICED_AUXILIARY_KIND = "unvoiced-clean-mel-shape-level"
UNVOICED_FLATNESS_KIND = "unvoiced-target-log-flatness"

# Captured mel targets are ln-amplitude with a real floor at ln(1e-5);
# frames whose reference log-mean amplitude sits at that floor carry no
# measurable spectral shape, so a flatness statistic on them is noise
# about a degenerate target rather than supervision. They are excluded
# from the auxiliary (the base epsilon loss still trains them).
SILENT_LEVEL_FLOOR = -11.5


def log_flatness(z):
    """Per-frame log spectral flatness of a log-mel tensor [B,T,M].

    Flatness is geometric-mean over arithmetic-mean magnitude. In log-mel
    space z, log flatness is mean(z) - (logsumexp(z) - log M) per frame.
    The result is <= 0, equals 0 only for a perfectly flat spectrum, and is
    invariant to a uniform additive shift of z (a pure level change).
    """
    import math
    import torch
    bins = z.shape[2]
    return z.mean(dim=2, keepdim=True) - (
        torch.logsumexp(z, dim=2, keepdim=True) - math.log(bins))


def objective_id_for_settings(settings: dict) -> str:
    """Objective identity for a captured training configuration's settings.

    Checkpoint lineage must compare the objective the parent actually ran, so
    identity is derived from the same settings object the run recorded.
    """
    auxiliary = settings.get("auxiliaryObjective")
    if auxiliary is None:
        return DiffSingerDDPMObjective(settings["loss"]).objective_id
    kind = auxiliary["kind"] if isinstance(auxiliary, dict) else None
    if kind not in (UNVOICED_AUXILIARY_KIND, UNVOICED_FLATNESS_KIND):
        raise ValueError("Unsupported auxiliary objective kind")
    return f"diffsinger-ddpm-{settings['loss']}-{kind}-v1"


def _validate_ddpm_contract(model, inputs, target):
    """Shared admission checks for whole-phrase non-shallow DDPM objectives."""
    import torch
    if (getattr(model, "diffusion_type", None) != "ddpm"
            or getattr(model, "use_shallow_diffusion", None) is not False):
        raise ValueError("Adapter requires a non-shallow DDPM model")
    encoder = getattr(model, "fs2", None)
    flags = ("use_lang_id", "use_energy_embed", "use_voicing_embed",
             "use_tension_embed", "use_key_shift_embed", "use_speed_embed", "use_spk_id")
    if any(getattr(encoder, flag, None) is not False for flag in flags):
        raise ValueError("Model requires conditioning not provided by the DDPM adapters")
    if ("tokens" not in inputs or "mel2ph" not in inputs
            or type(inputs.get("frameOffset")) is not int or inputs["frameOffset"] != 0
            or type(inputs.get("phraseAnalysisFrames")) is not int
            or inputs["phraseAnalysisFrames"] != target.shape[1]):
        raise ValueError("DDPM duration conditioning requires the entire phrase")
    use_breathiness = getattr(encoder, "use_breathiness_embed", None)
    if use_breathiness not in (False, True):
        raise ValueError("Model breathiness embedding declaration is invalid")
    breathiness = inputs.get("breathiness")
    if (not isinstance(breathiness, torch.Tensor) or breathiness.shape != inputs["f0Hz"].shape
            or breathiness.dtype != torch.float32 or breathiness.device != target.device
            or not torch.isfinite(breathiness).all() or torch.any(breathiness < 0)
            or torch.any(breathiness > 1)):
        raise ValueError("DDPM breathiness conditioning must be finite normalized [0, 1]")
    if not use_breathiness and torch.any(breathiness != 0):
        raise ValueError("Base DDPM model cannot silently discard breathiness supervision")
    return encoder, use_breathiness, breathiness


def _check_noise_pair(pair, target):
    import torch
    shape = (target.shape[0], 1, target.shape[2], target.shape[1])
    if any(not isinstance(value, torch.Tensor) or tuple(value.shape) != shape
           or value.dtype != torch.float32 or value.device != target.device
           or not torch.isfinite(value).all() for value in pair) or pair[1].requires_grad:
        raise ValueError("Invalid DDPM noise prediction or target")


def _ddpm_forward(model, inputs, target):
    """The identical training forward both unvoiced auxiliaries reproduce.

    Returns (batch, timesteps, noise, x_t, predicted, z_hat, z_ref, mask_inputs)
    where z_hat/z_ref are denormalized log-mels [B,T,M] and mask_inputs is the
    (phoneIds, rest) pair validated for per-frame masking.
    """
    import torch
    encoder, use_breathiness, breathiness = _validate_ddpm_contract(model, inputs, target)
    for name in ("phoneIds", "rest"):
        value = inputs.get(name)
        if (not isinstance(value, torch.Tensor) or tuple(value.shape) != tuple(target.shape[:2])
                or value.device != target.device):
            raise ValueError("Unvoiced auxiliary requires per-frame phone ids and rest flags")
    diffusion = getattr(model, "diffusion", None)
    for name in ("norm_spec", "denorm_spec", "q_sample", "denoise_fn",
                 "predict_start_from_noise", "alphas_cumprod", "k_step"):
        if not hasattr(diffusion, name):
            raise ValueError("Model diffusion module lacks the pinned DDPM interface")
    # Identical call sequence to GaussianDiffusion.forward(infer=False):
    # encoder, normalize, timestep draw, noise draw, q_sample, denoise_fn.
    condition = encoder(inputs["tokens"], mel2ph=inputs["mel2ph"], f0=inputs["f0Hz"],
                        **({"breathiness": breathiness} if use_breathiness else {}))
    cond = condition.transpose(1, 2)
    spec = diffusion.norm_spec(target).transpose(-2, -1)
    if diffusion.num_feats == 1:
        spec = spec[:, None, :, :]
    batch = target.shape[0]
    t = torch.randint(0, diffusion.k_step, (batch,), device=target.device).long()
    noise = torch.randn_like(spec)
    x_t = diffusion.q_sample(x_start=spec, t=t, noise=noise)
    predicted = diffusion.denoise_fn(x_t, t, cond)
    _check_noise_pair((predicted, noise), target)
    x0 = diffusion.predict_start_from_noise(x_t, t=t, noise=predicted)
    z_hat = diffusion.denorm_spec(x0)[:, 0].transpose(1, 2)
    return batch, t, noise, predicted, z_hat, target, (inputs["phoneIds"], inputs["rest"]), diffusion


class DiffSingerDDPMUnvoicedSpectralObjective:
    """Epsilon DDPM loss plus a clean-mel shape/level term on unvoiced frames.

    The auxiliary term reproduces the upstream training forward exactly: the
    same fs2 call, the same timestep and noise draws in the same RNG order,
    and the same q_sample/denoise_fn evaluation as GaussianDiffusion.forward.
    It then reconstructs the predicted clean spectrogram x0 from that same
    draw, denormalizes it with the checkpoint's spec_min/spec_max, and applies
    a smooth-L1 spectral-shape plus log-energy error on labeled non-silent
    unvoiced frames, weighted by the schedule value alpha_bar_t so high-noise
    timesteps do not dominate. This is an experimental repair objective, not a
    proven fix; weight zero reproduces the base objective bit for bit.
    """
    def __init__(self, loss_type: str, weight: float, unvoiced_ids):
        if loss_type not in ("l1", "l2"):
            raise ValueError("DDPM loss must be l1 or l2")
        import math
        if type(weight) not in (int, float) or not math.isfinite(weight) or not 0 <= weight <= 1:
            raise ValueError("Auxiliary weight must be a finite value in [0, 1]")
        ids = sorted({int(value) for value in unvoiced_ids})
        if (not ids or len(set(unvoiced_ids)) != len(unvoiced_ids) or len(ids) > 64
                or any(type(value) is not int or value < 1 for value in unvoiced_ids)):
            raise ValueError("Unvoiced token ids must be distinct positive integers")
        self.loss_type = loss_type
        self.weight = float(weight)
        self.unvoiced_ids = ids
        self.objective_id = f"diffsinger-ddpm-{loss_type}-{UNVOICED_AUXILIARY_KIND}-v1"
        self.last_draw = None

    def components(self, model, inputs, target):
        """Return (base, unscaled_auxiliary) per-element [B,T,M] losses and run
        the identical forward the base objective performs."""
        import hashlib
        import math
        import torch
        import torch.nn.functional as F
        batch, t, noise, predicted, z_hat, z_ref, mask_inputs, diffusion = (
            _ddpm_forward(model, inputs, target))
        error = predicted - noise
        base = error.abs() if self.loss_type == "l1" else error.square()
        base = base[:, 0].transpose(1, 2)
        bins = target.shape[2]
        level_hat = torch.logsumexp(z_hat, dim=2, keepdim=True) - math.log(bins)
        level_ref = torch.logsumexp(z_ref, dim=2, keepdim=True) - math.log(bins)
        shape = F.smooth_l1_loss(z_hat - level_hat, z_ref - level_ref, reduction="none")
        level = F.smooth_l1_loss(level_hat, level_ref, reduction="none").expand_as(shape)
        unvoiced = torch.isin(inputs["phoneIds"], torch.tensor(
            self.unvoiced_ids, dtype=inputs["phoneIds"].dtype, device=target.device)) & ~inputs["rest"]
        mask = unvoiced[:, :, None].to(target.dtype)
        alpha_bar = diffusion.alphas_cumprod[t].view(batch, 1, 1)
        auxiliary = (shape + level) * mask * alpha_bar
        self.last_draw = dict(timesteps=[int(value) for value in t],
                              noiseSha256=hashlib.sha256(
                                  noise.detach().cpu().numpy().tobytes()).hexdigest(),
                              unvoicedFrames=int(mask.sum()))
        return base, auxiliary

    def __call__(self, model, inputs, target):
        base, auxiliary = self.components(model, inputs, target)
        return base + self.weight * auxiliary


class DiffSingerDDPMUnvoicedFlatnessObjective:
    """Epsilon DDPM loss plus a target-relative log-flatness term on unvoiced
    frames, with the same log-energy level term retained for protection.

    Motivation (see INTEGRATED_SINGER_EXECUTION): on the tested configuration
    (one song, eight posterior draws, ten sampler steps), the conditioned
    acoustic model produced spectrally concentrated predictions on unvoiced
    phones - no evaluated draw reached the reference's noise-like flatness
    range. This auxiliary scores the per-frame log spectral flatness of the
    predicted clean mel against the reference's own log flatness, so the
    target is the measured statistic of the data rather than an absolute
    "flat" target that would reward white-noise spectra at any level.

    In log-mel space z (per frame, M bins), spectral flatness is
    exp(mean(z)) / mean(exp(z)), so log-flatness is
    mean(z) - (logsumexp(z) - log M). The term is a smooth-L1 on that scalar
    per frame, plus the e9 log-energy term, masked to labeled non-silent
    unvoiced frames and weighted by alpha_bar_t. The forward, timestep and
    noise draws are identical to the base objective; weight zero reproduces
    the base objective bit for bit. This is an experimental repair objective,
    not a proven fix.
    """
    def __init__(self, loss_type: str, weight: float, unvoiced_ids):
        if loss_type not in ("l1", "l2"):
            raise ValueError("DDPM loss must be l1 or l2")
        import math
        if type(weight) not in (int, float) or not math.isfinite(weight) or not 0 <= weight <= 1:
            raise ValueError("Auxiliary weight must be a finite value in [0, 1]")
        ids = sorted({int(value) for value in unvoiced_ids})
        if (not ids or len(set(unvoiced_ids)) != len(unvoiced_ids) or len(ids) > 64
                or any(type(value) is not int or value < 1 for value in unvoiced_ids)):
            raise ValueError("Unvoiced token ids must be distinct positive integers")
        self.loss_type = loss_type
        self.weight = float(weight)
        self.unvoiced_ids = ids
        self.objective_id = f"diffsinger-ddpm-{loss_type}-{UNVOICED_FLATNESS_KIND}-v1"
        self.last_draw = None

    def components(self, model, inputs, target):
        """Return (base, unscaled_auxiliary) per-element [B,T,M] losses and run
        the identical forward the base objective performs."""
        import hashlib
        import math
        import torch
        import torch.nn.functional as F
        batch, t, noise, predicted, z_hat, z_ref, mask_inputs, diffusion = (
            _ddpm_forward(model, inputs, target))
        error = predicted - noise
        base = error.abs() if self.loss_type == "l1" else error.square()
        base = base[:, 0].transpose(1, 2)
        bins = target.shape[2]
        # Log spectral flatness per frame via the shared helper.
        flat_hat = log_flatness(z_hat)
        flat_ref = log_flatness(z_ref)
        flatness = F.smooth_l1_loss(flat_hat, flat_ref, reduction="none").expand(
            -1, -1, bins)
        level_hat = torch.logsumexp(z_hat, dim=2, keepdim=True) - math.log(bins)
        level_ref = torch.logsumexp(z_ref, dim=2, keepdim=True) - math.log(bins)
        level = F.smooth_l1_loss(level_hat, level_ref, reduction="none").expand_as(flatness)
        unvoiced = torch.isin(mask_inputs[0], torch.tensor(
            self.unvoiced_ids, dtype=mask_inputs[0].dtype, device=target.device)) & ~mask_inputs[1]
        # Exclude silent/near-floor targets: their log-mean amplitude sits at
        # the capture floor, so flatness on them is degenerate supervision.
        silent = (level_ref.squeeze(2) <= SILENT_LEVEL_FLOOR)
        eligible = unvoiced & ~silent
        mask = eligible[:, :, None].to(target.dtype)
        alpha_bar = diffusion.alphas_cumprod[t].view(batch, 1, 1)
        auxiliary = (flatness + level) * mask * alpha_bar
        self.last_draw = dict(timesteps=[int(value) for value in t],
                              noiseSha256=hashlib.sha256(
                                  noise.detach().cpu().numpy().tobytes()).hexdigest(),
                              unvoicedFrames=int(unvoiced.sum()),
                              excludedSilentFrames=int((unvoiced & silent).sum()),
                              eligibleFrames=int(eligible.sum()),
                              flatnessTerm=float((flatness * mask * alpha_bar).sum().detach()),
                              levelTerm=float((level * mask * alpha_bar).sum().detach()))
        return base, auxiliary

    def __call__(self, model, inputs, target):
        base, auxiliary = self.components(model, inputs, target)
        return base + self.weight * auxiliary
