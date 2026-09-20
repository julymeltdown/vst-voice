"""The score-derived breathiness prior, which is what makes the channel usable."""
import unittest

from tools.voice_model_training.breathiness_prior import PERIODIC_MARGIN, derive_prior


def estimate(by_symbol, **updates):
    value = dict(formatId="com.project-seam.aperiodicity-estimate", schemaVersion=1,
                 policy="Zero-crossing rate and spectral flatness", corpusSha256="a" * 64,
                 bySymbol=by_symbol)
    return value | updates


class BreathinessPriorTests(unittest.TestCase):
    def test_noise_like_phones_keep_their_measured_value(self):
        prior = derive_prior(estimate({"s": dict(mean=0.477, windows=648),
                                      "a": dict(mean=0.0388, windows=1424)}))
        rows = {row["symbol"]: row for row in prior["symbols"]}
        self.assertAlmostEqual(rows["s"]["breathiness"], 0.477)
        self.assertAlmostEqual(rows["a"]["breathiness"], 0.0388)
        self.assertEqual(rows["s"]["periodic"], "MEASURED")
        self.assertFalse(prior["supervisionAdmitted"])
        self.assertFalse(prior["singerQualified"])
        self.assertFalse(prior["releaseEligible"])

    def test_effectively_periodic_phones_are_pinned_to_zero(self):
        # A tiny measured mean must not leak a small arbitrary noise floor into a
        # voiced render; pinned zero keeps unvoiced behaviour unchanged.
        prior = derive_prior(estimate({"pau": dict(mean=PERIODIC_MARGIN / 2, windows=24),
                                       "m": dict(mean=PERIODIC_MARGIN, windows=827),
                                       "z": dict(mean=0.0, windows=10)}))
        for row in prior["symbols"]:
            with self.subTest(symbol=row["symbol"]):
                self.assertEqual(row["breathiness"], 0.0)
                self.assertEqual(row["periodic"], "PINNED_PERIODIC")

    def test_minimum_windows_gates_unmeasured_symbols(self):
        table = {"s": dict(mean=0.477, windows=648), "q": dict(mean=0.9, windows=2)}
        gated = derive_prior(estimate(table), minimum_windows=100)
        rows = {row["symbol"]: row for row in gated["symbols"]}
        self.assertAlmostEqual(rows["s"]["breathiness"], 0.477)
        self.assertEqual(rows["q"]["breathiness"], 0.0)
        self.assertFalse(rows["q"]["measured"])
        self.assertEqual(rows["q"]["sourceMean"], 0.9)

    def test_rejects_malformed_estimates(self):
        for value in (None, "estimate", {}, estimate({}),
                      estimate({"s": dict(mean=0.5)}),
                      estimate({"s": dict(mean=0.5, windows=1, extra=1)}),
                      estimate({"s": dict(mean=1.5, windows=1)}),
                      estimate({"s": dict(mean=-0.1, windows=1)}),
                      estimate({"s": dict(mean=0.5, windows=-1)}),
                      estimate({"": dict(mean=0.5, windows=1)}),
                      estimate({chr(127): dict(mean=0.5, windows=1)}),
                      estimate({"s": dict(mean=0.5, windows=1)}, schemaVersion=2)):
            with self.subTest(value=value), self.assertRaises(ValueError):
                derive_prior(value)
        with self.assertRaises(ValueError):
            derive_prior(estimate({"s": dict(mean=0.5, windows=1)}), minimum_windows=0)

    def test_symbols_are_ordered_and_bounded(self):
        prior = derive_prior(estimate({"z": dict(mean=0.2, windows=1),
                                       "a": dict(mean=0.1, windows=1),
                                       "m": dict(mean=0.3, windows=1)}))
        self.assertEqual([row["symbol"] for row in prior["symbols"]], ["a", "m", "z"])
        self.assertTrue(all(0.0 <= row["breathiness"] <= 1.0 for row in prior["symbols"]))


if __name__ == "__main__":
    unittest.main()
