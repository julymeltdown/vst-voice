import hashlib
import importlib.util
from types import SimpleNamespace
import unittest

from tools.voice_model_training.diffsinger_objective import (
    DiffSingerDDPMObjective, DiffSingerDDPMUnvoicedSpectralObjective,
    DiffSingerDDPMUnvoicedFlatnessObjective,
    UNVOICED_AUXILIARY_KIND, UNVOICED_FLATNESS_KIND, objective_id_for_settings)
from tools.voice_model_training.optimization import acoustic_training_step


@unittest.skipUnless(importlib.util.find_spec("torch"), "Optional Torch environment not installed")
class DiffSingerObjectiveTests(unittest.TestCase):
    def test_training_call_layout_and_rejected_chunk(self):
        import numpy as np
        import torch
        class Fixture(torch.nn.Module):
            diffusion_type = "ddpm"
            use_shallow_diffusion = False
            def __init__(self):
                super().__init__()
                self.bias = torch.nn.Parameter(torch.zeros(1, 1, 2, 3))
                self.fs2 = SimpleNamespace(**{flag: False for flag in (
                    "use_lang_id", "use_energy_embed", "use_breathiness_embed", "use_voicing_embed",
                    "use_tension_embed", "use_key_shift_embed", "use_speed_embed", "use_spk_id")})
                self.calls = 0
            def forward(self, tokens, *, mel2ph, f0, gt_mel, infer, breathiness=None):
                self.calls += 1
                assert not infer and tokens.tolist() == [[1, 1]] and mel2ph.tolist() == [[1, 1, 2]]
                assert tuple(gt_mel.shape) == (1, 3, 2)
                return SimpleNamespace(aux_out=None, diff_out=(self.bias, torch.ones_like(self.bias)))
        model = Fixture()
        optimizer = torch.optim.SGD(model.parameters(), lr=.1)
        batch = dict(sourceId="interface-fixture", partition="train", hopSize=256,
            frameOffset=0, phraseAnalysisFrames=3, tokens=[1, 1], mel2ph=[1, 1, 2],
            columns=dict(phoneId=[1]*3, f0Hz=[220]*3, voiced=[True]*3, midi=[57]*3,
                         rest=[False]*3, slur=[False]*3, breathiness=[0.0]*3, validSamples=[256]*3),
            melTargets=np.zeros((3, 2), dtype=np.float32))
        objective = DiffSingerDDPMObjective("l2")
        def step():
            return acoustic_training_step(model, optimizer, batch, vocabulary_size=1,
                objective=objective, objective_id=objective.objective_id)
        self.assertGreater(step()["loss"], 0)
        self.assertTrue(torch.all(model.bias > 0))
        batch["phraseAnalysisFrames"] = 4
        with self.assertRaises(ValueError): step()
        self.assertEqual(model.calls, 1)
        batch["phraseAnalysisFrames"] = 3
        model.fs2.use_spk_id = False
        model.fs2.use_breathiness_embed = True
        batch["columns"]["breathiness"] = [0.0, 0.5, 1.0]
        self.assertGreater(step()["loss"], 0)
        model.fs2.use_spk_id = True
        with self.assertRaises(ValueError): step()
        self.assertEqual(model.calls, 2)


def _batch(phone_ids, mel2ph, tokens, *, rest=None, loss_mask=None, frames=None, bins=4):
    import numpy as np
    count = len(phone_ids)
    rest = [False] * count if rest is None else rest
    return dict(sourceId="aux-fixture", partition="train", hopSize=256,
        frameOffset=0, phraseAnalysisFrames=count, tokens=list(tokens), mel2ph=list(mel2ph),
        lossMask=([True] * count if loss_mask is None else loss_mask),
        columns=dict(phoneId=list(phone_ids), f0Hz=[220.0 if not r else 0.0 for r in rest],
                     voiced=[not r for r in rest], midi=[57] * count, rest=list(rest),
                     slur=[False] * count, breathiness=[0.0] * count, validSamples=[256] * count),
        melTargets=np.linspace(-8.0, -2.0, num=count * bins, dtype=np.float32).reshape(count, bins))


