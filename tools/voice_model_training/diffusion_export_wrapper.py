"""Typed non-shallow entry point for the pinned upstream diffusion module.

Preserves openvpi/DiffSinger 336cf01b's non-shallow forward algorithm while
avoiding unannotated optional x_start/depth arguments inferred as Tensor by JIT.
Uses the upstream trained backbone, sampling helpers, schedule and denormalizer.

One deliberate departure from the pinned upstream sampler is carried here, and it
is the reason this wrapper reimplements the two transitions instead of calling
them: the clean-latent estimate each step feeds back into the next is clamped to
the interval the latent is defined on. See CLAMPED_LATENT_MIN.
"""
import torch


# norm_spec maps spec_min .. spec_max onto -1 .. 1, so every clean latent is
# inside that interval by construction. The measured admitted corpora sit well
# inside it: the 400-song corpus spans -0.919 .. 0.870.
#
# The DDIM and ancestral transitions both feed their own estimate of the clean
# latent back into the next step. An estimate outside the interval is not
# corrected by later steps; it is amplified, because the remaining steps treat it
# as the current best guess and add noise on top of it. Measured on the 400-song
# checkpoint, leaving it unclamped makes the sampler diverge as the step count
# grows: normalized output std went 5.98 to 216.27 as steps went 2 to 100, while
# correlation with the true target fell to 0.015. Clamping the estimate makes the
# same sampler stable in the step count and correlated with the target (std 2.89
# to 2.68, correlation 0.628 to 0.651). More steps are supposed to buy quality, so
# a sampler that degrades with more steps is wrong.
#
# Upstream carries the identical clamp inside p_sample and disables it by comment;
# this checkout previously carried neither. The bound is a property of the
# representation rather than a tuning knob, so it is fixed and not exposed.
CLAMPED_LATENT_MIN = -1.0
CLAMPED_LATENT_MAX = 1.0


def _extract(values: torch.Tensor, timestep: torch.Tensor) -> torch.Tensor:
    """Broadcast a schedule entry over a [B, F, M, T] latent, as upstream does."""
    return values[timestep].reshape((1, 1, 1, 1))


class DiffusionExportWrapper(torch.nn.Module):
    """Sampler entry point whose ONNX inlining upstream's TorchScript needs."""

    # Declared as TorchScript constants rather than read from module globals:
    # scripting refuses a closed-over module global, and the bound has to survive
    # into the scripted graph rather than being dropped or defaulted.
    __constants__ = ["clamped_min", "clamped_max"]
    clamped_min: float
    clamped_max: float

    def __init__(self, diffusion, clamp_latent: bool = True):
        super().__init__()
        self.diffusion = diffusion
        self.clamped_min = CLAMPED_LATENT_MIN
        self.clamped_max = CLAMPED_LATENT_MAX
        # Disabling the clamp makes each step delegate to the diffusion's own
        # transition, which is how check_sampler_transcription proves this is still
        # a transcription of the pinned upstream algorithm rather than a rewrite.
        self.clamp_latent = clamp_latent

    def forward(self, condition: torch.Tensor, steps: int) -> torch.Tensor:
        # Variance controls are already embedded in the condition tensor by the
        # duration encoder. A second control argument here would be an ignored input.
        return self.diffusion.denorm_spec(self.sample_latent(condition, steps)
                                          .squeeze(1).permute(0, 2, 1))

    def sample_latent(self, condition: torch.Tensor, steps: int) -> torch.Tensor:
        """Reverse process in normalized latent space, before denormalization.

        Kept separate from ``forward`` because the interval the clamp enforces is a
        property of the normalized latent, not of the denormalized mel a caller
        sees. A check on the mel output would be checking the wrong quantity.
        """
        condition = condition.transpose(1, 2)
        noise = torch.randn((1, self.diffusion.num_feats, self.diffusion.out_dims, condition.shape[2]),
                            device=condition.device)
        speedup = max(1, self.diffusion.timesteps // steps)
        speedup = int(self.diffusion.timestep_factors[torch.sum(self.diffusion.timestep_factors <= speedup) - 1])
        step_range = torch.arange(0, self.diffusion.k_step, speedup, dtype=torch.long,
                                 device=condition.device).flip(0)[:, None]
        x = noise
        if speedup > 1:
            for t in step_range:
                x = self._step_ddim(x, t, speedup, condition)
        else:
            for t in step_range:
                x = self._step_ancestral(x, t, condition)
        return x

    def _clamp_clean(self, clean: torch.Tensor) -> torch.Tensor:
        if not self.clamp_latent:
            return clean
        return torch.clamp(clean, min=self.clamped_min, max=self.clamped_max)

    def _step_ddim(self, x: torch.Tensor, t: torch.Tensor, interval: int,
                   condition: torch.Tensor) -> torch.Tensor:
        """One DDIM step, algebraically upstream's, with the clean estimate bounded."""
        if not self.clamp_latent:
            return self.diffusion.p_sample_ddim(x, t, interval, condition)
        a_t = _extract(self.diffusion.alphas_cumprod, t)
        a_prev = _extract(self.diffusion.alphas_cumprod,
                          torch.max(t - interval, torch.zeros_like(t)))
        noise_pred = self.diffusion.denoise_fn(x, t, cond=condition)
        clean = self._clamp_clean((x - (1. - a_t).sqrt() * noise_pred) / a_t.sqrt())
        # Re-derive the noise from the bounded estimate so the step stays on one
        # trajectory; keeping the unbounded estimate's noise instead would make the
        # two terms disagree about which sample they are stepping from.
        noise_pred = (x - a_t.sqrt() * clean) / (1. - a_t).sqrt()
        return a_prev.sqrt() * clean + (1. - a_prev).sqrt() * noise_pred

    def _step_ancestral(self, x: torch.Tensor, t: torch.Tensor,
                        condition: torch.Tensor) -> torch.Tensor:
        """One ancestral step, algebraically upstream's, with the clean estimate bounded."""
        if not self.clamp_latent:
            return self.diffusion.p_sample(x, t, condition)
        noise_pred = self.diffusion.denoise_fn(x, t, cond=condition)
        clean = self._clamp_clean(
            _extract(self.diffusion.sqrt_recip_alphas_cumprod, t) * x
            - _extract(self.diffusion.sqrt_recipm1_alphas_cumprod, t) * noise_pred)
        model_mean = (_extract(self.diffusion.posterior_mean_coef1, t) * clean
                      + _extract(self.diffusion.posterior_mean_coef2, t) * x)
        model_log_variance = _extract(self.diffusion.posterior_log_variance_clipped, t)
        noise = torch.randn_like(x)
        nonzero_mask = (t > 0).float().reshape(1, 1, 1, 1)
        return model_mean + nonzero_mask * (0.5 * model_log_variance).exp() * noise
