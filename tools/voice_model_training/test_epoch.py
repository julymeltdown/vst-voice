import unittest
from unittest.mock import patch

from tools.voice_model_training.optimization import run_acoustic_epoch


class EpochTests(unittest.TestCase):
    def test_exact_core_coverage_with_context(self):
        options = dict(expected_dataset_sha256="a" * 64, expected_profile_sha256="b" * 64,
                       vocabulary_size=1, maximum_updates=4, expected_source_frames={"s": 4})
        first = dict(datasetSha256="a" * 64, profileSha256="b" * 64, sourceId="s", phraseAnalysisFrames=4,
                     coreFrameOffset=0, coreFrameCount=2, frameOffset=0, lossMask=[True, True, False])
        second = dict(first, coreFrameOffset=2, frameOffset=1, lossMask=[False, True, True])
        def run(batches):
            return run_acoustic_epoch(None, None, batches, **options)
        with patch("tools.voice_model_training.optimization.acoustic_training_step",
                   return_value=dict(validSamples=512, loss=1., sourceId="s")):
            result = run([first, second])
            self.assertTrue(result["coverageVerified"])
            self.assertEqual(result["coveredSourceFrames"], {"s": 4})
            for rows in ([first], [first, first], [second, first],
                         [first, dict(second, lossMask=[True, True, True])],
                         [dict(first, sourceId="unknown")]):
                with self.assertRaises(ValueError): run(rows)

    def test_weighted_aggregation_and_incomplete_attempts(self):
        batch = dict(datasetSha256="a" * 64, profileSha256="b" * 64)
        options = dict(expected_dataset_sha256="a" * 64, expected_profile_sha256="b" * 64,
                       vocabulary_size=1, maximum_updates=2)
        def run(batches, **extra):
            return run_acoustic_epoch(None, None, batches, **(options | extra))
        with patch("tools.voice_model_training.optimization.acoustic_training_step") as step:
            step.side_effect = [dict(validSamples=100, loss=1., sourceId="s"), dict(validSamples=300, loss=3., sourceId="s")]
            result = run([batch, batch])
            self.assertEqual(result["meanLoss"], 2.5)
            self.assertEqual(result["updates"], 2)
            self.assertTrue(result["epochComplete"])
            self.assertFalse(result["trainingAdmitted"])
            step.side_effect = None
            step.return_value = dict(validSamples=100, loss=1., sourceId="s")
            with self.assertRaises(ValueError): run([batch, batch], maximum_updates=1)
            with self.assertRaises(ValueError): run([])
            with self.assertRaises(ValueError): run([dict(batch, datasetSha256="c" * 64)])
            before = step.call_count
            with self.assertRaises(RuntimeError): run([batch], cancelled=lambda: True)
            self.assertEqual(step.call_count, before)
            def broken():
                yield batch
                raise OSError("late shard failure")
            with self.assertRaises(OSError): run(broken())


if __name__ == "__main__":
    unittest.main()