class _FixtureEncoder:
    """Minimal fs2 stand-in: flags plus a callable producing [B,T,H] condition."""
    def __init__(self, torch, hidden=4, breathiness=True):
        self.use_lang_id = self.use_energy_embed = self.use_voicing_embed = False
        self.use_tension_embed = self.use_key_shift_embed = self.use_speed_embed = False
        self.use_spk_id = False
        self.use_breathiness_embed = breathiness
        self.token_embed = torch.nn.Embedding(8, hidden)
        self.f0_proj = torch.nn.Linear(1, hidden)
        self.breath_proj = torch.nn.Linear(1, hidden)

    def __call__(self, tokens, *, mel2ph, f0, breathiness=None, **kwargs):
        import torch
        embedded = self.token_embed(tokens)
        gathered = torch.gather(embedded, 1, (mel2ph - 1)[..., None].expand(-1, -1, embedded.shape[-1]))
        condition = gathered + self.f0_proj(f0[..., None].float() / 1000.0)
        if self.use_breathiness_embed:
            condition = condition + self.breath_proj(breathiness[..., None])
        return condition


def _fixture_model():
    """A DDPM model whose forward reproduces the upstream draw order exactly."""
    import torch
    import torch.nn as nn

    class Denoise(nn.Module):
        def __init__(self, bins, hidden=4):
            super().__init__()
            self.x_proj = nn.Conv2d(1, 1, 1)
            self.c_proj = nn.Linear(hidden, bins)
        def forward(self, x_t, t, cond):
            return self.x_proj(x_t) + self.c_proj(cond.transpose(1, 2)).transpose(1, 2).unsqueeze(1)

    class Diffusion(nn.Module):
        def __init__(self, bins, k_step=8):
            super().__init__()
            self.num_feats = 1
            self.k_step = k_step
            betas = torch.linspace(0.0001, 0.02, k_step)
            self.register_buffer("alphas_cumprod", torch.cumprod(1.0 - betas, dim=0))
            self.spec_min, self.spec_max = -12.0, 0.0
            self.denoise_fn = Denoise(bins)
        def norm_spec(self, x):
            return (x - self.spec_min) / (self.spec_max - self.spec_min) * 2 - 1
        def denorm_spec(self, x):
            return (x + 1) / 2 * (self.spec_max - self.spec_min) + self.spec_min
        def q_sample(self, x_start, t, noise):
            alpha = self.alphas_cumprod[t].view(-1, 1, 1, 1)
            return alpha.sqrt() * x_start + (1 - alpha).sqrt() * noise
        def predict_start_from_noise(self, x_t, t, noise):
            alpha = self.alphas_cumprod[t].view(-1, 1, 1, 1)
            return (x_t - (1 - alpha).sqrt() * noise) / alpha.sqrt()

    class Model(nn.Module):
        diffusion_type = "ddpm"
        use_shallow_diffusion = False
        def __init__(self, bins):
            super().__init__()
            self.fs2 = _FixtureEncoder(torch)
            self.diffusion = Diffusion(bins)
            self.encoder_params = nn.ParameterList(list(self.fs2.token_embed.parameters())
                + list(self.fs2.f0_proj.parameters()) + list(self.fs2.breath_proj.parameters()))
        def forward(self, tokens, *, mel2ph, f0, gt_mel, infer, breathiness=None):
            condition = self.fs2(tokens, mel2ph=mel2ph, f0=f0, breathiness=breathiness)
            cond = condition.transpose(1, 2)
            spec = self.diffusion.norm_spec(gt_mel).transpose(-2, -1)[:, None, :, :]
            t = torch.randint(0, self.diffusion.k_step, (tokens.shape[0],), device=tokens.device).long()
            noise = torch.randn_like(spec)
            x_t = self.diffusion.q_sample(x_start=spec, t=t, noise=noise)
            predicted = self.diffusion.denoise_fn(x_t, t, cond)
            return SimpleNamespace(aux_out=None, diff_out=(predicted, noise))

    return Model


