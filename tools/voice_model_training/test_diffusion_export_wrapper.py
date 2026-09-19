"""Bounding the clean-latent estimate is what keeps the sampler from diverging.

The pinned upstream transitions are restated here so the clamp can be measured
against the same algebra without loading the upstream checkout or a trained model.
Equality with the real upstream sampler is checked separately against genuine
weights by onnx_acoustic.check_sampler_transcription, which is the check that
proves this wrapper still transcribes upstream rather than reimplementing it.
"""
import importlib.util
import unittest

HAVE_TORCH = importlib.util.find_spec("torch") is not None

if HAVE_TORCH:
    import torch

    class StubBackbone(torch.nn.Module):
        """Denoiser that predicts a large constant, so an unbounded step explodes."""

        def __init__(self, predicted_noise):
            super().__init__()
            self.predicted_noise = predicted_noise

        def forward(self, x, timestep, cond):
            return torch.full_like(x, self.predicted_noise)

    class StubDiffusion:
        """The attributes the wrapper reads, with upstream's two transitions."""

        def __init__(self, timesteps=1000, k_step=1000, out_dims=4, predicted_noise=8.0):
            self.timesteps = timesteps
            self.k_step = k_step
            self.out_dims = out_dims
            self.num_feats = 1
            betas = torch.linspace(1e-4, 0.02, timesteps)
            alphas = 1.0 - betas
            ac = torch.cumprod(alphas, dim=0)
            ac_prev = torch.cat([torch.ones(1), ac[:-1]])
            self.alphas_cumprod = ac
            self.sqrt_recip_alphas_cumprod = torch.sqrt(1.0 / ac)
            self.sqrt_recipm1_alphas_cumprod = torch.sqrt(1.0 / ac - 1.0)
            variance = betas * (1.0 - ac_prev) / (1.0 - ac)
            self.posterior_log_variance_clipped = torch.log(torch.clamp(variance, min=1e-20))
            self.posterior_mean_coef1 = betas * torch.sqrt(ac_prev) / (1.0 - ac)
            self.posterior_mean_coef2 = (1.0 - ac_prev) * torch.sqrt(alphas) / (1.0 - ac)
            factors = [i for i in range(1, timesteps + 1) if timesteps % i == 0]
            self.timestep_factors = torch.LongTensor(factors)
            self.denoise_fn = StubBackbone(predicted_noise)
            self.upstream_calls = {"ddim": 0, "ancestral": 0}

        def denorm_spec(self, x):
            return x

        def p_sample_ddim(self, x, t, interval, cond):
            self.upstream_calls["ddim"] += 1
            a_t = self.alphas_cumprod[t].reshape(1, 1, 1, 1)
            a_prev = self.alphas_cumprod[torch.max(t - interval, torch.zeros_like(t))].reshape(1, 1, 1, 1)
            noise_pred = self.denoise_fn(x, t, cond=cond)
            return a_prev.sqrt() * (
                x / a_t.sqrt()
                + (((1.0 - a_prev) / a_prev).sqrt() - ((1.0 - a_t) / a_t).sqrt()) * noise_pred)

        def p_sample(self, x, t, cond):
            self.upstream_calls["ancestral"] += 1
            a_t = self.alphas_cumprod[t].reshape(1, 1, 1, 1)
            noise_pred = self.denoise_fn(x, t, cond=cond)
            recon = (self.sqrt_recip_alphas_cumprod[t].reshape(1, 1, 1, 1) * x
                     - self.sqrt_recipm1_alphas_cumprod[t].reshape(1, 1, 1, 1) * noise_pred)
            mean = (self.posterior_mean_coef1[t].reshape(1, 1, 1, 1) * recon
                    + self.posterior_mean_coef2[t].reshape(1, 1, 1, 1) * x)
            log_variance = self.posterior_log_variance_clipped[t].reshape(1, 1, 1, 1)
            noise = torch.randn_like(x)
            nonzero = (t > 0).float().reshape(1, 1, 1, 1)
            return mean + nonzero * (0.5 * log_variance).exp() * noise


