import unittest
import torch
from tools.voice_model_training.unvoiced_noise_diagnostic import fixed_feature_noise


class FeatureNoiseTests(unittest.TestCase):
    def test_reproducible_local_rng_and_voiced_identity(self):
        features = torch.ones(1, 4, 8)
        f0 = torch.tensor([[0., 200., 0., 200., 0., 200., 0., 200.]])
        state = torch.get_rng_state().clone()
        a = fixed_feature_noise(features, f0, sigma=.1, seed=71)
        b = fixed_feature_noise(features, f0, sigma=.1, seed=71)
        self.assertTrue(torch.equal(a, b))
        self.assertTrue(torch.equal(state, torch.get_rng_state()))
        self.assertTrue(torch.equal(a[:, :, 1::2], features[:, :, 1::2]))
        self.assertFalse(torch.equal(a[:, :, ::2], features[:, :, ::2]))
        self.assertTrue(torch.equal(features, torch.ones_like(features)))
        self.assertTrue(torch.equal(fixed_feature_noise(features,f0,sigma=0,seed=71), features))

    def test_bounds(self):
        for sigma in (-1, .11, float('nan'), True):
            with self.assertRaises(ValueError):
                fixed_feature_noise(torch.ones(1,4,8),torch.zeros(1,8),sigma=sigma,seed=0)


if __name__ == '__main__':
    unittest.main()
