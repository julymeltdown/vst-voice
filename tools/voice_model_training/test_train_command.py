import hashlib
from pathlib import Path
import tempfile
import unittest

from tools.voice_model_training.__main__ import encode_report, publish_new
from tools.voice_model_training.train import load_targets, model_settings


class TrainingCommandTests(unittest.TestCase):
    def test_closed_bounded_model_configuration(self):
        config = dict(formatId="com.project-seam.ddpm-training-config", schemaVersion=1,
                      hiddenSize=32, encoderLayers=1, channels=32, layers=2, timesteps=8,
                      seed=17, learningRate=.001, maximumUpdates=3, maximumSeconds=60, loss="l2")
        result = model_settings(config)
        self.assertEqual(result["hidden_size"], 32)
        self.assertFalse(result["use_shallow_diffusion"])
        self.assertFalse(result["use_breathiness_embed"])
        conditioned = model_settings(config | dict(schemaVersion=2, conditioningControls=["breathiness"]))
        self.assertTrue(conditioned["use_breathiness_embed"])
        self.assertFalse(conditioned["use_variance_scaling"])
        for controls in (["breathiness", "breathiness"], ["tension"], "breathiness"):
            with self.assertRaises(ValueError):
                model_settings(config | dict(schemaVersion=2, conditioningControls=controls))
        for update in (dict(hiddenSize=33), dict(hiddenSize=1024), dict(encoderLayers=True),
                       dict(timesteps=1001), dict(seed=-1), dict(learningRate=float("nan")),
                       dict(maximumUpdates=0), dict(maximumSeconds=0), dict(loss="other"),
                       dict(executable="arbitrary-code"), dict(schemaVersion=True)):
            with self.assertRaises(ValueError): model_settings(config | update)

    def test_schema3_auxiliary_objective_configuration(self):
        config = dict(formatId="com.project-seam.ddpm-training-config", schemaVersion=3,
                      hiddenSize=32, encoderLayers=1, channels=32, layers=2, timesteps=8,
                      seed=17, learningRate=.001, maximumUpdates=3, maximumSeconds=60, loss="l1",
                      conditioningControls=["breathiness"],
                      auxiliaryObjective=dict(kind="unvoiced-clean-mel-shape-level", weight=0.05,
                                              unvoicedSymbols=["h", "f", "k", "s", "t", "ch", "ts"]))
        result = model_settings(config)
        self.assertTrue(result["use_breathiness_embed"])
        self.assertNotIn("auxiliaryObjective", result)
        for auxiliary in (dict(kind="other", weight=.05, unvoicedSymbols=["s"]),
                          dict(kind="unvoiced-clean-mel-shape-level", weight=-.1, unvoicedSymbols=["s"]),
                          dict(kind="unvoiced-clean-mel-shape-level", weight=float("nan"), unvoicedSymbols=["s"]),
                          dict(kind="unvoiced-clean-mel-shape-level", weight=.05, unvoicedSymbols=[]),
                          dict(kind="unvoiced-clean-mel-shape-level", weight=.05, unvoicedSymbols=["s", "s"]),
                          dict(kind="unvoiced-clean-mel-shape-level", weight=.05, unvoicedSymbols=[""]),
                          dict(kind="unvoiced-clean-mel-shape-level", weight=.05, unvoicedSymbols=[123]),
                          dict(kind="unvoiced-clean-mel-shape-level", weight=.05),
                          "unvoiced-clean-mel-shape-level"):
            with self.assertRaises(ValueError):
                model_settings(config | dict(auxiliaryObjective=auxiliary))
        missing_auxiliary = {key: value for key, value in config.items() if key != "auxiliaryObjective"}
        for value in (missing_auxiliary, config | dict(schemaVersion=2),
                      config | dict(schemaVersion=1),
                      config | dict(schemaVersion=3, conditioningControls=["tension"])):
            with self.assertRaises(ValueError):
                model_settings(value)

    def test_target_capture_and_rejections(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            record = dict(profileSha256="a" * 64)
            publish_new(root / "record.json", record)
            row = dict(sourceId="source", record="record.json",
                       recordSha256=hashlib.sha256(encode_report(record)).hexdigest(), binary="mel.f32le")
            config = dict(formatId="com.project-seam.training-target-inventory", schemaVersion=1,
                          profileSha256="a" * 64, targets=[row])
            def load(value):
                payload = encode_report(value)
                (root / "targets.json").write_bytes(payload)
                return load_targets(root / "targets.json", hashlib.sha256(payload).hexdigest())
            targets, profile = load(config)
            self.assertEqual(profile, "a" * 64)
            self.assertEqual(targets["source"][1], root / "mel.f32le")
            for value in (config | dict(targets=[row, row]), config | dict(profileSha256="b" * 64),
                          config | dict(targets=[row | dict(binary="../escape")]),
                          config | dict(targets=[row | dict(recordSha256="0" * 64)]),
                          config | dict(targets=[])):
                with self.assertRaises(ValueError): load(value)


if __name__ == "__main__":
    unittest.main()
