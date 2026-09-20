import unittest

import numpy as np
import torch

from tools.voice_model_training.uv_noise_excitation import (
    EXCITATION_AMPLITUDE, SOURCE_SAMPLES_PER_FRAME, build_excitation_noise,
    excitation_amplitude, unvoiced_frame_mask, uv_noise_forward,
    uv_noise_generator_class)


PHONEMES = [dict(symbol='a'), dict(symbol='s'), dict(symbol='pau'), dict(symbol='k')]


def _columns(indices):
    return dict(phoneIndex=list(indices))


class MaskTests(unittest.TestCase):
    def test_unvoiced_phones_only(self):
        # a, s, pau, k, a -> only s and k are gated; pau and the f0=0 vowel are not.
        mask = unvoiced_frame_mask(_columns([0, 1, 2, 3, 0]), PHONEMES)
        self.assertEqual(mask.tolist(), [False, True, False, True, False])

    def test_rejects_bad_ownership(self):
        for columns, phonemes in (
                (dict(phoneIndex=[0, 4]), PHONEMES),
                (dict(phoneIndex=[0.5]), PHONEMES),
                (dict(phoneIndex=[]), PHONEMES),
                (dict(phoneIndex=[0]), []),
                (dict(phoneIndex=[0]), [dict(nosymbol='a')])):
            with self.assertRaises(ValueError):
                unvoiced_frame_mask(columns, phonemes)


class NoiseTests(unittest.TestCase):
    def test_known_ids_and_bounds(self):
        self.assertEqual(excitation_amplitude('zero-v1'), 0.0)
        self.assertAlmostEqual(excitation_amplitude('uv-gated-v1'), 1.0 / 3.0)
        with self.assertRaises(ValueError):
            excitation_amplitude('unknown')

    def test_control_draws_match_and_zero(self):
        mask = np.array([True, False, True], dtype=bool)
        torch.manual_seed(7)
        zero_raw, zero = build_excitation_noise(mask, 'zero-v1')
        torch.manual_seed(7)
        gated_raw, gated = build_excitation_noise(mask, 'uv-gated-v1')
        self.assertEqual(tuple(zero.shape), (1, 1, 3 * SOURCE_SAMPLES_PER_FRAME))
        self.assertTrue(torch.equal(zero_raw, gated_raw))
        self.assertTrue(torch.equal(zero, torch.zeros_like(zero)))
        self.assertFalse(torch.equal(gated, torch.zeros_like(gated)))
        # Same draw sequence: the gated arm equals zero arm plus masked noise.
        gate = torch.from_numpy(np.repeat(mask.astype(np.float32),
                                          SOURCE_SAMPLES_PER_FRAME)).reshape(1, 1, -1)
        self.assertTrue(torch.equal(gated, gated_raw * gate * EXCITATION_AMPLITUDE['uv-gated-v1']))
        # Voiced frames carry no noise.
        voiced_slice = gated[0, 0, SOURCE_SAMPLES_PER_FRAME:2 * SOURCE_SAMPLES_PER_FRAME]
        self.assertTrue(torch.equal(voiced_slice, torch.zeros_like(voiced_slice)))


class ForwardTests(unittest.TestCase):
    class _Base(torch.nn.Module):
        def __init__(self):
            super().__init__()
            self.mini_nsf = True
            self.noise_sigma = 0.0
            self.num_upsamples = 2
            self.num_kernels = 1
            self.ups = torch.nn.ModuleList([
                torch.nn.Identity(),
                torch.nn.ConvTranspose1d(4, 4, SOURCE_SAMPLES_PER_FRAME,
                                         stride=SOURCE_SAMPLES_PER_FRAME)])
            self.resblocks = torch.nn.ModuleList(
                [torch.nn.Identity(), torch.nn.Identity()])
            self.source_conv = torch.nn.Conv1d(1, 4, 1)
            self.conv_pre = torch.nn.Conv1d(4, 4, 3, padding=1)
            self.conv_post = torch.nn.Conv1d(4, 1, 3, padding=1)

        def fastsinegen(self, f0):
            return torch.zeros(1, 1, f0.shape[1] * SOURCE_SAMPLES_PER_FRAME)

    def test_zero_noise_matches_two_input_forward(self):
        base = self._Base()
        variant = uv_noise_generator_class(self._Base)()
        variant.load_state_dict(base.state_dict())
        mel = torch.randn(1, 4, 8)
        f0 = torch.zeros(1, 8)
        noise = torch.zeros(1, 1, 8 * SOURCE_SAMPLES_PER_FRAME)
        expected = uv_noise_forward(base, mel, f0, noise)
        actual = variant(mel, f0, noise)
        self.assertTrue(torch.equal(actual, expected))

    def test_noise_changes_output_and_shape_validated(self):
        variant = uv_noise_generator_class(self._Base)()
        mel = torch.randn(1, 4, 8)
        f0 = torch.zeros(1, 8)
        zero = torch.zeros(1, 1, 8 * SOURCE_SAMPLES_PER_FRAME)
        noise = torch.randn(1, 1, 8 * SOURCE_SAMPLES_PER_FRAME)
        self.assertFalse(torch.equal(variant(mel, f0, zero), variant(mel, f0, noise)))
        with self.assertRaises(ValueError):
            variant(mel, f0, torch.randn(1, 1, 7 * SOURCE_SAMPLES_PER_FRAME))

    def test_state_dict_identity_for_warm_start(self):
        base, variant = self._Base(), uv_noise_generator_class(self._Base)()
        self.assertEqual(set(base.state_dict()), set(variant.state_dict()))


if __name__ == '__main__':
    unittest.main()