@unittest.skipUnless(HAVE_TORCH, "Optional Torch environment not installed")
class ClampedSamplerTests(unittest.TestCase):
    def test_schedule_envelope_covers_both_paths_and_rejects_unbounded_output(self):
        from tools.voice_model_training.diffusion_export_wrapper import sampling_absolute_bound
        for timesteps, steps in ((1000, 2), (1000, 4), (1000, 16), (10, 10)):
            for prediction in (-8., 0., 8.):
                with self.subTest(timesteps=timesteps, steps=steps, prediction=prediction):
                    diffusion, latent = self.sample(steps, True, prediction, timesteps)
                    before = torch.get_rng_state().clone()
                    bound = sampling_absolute_bound(diffusion, 3, steps, 3)
                    self.assertTrue(torch.equal(before, torch.get_rng_state()))
                    self.assertLessEqual(float(latent.abs().max()), bound + 1e-4)
                    # A large DC offset has zero spread but violates the envelope.
                    self.assertGreater(float(torch.full_like(latent, 1000.).abs().max()), bound + 1e-4)
        diffusion, unbounded = self.sample(16, False)
        self.assertGreater(float(unbounded.abs().max()), sampling_absolute_bound(diffusion, 3, 16, 3))

    def sample(self, steps, clamp_latent, predicted_noise=8.0, timesteps=1000):
        from tools.voice_model_training.diffusion_export_wrapper import DiffusionExportWrapper

        diffusion = StubDiffusion(timesteps=timesteps, k_step=timesteps, predicted_noise=predicted_noise)
        wrapper = DiffusionExportWrapper(diffusion, clamp_latent=clamp_latent).eval()
        condition = torch.zeros((1, 3, 2), dtype=torch.float32)
        torch.manual_seed(3)
        latent = wrapper.sample_latent(condition, steps)
        return diffusion, latent

    def test_the_disabled_clamp_delegates_to_the_diffusion_itself(self):
        # The unbounded path must remain the diffusion's own transition, so the
        # transcription check compares upstream with upstream rather than with a
        # second copy of the same algebra.
        ddim, _ = self.sample(steps=4, clamp_latent=False)
        self.assertEqual(ddim.upstream_calls["ddim"], 4)
        self.assertEqual(ddim.upstream_calls["ancestral"], 0)
        # The ancestral transition is the speedup == 1 path, which is reached when
        # the requested steps cover the whole schedule.
        ancestral, _ = self.sample(steps=10, clamp_latent=False, timesteps=10)
        self.assertEqual(ancestral.upstream_calls["ancestral"], 10)

    def test_the_clamp_is_stated_on_the_clean_estimate(self):
        # The interval belongs to the clean latent. Asserting it on the noisy step
        # the sampler returns would be asserting the wrong quantity, because noise
        # is added on top of the bounded estimate afterwards.
        from tools.voice_model_training.diffusion_export_wrapper import (
            CLAMPED_LATENT_MAX, CLAMPED_LATENT_MIN, DiffusionExportWrapper)

        self.assertEqual((CLAMPED_LATENT_MIN, CLAMPED_LATENT_MAX), (-1.0, 1.0))
        wrapper = DiffusionExportWrapper(StubDiffusion(predicted_noise=8.0), clamp_latent=True)
        estimation = torch.tensor([[[[-50.0, 0.25, 50.0]]]])
        bounded = wrapper._clamp_clean(estimation)
        self.assertEqual(float(bounded.min()), CLAMPED_LATENT_MIN)
        self.assertEqual(float(bounded.max()), CLAMPED_LATENT_MAX)
        self.assertEqual(float(bounded.flatten()[1]), 0.25)
        passthrough = DiffusionExportWrapper(StubDiffusion(), clamp_latent=False)._clamp_clean(estimation)
        self.assertTrue(torch.equal(passthrough, estimation))

    def test_the_bounded_sampler_stays_near_its_interval(self):
        from tools.voice_model_training.diffusion_export_wrapper import (
            CLAMPED_LATENT_MAX, CLAMPED_LATENT_MIN)

        _, latent = self.sample(steps=16, clamp_latent=True)
        self.assertTrue(torch.isfinite(latent).all())
        # Noise is added after the estimate is bounded, so the step itself may sit
        # just outside the interval; it must not wander far from it.
        self.assertGreater(float(latent.min()), CLAMPED_LATENT_MIN - 0.5)
        self.assertLess(float(latent.max()), CLAMPED_LATENT_MAX + 0.5)

    def test_more_steps_do_not_make_the_bounded_sampler_diverge(self):
        spreads = [float(self.sample(steps=s, clamp_latent=True)[1].std()) for s in (2, 4, 8, 16)]
        self.assertTrue(all(value == value for value in spreads), spreads)
        self.assertLessEqual(spreads[-1], spreads[0] * 1.5, spreads)

    def test_the_unbounded_sampler_diverges_where_the_bounded_one_does_not(self):
        # This is the defect the clamp fixes: with an over-confident denoiser the
        # unbounded recurrence grows with the step count instead of settling.
        bounded = float(self.sample(steps=16, clamp_latent=True)[1].std())
        unbounded = float(self.sample(steps=16, clamp_latent=False)[1].std())
        self.assertGreater(unbounded, bounded * 10.0, (bounded, unbounded))

    def test_a_non_finite_estimate_cannot_escape_downstream(self):
        # infinity is clamped to the interval rather than propagated into the
        # denormalizer, where it would become a NaN sample that still renders.
        _, latent = self.sample(steps=8, clamp_latent=True, predicted_noise=float("inf"))
        self.assertTrue(torch.isfinite(latent).all())


if __name__ == "__main__":
    unittest.main()
