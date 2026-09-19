"""Orchestration failures; mocks are not voice or real-source qualification."""
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

from tools.voice_model_training.vocoder_training_run import train_reviewed_vocoder_epoch


class VocoderEpochTests(unittest.TestCase):
    def setUp(self):
        storage = patch("tools.voice_model_training.vocoder_training_run.require_disk_headroom")
        self.storage = storage.start()
        self.addCleanup(storage.stop)

    def test_held_out_selection_and_batch_identity_cannot_bypass_partition(self):
        import numpy as np
        snapshot = dict(schemaVersion=3, preparationIssues=[], sourcePermissionsAdmitted=True,
            labelsAdmitted=True, expiresAt=200, datasetSha256="a" * 64,
            sources=[dict(sourceId="s", frameCount=1024),
                     dict(sourceId="h", frameCount=1000, sourceSha256="c" * 64, audioSha256="d" * 64)],
            conditioning=[dict(sourceId="s", frameCount=4), dict(sourceId="h", frameCount=4)],
            bindings=dict(split=dict(groups=[dict(partition="train", sourceIds=["s"]),
                                             dict(partition="test", sourceIds=["h"])])))
        inputs = dict.fromkeys(("permission_config", "permission_hash", "label_config", "label_hash", "root",
            "rights_review", "rights_policy", "rights_anchor", "label_review", "label_policy", "label_anchor", "seed", "held_out_songs"))
        train = dict(sourceId="s", partition="train", datasetSha256="a" * 64, profileSha256="b" * 64,
            frameOffset=0, mel=np.zeros((1, 80, 4), np.float32), f0=None, pcm=np.zeros(1024),
            hopSize=256, validSamples=1024)
        held = dict(train, sourceId="h", partition="test", validSamples=1000, phraseAnalysisFrames=4,
                    sourceSha256="c" * 64, audioSha256="d" * 64)
        profile = dict(profileId="seam-full-hop-slaney-v1", sampleRate=48000, hopSize=256,
                       tailPadding="zero-to-whole-hop")
        prefix = "tools.voice_model_training.vocoder_training_run."
        with tempfile.TemporaryDirectory() as root, patch(prefix + "time.time", return_value=100), \
                patch(prefix + "assemble_dataset", return_value=snapshot), \
                patch(prefix + "iter_vocoder_batches") as batches, \
                patch(prefix + "vocoder_gan_step", return_value=dict(generatorLoss=1., discriminatorLoss=1.)) as step, \
                patch(prefix + "publish_vocoder_checkpoint") as publish:
            options = dict(dataset_inputs=inputs, conditioning_directory=Path(root),
                targets={"h": (dict(profile=profile), None)}, pcm_sources={},
                expected_profile_sha256="b" * 64, output=Path(root) / "checkpoint", run_metadata={},
                reconstruction_loss=lambda a, b: None, objective_id="fixture", maximum_updates=1)
            batches.side_effect = lambda *a, **k: iter([train] if k["partition"] == "train" else [held])
            observed = []
            def evaluate(**kwargs):
                observed.extend(kwargs["items"])
                return dict(summary={})
            with patch(prefix + "evaluate_held_out_reconstruction", side_effect=evaluate):
                train_reviewed_vocoder_epoch(None, [], None, None, held_out_items=["h"],
                                            training_segment_frames=16, **options)
            self.assertEqual(observed, [held])
            held_calls = [call for call in batches.call_args_list if call.kwargs["partition"] == "test"]
            self.assertEqual(held_calls[0].kwargs["batch_frames"], 4096)
            self.assertNotIn("training_segment_frames", held_calls[0].kwargs)
            step.reset_mock(); publish.reset_mock()
            for selection in (["s"], ["unknown"], ["h", "h"], [], [held]):
                with self.subTest(selection=selection), self.assertRaises(ValueError):
                    train_reviewed_vocoder_epoch(None, [], None, None, held_out_items=selection, **options)
                step.assert_not_called()
            for supplied in ([], [held, held], [dict(held, sourceSha256="e" * 64)],
                             [dict(held, partition="train")], [dict(held, validSamples=999)],
                             [dict(held, frameOffset=1)]):
                batches.side_effect = lambda *a, rows=supplied, **k: iter([train] if k["partition"] == "train" else rows)
                with self.subTest(rows=supplied), self.assertRaises(ValueError):
                    train_reviewed_vocoder_epoch(lambda m, f: np.zeros(1024), [], None, None,
                                                held_out_items=["h"], **options)
                publish.assert_not_called()

    def test_segments_require_every_sample_once_and_count_updates_separately(self):
        frames, samples = 40, 40 * 256 - 7
        snapshot = dict(schemaVersion=3, preparationIssues=[], sourcePermissionsAdmitted=True,
            labelsAdmitted=True, expiresAt=200, datasetSha256="a" * 64,
            sources=[dict(sourceId="s", frameCount=samples)], conditioning=[dict(sourceId="s", frameCount=frames)],
            bindings=dict(split=dict(groups=[dict(partition="train", sourceIds=["s"])])))
        inputs = dict.fromkeys(("permission_config", "permission_hash", "label_config", "label_hash", "root",
            "rights_review", "rights_policy", "rights_anchor", "label_review", "label_policy", "label_anchor",
            "seed", "held_out_songs"))
        batches = [dict(sourceId="s", partition="train", datasetSha256="a" * 64, profileSha256="b" * 64,
            frameOffset=begin, mel=SimpleNamespace(shape=(1, 80, end-begin)), f0=None, pcm=None,
            hopSize=256, validSamples=min((end-begin)*256, samples-begin*256))
            for begin, end in ((0, 14), (14, 27), (27, 40))]
        prefix = "tools.voice_model_training.vocoder_training_run."
        with tempfile.TemporaryDirectory() as root, patch(prefix + "time.time", return_value=100), \
                patch(prefix + "assemble_dataset", return_value=snapshot), \
                patch(prefix + "iter_vocoder_batches", side_effect=lambda *a, **k: iter(batches)) as reader, \
                patch(prefix + "vocoder_gan_step", return_value=dict(generatorLoss=2., discriminatorLoss=3.)) as step, \
                patch(prefix + "publish_vocoder_checkpoint", side_effect=lambda *a, **k: k["epoch"]) as publish:
            options = dict(dataset_inputs=inputs, conditioning_directory=Path(root), targets={}, pcm_sources={},
                expected_profile_sha256="b" * 64, output=Path(root)/"out", run_metadata={},
                reconstruction_loss=lambda a,b: None, objective_id="fixture", maximum_updates=3,
                training_segment_frames=16)
            result = train_reviewed_vocoder_epoch(None, [], None, None, **options)
            self.assertEqual((result["updates"], result["sourceCount"], result["validSamples"]), (3, 1, samples))
            self.assertEqual(result["coveredSourceSamples"], {"s": samples})
            self.assertEqual(result["sourceUpdates"], {"s": 3})
            self.assertEqual(reader.call_args.kwargs["training_segment_frames"], 16)
            for rows in (batches[:1], batches[::-1], [batches[0], batches[0], batches[2]],
                         [batches[0], dict(batches[1], frameOffset=15), batches[2]],
                         [*batches[:2], dict(batches[2], validSamples=13*256)]):
                reader.side_effect=lambda *a, rows=rows, **k: iter(rows)
                publish.reset_mock()
                with self.subTest(rows=rows), self.assertRaises(ValueError):
                    train_reviewed_vocoder_epoch(None, [], None, None, **options)
                publish.assert_not_called()
            step.reset_mock()
            with self.assertRaisesRegex(ValueError, "update budget"):
                train_reviewed_vocoder_epoch(None, [], None, None, **(options | dict(maximum_updates=2)))
            step.assert_not_called()

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
            events = []
            train_reviewed_vocoder_epoch(None, [], None, None, **options, on_progress=events.append)
            self.assertEqual([event["stage"] for event in events],
                ["admission-started", "updates-started", "updates-progress", "readmission-started", "checkpoint-started"])
            update = events[2]
            self.assertEqual((update["completedUpdates"], update["totalUpdates"], update["validSamples"]), (1, 1, 2000))
            self.assertEqual(update["meanGeneratorLoss"], result["meanGeneratorLoss"])
            self.assertTrue(all(event["elapsedSeconds"] >= 0 for event in events))
            publish.reset_mock()
            def reject_progress(event):
                if event["stage"] == "updates-progress":
                    raise RuntimeError("progress sink failed")
            with self.assertRaisesRegex(RuntimeError, "progress sink failed"):
                train_reviewed_vocoder_epoch(None, [], None, None, **options, on_progress=reject_progress)
            publish.assert_not_called()
            train_reviewed_vocoder_epoch(None, [], None, None, **options, maximum_checkpoint_total_bytes=12345)
            self.assertEqual(publish.call_args.kwargs["maximum_total_bytes"], 12345)
            step.reset_mock()
            publish.reset_mock()
            self.storage.side_effect = OSError(28, "no checkpoint headroom")
            with self.assertRaisesRegex(OSError, "headroom"):
                train_reviewed_vocoder_epoch(None, [], None, None, **options)
            step.assert_not_called()
            publish.assert_not_called()
            # Space can disappear during an update. Refuse publication as well.
            self.storage.side_effect = [None, None, OSError(28, "headroom lost")]
            with self.assertRaisesRegex(OSError, "headroom lost"):
                train_reviewed_vocoder_epoch(None, [], None, None, **options)
            step.assert_called_once()
            publish.assert_not_called()
            self.storage.side_effect = None
            step.reset_mock()
            for bound in (0, True, 1024 * 1024 * 1024 + 1):
                with self.assertRaises(ValueError):
                    train_reviewed_vocoder_epoch(None, [], None, None, **options, maximum_checkpoint_total_bytes=bound)
                step.assert_not_called()
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
