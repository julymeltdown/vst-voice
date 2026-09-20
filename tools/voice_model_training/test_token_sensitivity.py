import unittest
import numpy as np

from tools.voice_model_training.token_sensitivity import sensitivity, swapped_frames


class TokenSensitivityTests(unittest.TestCase):
    def test_swap_effect_is_measured_over_the_owning_phone_only(self):
        alignment = np.array([1, 1, 2, 2, 2, 3])
        original = np.zeros((6, 4))
        swapped = np.zeros((6, 4))
        swapped[2:5] = 2.0            # only phone 2 changed
        report = sensitivity(original, swapped, alignment, 2)
        self.assertEqual(report['frames'], 3)
        self.assertAlmostEqual(report['meanAbsoluteChange'], 2.0)
        self.assertEqual(sensitivity(original, swapped, alignment, 1)['meanAbsoluteChange'], 0.0)

    def test_ignores_changes_outside_the_phone(self):
        alignment = np.array([1, 1, 2, 2])
        original, swapped = np.zeros((4, 4)), np.zeros((4, 4))
        swapped[:2] = 5.0             # only phone 1 changed
        self.assertEqual(sensitivity(original, swapped, alignment, 2)['meanAbsoluteChange'], 0.0)

    def test_flat_phone_reports_undefined_ratio(self):
        alignment = np.array([1, 1])
        report = sensitivity(np.ones((2, 4)), np.full((2, 4), 2.0), alignment, 1)
        self.assertAlmostEqual(report['meanAbsoluteChange'], 1.0)
        self.assertIsNone(report['changeToSpreadRatio'])

    def test_invalid_inputs(self):
        alignment = np.array([1, 2])
        for bad in (0, -1):
            with self.assertRaises(ValueError):
                swapped_frames(alignment, bad)
        with self.assertRaises(ValueError):
            sensitivity(np.zeros((2, 4)), np.zeros((2, 4)), alignment, 3)  # no frames
        with self.assertRaises(ValueError):
            sensitivity(np.zeros((2, 4)), np.zeros((3, 4)), alignment, 1)
        with self.assertRaises(ValueError):
            sensitivity(np.zeros((2, 4)), np.full((2, 4), np.nan), alignment, 1)


if __name__ == '__main__':
    unittest.main()
