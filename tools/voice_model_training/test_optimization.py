import importlib.util
import unittest

from tools.voice_model_training.optimization import acoustic_training_step, acoustic_evaluation_step


@unittest.skipUnless(importlib.util.find_spec("torch"), "Optional Torch training environment not installed")
class OptimizationTests(unittest.TestCase):
    def test_evaluation_preserves_rng_gradients_and_mixed_modes(self):
        import numpy as np
        import torch
        class Fixture(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.bias = torch.nn.Parameter(torch.zeros(1, 1, 1))
                self.child = torch.nn.Dropout()
            def forward(self, inputs):
                return self.bias + torch.rand_like(self.bias)
        model = Fixture()
        model.train()
        model.child.eval()
        model.bias.grad = torch.ones_like(model.bias) * 7
        rng = torch.get_rng_state().clone()
        batch = dict(sourceId="evaluation-fixture", partition="validation", hopSize=256,
            columns=dict(phoneId=[1], f0Hz=[220], voiced=[True], midi=[57], rest=[False], slur=[False], validSamples=[256]),
            melTargets=np.ones((1, 1), dtype=np.float32))
        first = acoustic_evaluation_step(model, batch, vocabulary_size=1, seed=41)
        second = acoustic_evaluation_step(model, batch, vocabulary_size=1, seed=41)
        self.assertEqual(first["loss"], second["loss"])
        self.assertTrue(first["evaluation"])
        self.assertIsNone(first["gradientNorm"])
        self.assertTrue(torch.equal(torch.get_rng_state(), rng))
        self.assertTrue(model.training)
        self.assertFalse(model.child.training)
        self.assertEqual(float(model.bias.grad.sum()), 7)
        self.assertEqual(float(model.bias.detach().sum()), 0)
        with self.assertRaises(ValueError):
            acoustic_evaluation_step(model, dict(batch, partition="train"), vocabulary_size=1, seed=41)
        self.assertTrue(torch.equal(torch.get_rng_state(), rng))
        self.assertTrue(model.training)
        self.assertFalse(model.child.training)

    def test_real_update_weighted_tail_and_held_out_rejection(self):
        import numpy as np
        import torch
        class Fixture(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.bias = torch.nn.Parameter(torch.zeros(1, 1, 2))
            def forward(self, inputs):
                return self.bias.expand(1, inputs["phoneIds"].shape[1], 2)
        model = Fixture()
        optimizer = torch.optim.SGD(model.parameters(), lr=.1)
        batch = dict(sourceId="fixture-not-a-singer", partition="train", hopSize=256,
            columns=dict(phoneId=[1, 1], f0Hz=[220, 220], voiced=[True, True],
                         midi=[57, 57], rest=[False, False], slur=[False, False], validSamples=[256, 128]),
            melTargets=np.array([[1, 1], [3, 3]], dtype=np.float32))
        first = acoustic_training_step(model, optimizer, batch, vocabulary_size=1)
        self.assertAlmostEqual(first["loss"], 5 / 3, places=6)
        self.assertTrue(torch.all(model.bias > 0))
        second = acoustic_training_step(model, optimizer, batch, vocabulary_size=1)
        self.assertLess(second["loss"], first["loss"])
        masked_model = Fixture()
        masked_optimizer = torch.optim.SGD(masked_model.parameters(), lr=.1)
        masked = dict(batch, lossMask=[True, False])
        measured = acoustic_training_step(masked_model, masked_optimizer, masked, vocabulary_size=1)
        self.assertAlmostEqual(measured["loss"], 1, places=6)
        self.assertEqual(measured["lossFrames"], 1)
        self.assertEqual(measured["validSamples"], 256)
        custom_model = Fixture()
        custom_optimizer = torch.optim.SGD(custom_model.parameters(), lr=.1)
        def custom_objective(adapter, inputs, targets):
            return (adapter(inputs) - 2).square()  # Fixture noise target, not mel regression.
        custom = acoustic_training_step(custom_model, custom_optimizer, masked, vocabulary_size=1,
            objective=custom_objective, objective_id="fixture-noise-mse")
        self.assertEqual(custom["objectiveId"], "fixture-noise-mse")
        self.assertAlmostEqual(custom["loss"], 4)
        self.assertTrue(torch.all(custom_model.bias > 0))
        observed = {}
        def aligned_objective(adapter, inputs, target):
            observed["tokens"] = inputs["tokens"].tolist()
            observed["mel2ph"] = inputs["mel2ph"].tolist()
            return (adapter(inputs) - target).square()
        aligned = dict(batch, tokens=[1, 1, 1], mel2ph=[1, 3])
        acoustic_training_step(custom_model, custom_optimizer, aligned, vocabulary_size=1,
            objective=aligned_objective, objective_id="fixture-aligned-mse")
        self.assertEqual(observed, {"tokens": [[1, 1, 1]], "mel2ph": [[1, 3]]})
        with self.assertRaises(ValueError):
            acoustic_training_step(custom_model, custom_optimizer, dict(aligned, mel2ph=[1, 4]), vocabulary_size=1)
        with self.assertRaises(ValueError):
            acoustic_training_step(custom_model, custom_optimizer, batch, vocabulary_size=1, objective=custom_objective)
        with self.assertRaises(ValueError):
            acoustic_training_step(custom_model, custom_optimizer, batch, vocabulary_size=1,
                objective=lambda model, inputs, target: target.sum(), objective_id="invalid-reduced-loss")
        with self.assertRaises(ValueError):
            acoustic_training_step(masked_model, masked_optimizer, dict(batch, lossMask=[False, False]), vocabulary_size=1)
        saved = model.bias.detach().clone()
        batch["partition"] = "test"
        with self.assertRaises(ValueError): acoustic_training_step(model, optimizer, batch, vocabulary_size=1)
        self.assertTrue(torch.equal(saved, model.bias))
        batch["partition"] = "train"
        batch["melTargets"][0, 0] = np.nan
        with self.assertRaises(ValueError): acoustic_training_step(model, optimizer, batch, vocabulary_size=1)
        self.assertTrue(torch.equal(saved, model.bias))

    def test_nonfinite_gradient_never_steps_optimizer(self):
        import numpy as np
        import torch
        from unittest.mock import patch
        class BadGradient(torch.autograd.Function):
            @staticmethod
            def forward(ctx, value):
                return value.clone()
            @staticmethod
            def backward(ctx, gradient):
                return torch.full_like(gradient, float("inf"))
        class Fixture(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.bias = torch.nn.Parameter(torch.zeros(1, 1, 1))
            def forward(self, inputs):
                return BadGradient.apply(self.bias)
        model = Fixture()
        optimizer = torch.optim.SGD(model.parameters(), lr=.1)
        batch = dict(sourceId="bad-gradient-fixture", partition="train", hopSize=256,
            columns=dict(phoneId=[1], f0Hz=[220], voiced=[True], midi=[57], rest=[False], slur=[False], validSamples=[256]),
            melTargets=np.ones((1, 1), dtype=np.float32))
        with patch.object(optimizer, "step", wraps=optimizer.step) as step:
            with self.assertRaises(RuntimeError):
                acoustic_training_step(model, optimizer, batch, vocabulary_size=1)
            step.assert_not_called()
        self.assertEqual(float(model.bias.detach().sum()), 0)


if __name__ == "__main__":
    unittest.main()