@unittest.skipUnless(importlib.util.find_spec("torch"), "Optional Torch environment not installed")
class UnvoicedSpectralObjectiveTests(unittest.TestCase):
    def _inputs(self, phone_ids, rest, tokens, mel2ph):
        import torch
        count = len(phone_ids)
        return dict(phoneIds=torch.tensor([phone_ids]), f0Hz=torch.tensor([[220.0] * count]),
                    voiced=torch.tensor([[True] * count]), rest=torch.tensor([rest]),
                    slur=torch.tensor([[False] * count]), breathiness=torch.zeros(1, count),
                    midi=torch.tensor([[57] * count]), frameOffset=0, phraseAnalysisFrames=count,
                    tokens=torch.tensor([tokens]), mel2ph=torch.tensor([mel2ph]))

    def test_weight_zero_reproduces_base_loss_draws_and_gradients(self):
        import torch
        Model = _fixture_model()
        torch.manual_seed(7)
        model_a, model_b = Model(4), Model(4)
        model_b.load_state_dict(model_a.state_dict())
        inputs = self._inputs([1, 2, 2], [False, False, False], [1, 2], [1, 2, 2])
        target = torch.linspace(-8, -2, steps=12).reshape(1, 3, 4)
        base = DiffSingerDDPMObjective("l1")
        auxiliary = DiffSingerDDPMUnvoicedSpectralObjective("l1", weight=0.0, unvoiced_ids=[2])
        torch.manual_seed(3)
        base_elem = base(model_a, inputs, target)
        torch.manual_seed(3)
        aux_elem = auxiliary(model_b, inputs, target)
        self.assertTrue(torch.equal(base_elem, aux_elem))
        # The recorded draw is the same timestep/noise the base forward would use.
        torch.manual_seed(3)
        expected_t = torch.randint(0, 8, (1,)).tolist()
        self.assertEqual(auxiliary.last_draw["timesteps"], expected_t)
        # Identical gradients under identical RNG.
        weights = torch.ones(1, 3, 1)
        for model, elem in ((model_a, base_elem), (model_b, aux_elem)):
            (elem * weights).sum().backward()
        grads_a = {k: p.grad.clone() for k, p in model_a.named_parameters() if p.grad is not None}
        for name, parameter in model_b.named_parameters():
            self.assertIn(name, grads_a)
            self.assertTrue(torch.equal(grads_a[name], parameter.grad), name)

    def test_auxiliary_term_masking_and_gradient(self):
        import torch
        Model = _fixture_model()
        torch.manual_seed(7)
        model = Model(4)
        # Frames 1 and 2 are unvoiced id 2; frame 0 is voiced id 1; frame 3 is rest id 2.
        inputs = self._inputs([1, 2, 2, 2], [False, False, False, True], [1, 2, 2], [1, 2, 2, 3])
        target = torch.linspace(-8, -2, steps=16).reshape(1, 4, 4)
        auxiliary = DiffSingerDDPMUnvoicedSpectralObjective("l1", weight=0.5, unvoiced_ids=[2])
        torch.manual_seed(5)
        base_elem, aux_elem = auxiliary.components(model, inputs, target)
        self.assertTrue(torch.isfinite(aux_elem).all())
        self.assertTrue(bool((aux_elem >= 0).all()))
        # Only the two non-rest unvoiced frames may carry auxiliary loss.
        self.assertTrue(bool((aux_elem[0, 0] == 0).all()))
        self.assertTrue(bool((aux_elem[0, 3] == 0).all()))
        self.assertTrue(bool((aux_elem[0, 1] > 0).any() or (aux_elem[0, 2] > 0).any()))
        scalar = (aux_elem * torch.ones(1, 4, 1)).sum()
        grads = torch.autograd.grad(scalar, list(model.parameters()), retain_graph=True, allow_unused=True)
        norms = [float(g.square().sum()) for g in grads if g is not None]
        self.assertTrue(any(norm > 0 for norm in norms))
        self.assertTrue(all(norm == norm for norm in norms))

    def test_masked_out_unvoiced_frames_contribute_nothing(self):
        import torch
        Model = _fixture_model()
        torch.manual_seed(7)
        model = Model(4)
        optimizer = torch.optim.SGD(model.parameters(), lr=0.0)
        # All frames unvoiced but every unvoiced frame masked out of the loss.
        batch = _batch([2, 2, 2], [1, 1, 1], [2], rest=[False, False, False],
                       loss_mask=[True, False, False])
        batch["columns"]["phoneId"] = [1, 2, 2]
        batch["tokens"], batch["mel2ph"] = [1, 2], [1, 2, 2]
        auxiliary = DiffSingerDDPMUnvoicedSpectralObjective("l1", weight=0.5, unvoiced_ids=[2])
        base = DiffSingerDDPMObjective("l1")
        torch.manual_seed(9)
        with_aux = acoustic_training_step(model, optimizer, batch, vocabulary_size=2,
            objective=auxiliary, objective_id=auxiliary.objective_id)
        torch.manual_seed(9)
        without_aux = acoustic_training_step(model, optimizer, batch, vocabulary_size=2,
            objective=base, objective_id=base.objective_id)
        self.assertAlmostEqual(with_aux["loss"], without_aux["loss"], places=7)
        self.assertEqual(with_aux["draw"]["unvoicedFrames"], 2)
        self.assertIsNone(without_aux["draw"])

    def test_declared_validation(self):
        for weight, ids in ((-0.1, [2]), (1.1, [2]), (float("nan"), [2]),
                            (0.5, []), (0.5, [0]), (0.5, [2, 2])):
            with self.assertRaises(ValueError):
                DiffSingerDDPMUnvoicedSpectralObjective("l1", weight=weight, unvoiced_ids=ids)
        with self.assertRaises(ValueError):
            DiffSingerDDPMUnvoicedSpectralObjective("l3", weight=0.5, unvoiced_ids=[2])
        with self.assertRaises(ValueError):
            objective_id_for_settings(dict(loss="l1", auxiliaryObjective=dict(kind="other")))
        self.assertEqual(
            objective_id_for_settings(dict(loss="l1", auxiliaryObjective=dict(
                kind=UNVOICED_AUXILIARY_KIND))),
            "diffsinger-ddpm-l1-unvoiced-clean-mel-shape-level-v1")
        self.assertEqual(objective_id_for_settings(dict(loss="l2")),
                         "diffsinger-ddpm-l2-whole-phrase-v2")


