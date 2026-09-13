from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.voice_model_training.__main__ import publish_new
from tools.voice_model_training.epochs import run_reviewed_epochs


class EpochRunTests(unittest.TestCase):
    def test_lineage_limits_and_incomplete_run(self):
        with tempfile.TemporaryDirectory() as temporary, \
                patch("tools.voice_model_training.epochs.train_reviewed_epoch") as train:
            root = Path(temporary)
            def epoch(*args, **kwargs):
                destination = kwargs["output"]
                destination.mkdir()
                receipt = dict(checkpointBytes=100, checkpointSha256="a" * 64,
                               metadata=dict(datasetSha256="b" * 64, run=kwargs["run_metadata"]),
                               epoch=dict(epochComplete=True, updates=1, meanLoss=.5))
                publish_new(destination / "checkpoint.json", receipt)
                return receipt
            train.side_effect = epoch
            options = dict(epochs=2, completed_epochs=0, parent_receipt_sha256=None,
                           metadata={}, epoch_options=dict(maximum_seconds=60), maximum_total_checkpoint_bytes=200)
            result = run_reviewed_epochs(None, None, output=root / "success", **options)
            self.assertEqual(result["completedEpochs"], 2)
            self.assertTrue((root / "success" / "run.json").is_file())
            calls = train.call_args_list
            self.assertEqual(calls[1].kwargs["expected_dataset_sha256"], "b" * 64)
            self.assertEqual(calls[1].kwargs["maximum_checkpoint_bytes"], 100)
            self.assertEqual(calls[1].kwargs["run_metadata"]["parentReceiptSha256"],
                             result["checkpoints"][0]["receiptSha256"])
            with self.assertRaises(RuntimeError):
                run_reviewed_epochs(None, None, output=root / "partial",
                                    **(options | dict(maximum_total_checkpoint_bytes=100)))
            self.assertTrue((root / "partial" / "epoch-000001" / "checkpoint.json").exists())
            self.assertFalse((root / "partial" / "run.json").exists())
            for update in (dict(epochs=0), dict(epochs=True), dict(epochs=1001),
                           dict(maximum_run_seconds=float("nan")), dict(maximum_total_checkpoint_bytes=0)):
                with self.assertRaises(ValueError):
                    run_reviewed_epochs(None, None, output=root / "invalid", **(options | update))
                self.assertFalse((root / "invalid").exists())


if __name__ == "__main__":
    unittest.main()
