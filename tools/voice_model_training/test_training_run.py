"""Orchestration contract tests; mocked training is not singer-quality evidence."""
from copy import deepcopy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.voice_model_training.training_run import train_reviewed_epoch


class ReviewedRunTests(unittest.TestCase):
    def test_refresh_coverage_and_publication_failures(self):
        snapshot = dict(schemaVersion=3, preparationIssues=[], sourcePermissionsAdmitted=True,
                        labelsAdmitted=True, expiresAt=200, datasetSha256="a" * 64,
                        vocabulary=["a"], conditioning=[dict(sourceId="s", frameCount=8)],
                        bindings=dict(split=dict(groups=[dict(partition="train", sourceIds=["s"])])))
        inputs = dict.fromkeys(("permission_config", "permission_hash", "label_config", "label_hash",
                               "root", "rights_review", "rights_policy", "rights_anchor",
                               "label_review", "label_policy", "label_anchor", "seed", "held_out_songs"))
        with tempfile.TemporaryDirectory() as temp, \
                patch("tools.voice_model_training.training_run.time.time", return_value=100), \
                patch("tools.voice_model_training.training_run.assemble_dataset", return_value=snapshot) as admit, \
                patch("tools.voice_model_training.training_run.iter_supervised_batches", return_value=iter([])), \
                patch("tools.voice_model_training.training_run.run_acoustic_epoch", return_value={}) as epoch, \
                patch("tools.voice_model_training.training_run.publish_checkpoint") as publish:
            options = dict(dataset_inputs=inputs, conditioning_directory=Path(temp) / "shards",
                           targets={}, expected_profile_sha256="b" * 64, output=Path(temp) / "checkpoint",
                           run_metadata={}, maximum_updates=1)
            def finish(*args, **kwargs):
                kwargs["before_publish"]()
                return {"published": True}
            publish.side_effect = finish
            self.assertEqual(train_reviewed_epoch(None, None, **options), {"published": True})
            self.assertEqual(admit.call_count, 3)
            self.assertTrue(admit.call_args.kwargs["reuse_conditioning"])
            self.assertEqual(epoch.call_args.kwargs["expected_source_frames"], {"s": 8})
            conditioned_snapshot = deepcopy(snapshot)
            conditioned_snapshot["conditioning"][0].update(
                conditioningRevision=2, conditioningControls=["breathiness"])
            admit.return_value = conditioned_snapshot
            conditioned_options = options | dict(
                run_metadata={"configuration": {"use_breathiness_embed": True}})
            self.assertEqual(train_reviewed_epoch(None, None, **conditioned_options), {"published": True})
            admit.return_value = snapshot
            with self.assertRaisesRegex(ValueError, "revision-2 supervision"):
                train_reviewed_epoch(None, None, **conditioned_options)
            admit.return_value = conditioned_snapshot
            with self.assertRaisesRegex(ValueError, "silently discard"):
                train_reviewed_epoch(None, None, **options)
            admit.return_value = snapshot
            epoch.reset_mock()
            publish.reset_mock()
            with self.assertRaisesRegex(ValueError, "checkpoint dataset"):
                train_reviewed_epoch(None, None, **options, expected_dataset_sha256="c" * 64)
            epoch.assert_not_called()
            publish.assert_not_called()
            for changed in (dict(snapshot, preparationIssues=[{"code": "missing-partition"}]),
                            dict(snapshot, expiresAt=100),
                            dict(snapshot, conditioning=[dict(sourceId="s", frameCount=4097)])):
                admit.return_value = changed
                epoch.reset_mock()
                publish.reset_mock()
                with self.assertRaises(ValueError): train_reviewed_epoch(None, None, **options)
                epoch.assert_not_called()
                publish.assert_not_called()
            admit.return_value = snapshot
            changed = deepcopy(snapshot)
            changed["datasetSha256"] = "c" * 64
            admit.side_effect = [snapshot, changed]
            publish.reset_mock()
            with self.assertRaisesRegex(ValueError, "identity changed"):
                train_reviewed_epoch(None, None, **options)
            publish.assert_not_called()
            admit.side_effect = [snapshot, snapshot, changed]
            with self.assertRaisesRegex(ValueError, "identity changed"):
                train_reviewed_epoch(None, None, **options)
            admit.side_effect = None
            epoch.side_effect = OSError("late target failure")
            publish.reset_mock()
            with self.assertRaises(OSError): train_reviewed_epoch(None, None, **options)
            publish.assert_not_called()
            epoch.reset_mock()
            with self.assertRaises(RuntimeError):
                train_reviewed_epoch(None, None, **(options | dict(cancelled=lambda: True)))
            epoch.assert_not_called()


if __name__ == "__main__":
    unittest.main()
