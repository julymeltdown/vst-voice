import unittest
import numpy as np

from tools.voice_model_training.aperiodicity import estimate


class AperiodicityTests(unittest.TestCase):
    def test_noise_scores_above_a_pure_tone(self):
        rate, hop, frames = 48000, 256, 40
        t = np.arange(frames * hop + 1024) / rate
        tone = np.sin(2 * np.pi * 220 * t)
        noise = np.random.default_rng(5).normal(0, 0.2, t.size)
        tone_values = estimate(tone, frame_count=frames, hop_size=hop)
        noise_values = estimate(noise, frame_count=frames, hop_size=hop)
        self.assertLess(float(np.mean(tone_values)), float(np.mean(noise_values)))
        self.assertGreater(float(np.mean(noise_values)), 0.4)
        self.assertLess(float(np.mean(tone_values)), 0.6)

    def test_values_are_bounded_and_finite_including_silence(self):
        samples = np.concatenate([np.zeros(2048), np.random.default_rng(9).normal(0, .1, 4000)])
        values = estimate(samples, frame_count=20, hop_size=256)
        self.assertEqual(len(values), 20)
        self.assertTrue(all(0.0 <= value <= 1.0 for value in values))
        self.assertEqual(values[0], 0.0)          # silent first frame
        self.assertTrue(all(value == value for value in values))

    def test_short_final_frame_is_padded_not_dropped(self):
        values = estimate(np.zeros(3000) + .5, frame_count=20, hop_size=256)
        self.assertEqual(len(values), 20)

    def test_invalid_inputs(self):
        base = dict(frame_count=8, hop_size=256, frame_size=1024)
        for bad in (dict(frame_count=0), dict(hop_size=0), dict(frame_size=1)):
            with self.assertRaises(ValueError):
                estimate(np.zeros(4096), **{**base, **bad})
        with self.assertRaises(ValueError):
            estimate(np.full(4096, np.nan), frame_count=8, hop_size=256)


if __name__ == '__main__':
    unittest.main()
