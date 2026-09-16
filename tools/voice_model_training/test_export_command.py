import copy
import hashlib
import json
import unittest

from tools.voice_model_training.export import export_identity
from tools.voice_model_training.train import model_settings, REVISION


class ExportCommandTests(unittest.TestCase):
    def test_checkpoint_profile_architecture_and_vocabulary_binding(self):
        settings = dict(formatId="com.project-seam.ddpm-training-config", schemaVersion=1,
                        hiddenSize=32, encoderLayers=1, channels=32, layers=2, timesteps=8,
                        seed=17, learningRate=.001, maximumUpdates=3, maximumSeconds=60, loss="l2")
        profile = dict(bins=80)
        digest = hashlib.sha256(json.dumps(profile, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        state = dict(metadata=dict(profileSha256=digest, run=dict(revision=REVISION, settings=settings,
                     configuration=model_settings(settings), vocabulary=["a"])), epoch=dict(profileSha256=digest))
        self.assertEqual(export_identity(state, profile)[1], ["a"])
        conditioned_settings = settings | dict(schemaVersion=2, conditioningControls=["breathiness"])
        conditioned = copy.deepcopy(state)
        conditioned["metadata"]["run"]["settings"] = conditioned_settings
        conditioned["metadata"]["run"]["configuration"] = model_settings(conditioned_settings)
        self.assertTrue(export_identity(conditioned, profile)[0]["use_breathiness_embed"])
        for field, value in (("revision", "wrong"), ("configuration", {}), ("vocabulary", ["a", "a"]),
                             ("vocabulary", [])):
            changed = copy.deepcopy(state)
            changed["metadata"]["run"][field] = value
            with self.assertRaises(ValueError): export_identity(changed, profile)
        with self.assertRaises(ValueError): export_identity(state, dict(bins=81))


if __name__ == "__main__":
    unittest.main()
