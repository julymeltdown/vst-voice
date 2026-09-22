"""Analytical signal controls for the additive diagnostic, not singing labels."""
import hashlib
import io
import unittest
import wave

import numpy as np

from tools.singing_quality.source_frequency_diagnostic import diagnose, measure_window


class SourceFrequencyDiagnosticTests(unittest.TestCase):
    def test_bin_centered_components_have_expected_energy_not_amplitude_shares(self):
        rate = 48000
        time = np.arange(rate) / rate
        samples = 0.2 * np.sin(2 * np.pi * 10 * time) + 0.4 * np.sin(2 * np.pi * 200 * time)
        result = measure_window(samples, rate)
        rectangular = result["spectra"][0]
        self.assertEqual("rectangular", rectangular["window"])
        self.assertAlmostEqual(0.2, rectangular["powerSharesBelowHz"]["71"], places=12)
        self.assertAlmostEqual(200, rectangular["dominantBinHz"], places=12)
        self.assertAlmostEqual(0, result["dcMeanSquareShare"], places=12)

    def test_dc_nyquist_and_odd_length_last_bin_obey_parseval(self):
        for size in (22050, 22051):
            for frequency_bin in (0, size // 2):
                with self.subTest(size=size, bin=frequency_bin):
                    signal = 0.5 * np.cos(2 * np.pi * frequency_bin * np.arange(size) / size)
                    result = measure_window(signal, 44100)
                    for spectrum in result["spectra"]:
                        self.assertAlmostEqual(spectrum["windowedTimeEnergy"],
                                               spectrum["parsevalFrequencyEnergy"], places=9)
                    self.assertAlmostEqual(1 if frequency_bin == 0 else 0,
                                           result["spectra"][0]["powerSharesBelowHz"]["71"], places=12)

    def test_silence_is_undefined_share_not_a_passing_zero(self):
        result = measure_window(np.zeros(4410), 44100)
        self.assertIsNone(result["dcMeanSquareShare"])
        for spectrum in result["spectra"]:
            self.assertIsNone(spectrum["dominantBinHz"])
            self.assertTrue(all(value is None for value in spectrum["powerSharesBelowHz"].values()))

    def test_invalid_geometry_and_samples_are_rejected(self):
        for samples, rate in (([], 44100), ([float("nan"), 0], 44100),
                              ([float("inf"), 0], 44100), ([1.1, 0], 44100),
                              ([[0, 0]], 44100), ([0, 0], 8000), (np.zeros(48001), 48000)):
            with self.subTest(rate=rate), self.assertRaises(ValueError):
                measure_window(samples, rate)

    def test_source_identity_fixed_window_and_bounds_remain_explicit(self):
        stream = io.BytesIO()
        with wave.open(stream, "wb") as output:
            output.setparams((1, 2, 44100, 0, "NONE", "not compressed"))
            signal = (10000 * np.sin(2 * np.pi * 100 * np.arange(24255) / 44100)).astype("<i2")
            output.writeframes(signal.tobytes())
        payload = stream.getvalue()
        digest = hashlib.sha256(payload).hexdigest()
        report = diagnose(payload, digest, 44100)
        self.assertEqual(24255, report["selectedFrames"])
        self.assertEqual(13230, report["fixed100To400ms"]["frames"])
        self.assertFalse(report["transformedSource"])
        self.assertFalse(report["originalAssessmentsSuperseded"])
        self.assertFalse(report["releaseEligible"])
        with self.assertRaisesRegex(ValueError, "digest"):
            diagnose(payload, "0" * 64, 44100)
        for start, frames in ((-1, 24255), (1, 24255), (0, 17639), (False, 24255)):
            with self.subTest(start=start, frames=frames), self.assertRaisesRegex(ValueError, "slice"):
                diagnose(payload, digest, 44100, start=start, frames=frames)


if __name__ == "__main__":
    unittest.main()