@unittest.skipUnless(importlib.util.find_spec("torch"), "Optional Torch environment not installed")
class UnvoicedFlatnessObjectiveTests(unittest.TestCase):
    """The target-relative log-flatness auxiliary shares the spectral fixture."""

    def _inputs(self, phone_ids, rest, tokens, mel2ph):
        import torch
        count = len(phone_ids)
        return dict(phoneIds=torch.tensor([phone_ids]), f0Hz=torch.tensor([[220.0] * count]),
                    voiced=torch.tensor([[True] * count]), rest=torch.tensor([rest]),
                    slur=torch.tensor([[False] * count]), breathiness=torch.zeros(1, count),
                    midi=torch.tensor([[57] * count]), frameOffset=0, phraseAnalysisFrames=count,
                    tokens=torch.tensor([tokens]), mel2ph=torch.tensor([mel2ph]))

    def test_weight_zero_reproduces_base_loss_draws_and_gradients(self):
        import torch
        Model = _fixture_model()
        torch.manual_seed(7)
        model_a, model_b = Model(4), Model(4)
        model_b.load_state_dict(model_a.state_dict())
        inputs = self._inputs([1, 2, 2], [False, False, False], [1, 2], [1, 2, 2])
        target = torch.linspace(-8, -2, steps=12).reshape(1, 3, 4)
        base = DiffSingerDDPMObjective("l1")
        auxiliary = DiffSingerDDPMUnvoicedFlatnessObjective("l1", weight=0.0, unvoiced_ids=[2])
        torch.manual_seed(3)
        base_elem = base(model_a, inputs, target)
        torch.manual_seed(3)
        aux_elem = auxiliary(model_b, inputs, target)
        self.assertTrue(torch.equal(base_elem, aux_elem))
        torch.manual_seed(3)
        expected_t = torch.randint(0, 8, (1,)).tolist()
        self.assertEqual(auxiliary.last_draw["timesteps"], expected_t)
        weights = torch.ones(1, 3, 1)
        for model, elem in ((model_a, base_elem), (model_b, aux_elem)):
            (elem * weights).sum().backward()
        grads_a = {k: p.grad.clone() for k, p in model_a.named_parameters() if p.grad is not None}
        for name, parameter in model_b.named_parameters():
            self.assertIn(name, grads_a)
            self.assertTrue(torch.equal(grads_a[name], parameter.grad), name)

    def test_flatness_term_rewards_matching_reference_concentration(self):
        """The auxiliary is zero when predicted clean mel equals the reference."""
        import torch
        Model = _fixture_model()
        torch.manual_seed(7)
        model = Model(4)
        inputs = self._inputs([2, 2], [False, False], [2], [1, 1])
        target = torch.linspace(-8, -2, steps=8).reshape(1, 2, 4)
        auxiliary = DiffSingerDDPMUnvoicedFlatnessObjective("l1", weight=0.5, unvoiced_ids=[2])
        torch.manual_seed(5)
        base_elem, aux_elem = auxiliary.components(model, inputs, target)
        self.assertTrue(torch.isfinite(aux_elem).all())
        self.assertTrue(bool((aux_elem >= 0).all()))
        # A flatness target equal to the reference's own means the term can
        # approach zero; a wrong-concentration prediction cannot zero it.
        scalar = (aux_elem * torch.ones(1, 2, 1)).sum()
        grads = torch.autograd.grad(scalar, list(model.parameters()),
                                    retain_graph=True, allow_unused=True)
        self.assertTrue(any(float(g.square().sum()) > 0 for g in grads if g is not None))

    def test_masking_limits_auxiliary_to_unvoiced_non_rest_frames(self):
        import torch
        Model = _fixture_model()
        torch.manual_seed(7)
        model = Model(4)
        inputs = self._inputs([1, 2, 2, 2], [False, False, False, True], [1, 2, 2], [1, 2, 2, 3])
        target = torch.linspace(-8, -2, steps=16).reshape(1, 4, 4)
        auxiliary = DiffSingerDDPMUnvoicedFlatnessObjective("l1", weight=0.5, unvoiced_ids=[2])
        torch.manual_seed(5)
        base_elem, aux_elem = auxiliary.components(model, inputs, target)
        self.assertTrue(bool((aux_elem[0, 0] == 0).all()))
        self.assertTrue(bool((aux_elem[0, 3] == 0).all()))

    def test_declared_validation(self):
        for weight, ids in ((-0.1, [2]), (1.1, [2]), (float("nan"), [2]),
                            (0.5, []), (0.5, [0]), (0.5, [2, 2])):
            with self.assertRaises(ValueError):
                DiffSingerDDPMUnvoicedFlatnessObjective("l1", weight=weight, unvoiced_ids=ids)
        with self.assertRaises(ValueError):
            DiffSingerDDPMUnvoicedFlatnessObjective("l3", weight=0.5, unvoiced_ids=[2])
        self.assertEqual(
            objective_id_for_settings(dict(loss="l1", auxiliaryObjective=dict(
                kind=UNVOICED_FLATNESS_KIND))),
            "diffsinger-ddpm-l1-unvoiced-target-log-flatness-v1")


if __name__ == "__main__":
    unittest.main()
