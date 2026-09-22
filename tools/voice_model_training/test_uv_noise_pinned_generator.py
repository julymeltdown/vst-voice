"""Pinned-real-Generator fixture for the UV-noise variant.

Compares the subclassed variant against the genuine upstream forward on the
same weights: identical output and gradients at zero noise, a changed output
at nonzero noise, and identical state_dict keys for warm-start governance.
Uses the small smoke architecture so the fixture stays in-memory.
"""
import importlib.util
import unittest

if importlib.util.find_spec("torch") is None:
    raise unittest.SkipTest("Optional Torch environment not installed")

import torch

from tools.voice_model_training.check_vocoder_model import (
    TRAINING_REVISION, trusted_checkout, vocoder_configuration)
from tools.voice_model_training.train_vocoder import _module
from tools.voice_model_training.uv_noise_excitation import (
    SOURCE_SAMPLES_PER_FRAME, uv_noise_generator_class)

CHECKOUT = ('/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/'
            'build/neural-runtime/singing-vocoders-source')


class PinnedGeneratorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from pathlib import Path
        checkout = trusted_checkout(Path(CHECKOUT), TRAINING_REVISION)
        source = _module(checkout, 'seam_test_uvnoise_arch', 'models/nsf_HiFigan/models.py')
        cls.AttrDict = source.AttrDict
        cls.Base = source.Generator
        cls.Variant = uv_noise_generator_class(source.Generator)
        cls.configuration = cls.AttrDict(vocoder_configuration('mini-nsf-32-smoke-v1'))

    def pair(self):
        torch.manual_seed(11)
        base = self.Base(self.configuration)
        variant = self.Variant(self.configuration)
        variant.load_state_dict(base.state_dict())
        return base, variant

    def test_state_dict_and_load_are_identical(self):
        base, variant = self.pair()
        self.assertEqual(set(base.state_dict()), set(variant.state_dict()))
        for key, value in base.state_dict().items():
            self.assertTrue(torch.equal(value, variant.state_dict()[key]))

    def test_zero_noise_matches_upstream_forward_and_gradients(self):
        base, variant = self.pair()
        frames = 8
        torch.manual_seed(3)
        mel = torch.randn(1, 80, frames)
        f0 = torch.linspace(0, 220, frames).reshape(1, frames)
        f0[:, :3] = 0
        expected = base(mel.clone(), f0)
        zero = torch.zeros(1, 1, frames * SOURCE_SAMPLES_PER_FRAME)
        actual = variant(mel.clone(), f0, zero)
        self.assertTrue(torch.equal(actual, expected))
        for model in (base, variant):
            model.zero_grad(set_to_none=True)
        base(mel.clone(), f0).square().mean().backward()
        variant(mel.clone(), f0, zero).square().mean().backward()
        base_grads = {k: p.grad for k, p in base.named_parameters()}
        variant_grads = {k: p.grad for k, p in variant.named_parameters()}
        self.assertEqual(set(base_grads), set(variant_grads))
        for name in base_grads:
            self.assertTrue(torch.equal(base_grads[name], variant_grads[name]), name)

    def test_nonzero_noise_changes_output(self):
        base, variant = self.pair()
        frames = 8
        torch.manual_seed(5)
        mel = torch.randn(1, 80, frames)
        f0 = torch.zeros(1, frames)
        torch.manual_seed(9)
        noise = torch.randn(1, 1, frames * SOURCE_SAMPLES_PER_FRAME)
        quiet = variant(mel.clone(), f0, torch.zeros_like(noise))
        excited = variant(mel.clone(), f0, noise)
        self.assertFalse(torch.equal(quiet, excited))
        self.assertTrue(torch.isfinite(excited).all())


if __name__ == '__main__':
    unittest.main()
