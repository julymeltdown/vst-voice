import copy
import hashlib
import json
import unittest

from tools.voice_model_training.export import export_identity, published_vocabulary
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

    def test_published_vocabulary_is_pad_prefixed_and_keeps_checkpoint_order(self):
        # The trained embedding reserves token zero for padding, which is why the
        # model is built with len(vocabulary) + 1. A published list without that
        # leading entry describes the same model with every ID shifted by one, and
        # the bundle step refuses it, so the published form is asserted here.
        self.assertEqual(published_vocabulary(["a", "i", "u"]), ["<PAD>", "a", "i", "u"])
        self.assertEqual(published_vocabulary(["s"]), ["<PAD>", "s"])
        # An apostrophe- and case-bearing phone survives unchanged and in order.
        published = published_vocabulary(["N", "a", "g", "ts"])
        self.assertEqual(published[1:], ["N", "a", "g", "ts"])
        self.assertEqual(len(set(published)), len(published))
        # A captured padding token would collide with the reserved slot.
        with self.assertRaises(ValueError): published_vocabulary(["a", "<PAD>"])
        with self.assertRaises(ValueError): published_vocabulary(["<PAD>"])


if __name__ == "__main__":
    unittest.main()
