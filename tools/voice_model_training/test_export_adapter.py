import unittest

from tools.voice_model_training.export_adapter import prepare_acoustic_export


class ExportAdapterTests(unittest.TestCase):
    def test_incompatible_profiles_reject_before_upstream_import(self):
        profile = dict(profileId="seam-full-hop-slaney-v1", amplitudeScale="ln-amplitude",
                       layout="TF", dtype="float32-le", bins=80)
        config = dict(diffusion_type="ddpm", use_shallow_diffusion=False)
        for update in (dict(amplitudeScale="log10"), dict(layout="FT"), dict(bins=True),
                       dict(bins=513), dict(dtype="float64")):
            with self.assertRaises(ValueError):
                prepare_acoustic_export(None, configuration=config, acoustic_profile=profile | update)
        for update in (dict(mel_base="10"), dict(use_shallow_diffusion=True),
                       dict(diffusion_type="reflow"), dict(use_spk_id=True), dict(use_lang_id=True)):
            with self.assertRaises(ValueError):
                prepare_acoustic_export(None, configuration=config | update, acoustic_profile=profile)


if __name__ == "__main__":
    unittest.main()
