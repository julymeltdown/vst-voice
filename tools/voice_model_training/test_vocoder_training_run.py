"""Orchestration failures; mocks are not voice or real-source qualification."""
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

from tools.voice_model_training.vocoder_training_run import train_reviewed_vocoder_epoch


class VocoderEpochTests(unittest.TestCase):
    def test_complete_coverage_and_refresh_before_publication(self):
        snapshot = dict(schemaVersion=3, preparationIssues=[], sourcePermissionsAdmitted=True,
            labelsAdmitted=True, expiresAt=200, datasetSha256="a" * 64,
            sources=[dict(sourceId="s", frameCount=2000)], conditioning=[dict(sourceId="s", frameCount=8)],
            bindings=dict(split=dict(groups=[dict(partition="train", sourceIds=["s"])])))
        inputs = dict.fromkeys(("permission_config", "permission_hash", "label_config", "label_hash", "root",
            "rights_review", "rights_policy", "rights_anchor", "label_review", "label_policy", "label_anchor", "seed", "held_out_songs"))
        batch = dict(sourceId="s", partition="train", datasetSha256="a" * 64, profileSha256="b" * 64,
            frameOffset=0, mel=SimpleNamespace(shape=(1, 80, 8)), f0=None, pcm=None, hopSize=256, validSamples=2000)
        prefix = "tools.voice_model_training.vocoder_training_run."
        with tempfile.TemporaryDirectory() as root, patch(prefix + "time.time", return_value=100), \
                patch(prefix + "assemble_dataset", return_value=snapshot) as refresh, \
                patch(prefix + "iter_vocoder_batches", side_effect=lambda *a, **k: iter([batch])) as batches, \
                patch(prefix + "vocoder_gan_step", return_value=dict(generatorLoss=2., discriminatorLoss=3.)) as step, \
                patch(prefix + "publish_vocoder_checkpoint") as publish:
            options = dict(dataset_inputs=inputs, conditioning_directory=Path(root), targets={}, pcm_sources={},
                expected_profile_sha256="b" * 64, output=Path(root) / "out", run_metadata={},
                reconstruction_loss=lambda a, b: None, objective_id="fixture", maximum_updates=1)
            def finish(*args, **kwargs):
                kwargs["before_publish"]()
                return kwargs["epoch"]
            publish.side_effect = finish
            result = train_reviewed_vocoder_epoch(None, [], None, None, **options)
            self.assertEqual(result["coveredSourceSamples"], {"s": 2000})
            self.assertEqual(result["validSamples"], 2000)
            self.assertEqual(result["meanGeneratorLoss"], 2.)
            self.assertEqual(refresh.call_count, 3)
            for supplied in ([], [batch, batch], [dict(batch, validSamples=1999)],
                             [dict(batch, partition="test")], [dict(batch, frameOffset=1)]):
                batches.side_effect = lambda *a, rows=supplied, **k: iter(rows)
                publish.reset_mock()
                with self.assertRaises(ValueError):
                    train_reviewed_vocoder_epoch(None, [], None, None, **options)
                publish.assert_not_called()
            batches.side_effect = lambda *a, **k: iter([batch])
            changed = dict(snapshot, datasetSha256="c" * 64)
            for sequence in ([snapshot, changed], [snapshot, snapshot, changed]):
                refresh.side_effect = sequence
                with self.assertRaisesRegex(ValueError, "identity changed"):
                    train_reviewed_vocoder_epoch(None, [], None, None, **options)
            refresh.side_effect = None
            step.reset_mock()
            with self.assertRaises(RuntimeError):
                train_reviewed_vocoder_epoch(None, [], None, None, **options, cancelled=lambda: True)
            step.assert_not_called()
            with self.assertRaisesRegex(ValueError, "fresh admission"):
                train_reviewed_vocoder_epoch(None, [], None, None, **options, expected_dataset_sha256="c" * 64)
            step.assert_not_called()


if __name__ == "__main__":
    unittest.main()
