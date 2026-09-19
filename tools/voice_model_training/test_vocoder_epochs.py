import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.voice_model_training.__main__ import publish_new
from tools.voice_model_training.vocoder_epochs import run_reviewed_vocoder_epochs


class VocoderEpochRunTests(unittest.TestCase):
    def options(self, **updates):
        return dict(epochs=2, completed_epochs=0, parent_receipt_sha256=None,
                    metadata=dict(configuration={"generator": "test-only"}, trainingRevision="fixture"),
                    epoch_options=dict(maximum_seconds=60), maximum_total_checkpoint_bytes=200) | updates

    def epoch(self, *args, **kwargs):
        destination = kwargs["output"]
        destination.mkdir()
        receipt = dict(checkpointBytes=100, checkpointSha256="a" * 64,
                       metadata=dict(datasetSha256="b" * 64, run=kwargs["run_metadata"]),
                       epoch=dict(epochComplete=True, coverageVerified=True, updates=1,
                                  meanGeneratorLoss=.5, meanDiscriminatorLoss=.25))
        publish_new(destination / "checkpoint.json", receipt)
        return receipt

    def test_lineage_remaining_bound_reconstruction_and_metadata(self):
        with tempfile.TemporaryDirectory() as temporary, \
                patch("tools.voice_model_training.vocoder_epochs.train_reviewed_vocoder_epoch",
                      side_effect=self.epoch) as train:
            root = Path(temporary)
            options = self.options(epoch_options=dict(maximum_seconds=60, held_out_items=["held-out"]))
            result = run_reviewed_vocoder_epochs(None, [], None, None, output=root / "run", **options)
            self.assertEqual(result["completedEpochs"], 2)
            self.assertEqual(result["checkpointBytes"], 200)
            self.assertFalse(result["trainingAdmitted"])
            self.assertFalse(result["releaseEligible"])
            self.assertTrue((root / "run" / "run.json").is_file())
            first, second = [call.kwargs for call in train.call_args_list]
            self.assertEqual(first["maximum_checkpoint_total_bytes"], 200)
            self.assertEqual(second["maximum_checkpoint_total_bytes"], 100)
            self.assertEqual(first["maximum_checkpoint_file_bytes"], 512 * 1024 * 1024)
            self.assertEqual(first["run_metadata"], options["metadata"] | dict(completedEpochs=1, parentReceiptSha256=None))
            self.assertEqual(second["run_metadata"]["completedEpochs"], 2)
            self.assertEqual(second["run_metadata"]["parentReceiptSha256"], result["checkpoints"][0]["receiptSha256"])
            self.assertEqual(second["expected_dataset_sha256"], "b" * 64)
            self.assertEqual(first["reconstruction_directory"], root / "run" / "reconstruction-000001")
            self.assertTrue(second["reconstruction_directory"].is_dir())
            self.assertEqual(options["epoch_options"], dict(maximum_seconds=60, held_out_items=["held-out"]))

    def test_single_and_resumed_epoch_use_new_child_directory(self):
        with tempfile.TemporaryDirectory() as temporary, \
                patch("tools.voice_model_training.vocoder_epochs.train_reviewed_vocoder_epoch",
                      side_effect=self.epoch) as train:
            root = Path(temporary)
            result = run_reviewed_vocoder_epochs(None, [], None, None, output=root / "run",
                **self.options(epochs=1, completed_epochs=9, parent_receipt_sha256="c" * 64))
            self.assertEqual(result["completedEpochs"], 10)
            self.assertEqual(result["checkpoints"][0]["path"], "epoch-000010")
            self.assertEqual(train.call_args.kwargs["run_metadata"]["parentReceiptSha256"], "c" * 64)
            self.assertIsNone(train.call_args.kwargs["reconstruction_directory"])
            self.assertTrue((root / "run" / "run.json").exists())

    def test_progress_binds_epoch_and_reports_failure_without_completion(self):
        with tempfile.TemporaryDirectory() as temporary:
            events = []
            def epoch(*args, **kwargs):
                kwargs["on_progress"](dict(stage="updates-progress", completedUpdates=1, totalUpdates=2,
                                           elapsedSeconds=.5))
                if kwargs["run_metadata"]["completedEpochs"] == 2:
                    raise RuntimeError("training failed")
                return self.epoch(*args, **kwargs)
            with patch("tools.voice_model_training.vocoder_epochs.train_reviewed_vocoder_epoch", epoch):
                with self.assertRaisesRegex(RuntimeError, "training failed"):
                    run_reviewed_vocoder_epochs(None, [], None, None, output=Path(temporary) / "run",
                        **self.options(on_progress=events.append))
            self.assertEqual([event["stage"] for event in events], ["run-started", "epoch-started",
                "updates-progress", "epoch-completed", "epoch-started", "updates-progress", "epoch-failed"])
            self.assertEqual([event["epoch"] for event in events[1:]], [1, 1, 1, 2, 2, 2])
            self.assertEqual(events[-1]["errorType"], "RuntimeError")
            self.assertEqual(events[3]["retainedCheckpointBytes"], 100)
            self.assertEqual(len(events[3]["receiptSha256"]), 64)
            self.assertFalse((Path(temporary) / "run" / "run.json").exists())

    def test_failure_and_budget_exhaustion_preserve_last_complete_checkpoint(self):
        for failure in (RuntimeError("fixture failure"), KeyboardInterrupt()):
            with self.subTest(failure=type(failure).__name__), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                def interrupted(*args, **kwargs):
                    if kwargs["run_metadata"]["completedEpochs"] == 2:
                        raise failure
                    return self.epoch(*args, **kwargs)
                with patch("tools.voice_model_training.vocoder_epochs.train_reviewed_vocoder_epoch", interrupted):
                    with self.assertRaises(type(failure)):
                        run_reviewed_vocoder_epochs(None, [], None, None, output=root / "run", **self.options())
                self.assertTrue((root / "run" / "epoch-000001" / "checkpoint.json").exists())
                self.assertFalse((root / "run" / "run.json").exists())
        with tempfile.TemporaryDirectory() as temporary, \
                patch("tools.voice_model_training.vocoder_epochs.train_reviewed_vocoder_epoch", side_effect=self.epoch) as train:
            root = Path(temporary)
            with self.assertRaisesRegex(RuntimeError, "budget"):
                run_reviewed_vocoder_epochs(None, [], None, None, output=root / "run",
                    **self.options(maximum_total_checkpoint_bytes=100))
            self.assertEqual(train.call_count, 1)
            self.assertTrue((root / "run" / "epoch-000001" / "checkpoint.json").exists())
            self.assertFalse((root / "run" / "run.json").exists())

    def test_cooperative_cancellation_deadline_and_epoch_callback(self):
        for kind in ("cancelled", "deadline", "epoch_callback"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                stop, clock = [False], [0.0]
                def epoch(*args, **kwargs):
                    self.assertFalse(kwargs["cancelled"]())
                    self.assertLessEqual(kwargs["maximum_seconds"], 2)
                    receipt = self.epoch(*args, **kwargs)
                    stop[0], clock[0] = True, 3.0 if kind == "deadline" else 0.0
                    self.assertTrue(kwargs["cancelled"]())
                    return receipt
                options = self.options(maximum_run_seconds=2, epochs=1 if kind == "deadline" else 2)
                if kind == "epoch_callback":
                    options["epoch_options"]["cancelled"] = lambda: stop[0]
                else:
                    options["cancelled"] = lambda: stop[0] if kind == "cancelled" else False
                with patch("tools.voice_model_training.vocoder_epochs.time.monotonic", lambda: clock[0]), \
                        patch("tools.voice_model_training.vocoder_epochs.train_reviewed_vocoder_epoch", epoch):
                    with self.assertRaises(RuntimeError):
                        run_reviewed_vocoder_epochs(None, [], None, None, output=root / "run", **options)
                self.assertTrue((root / "run" / "epoch-000001" / "checkpoint.json").exists())
                self.assertFalse((root / "run" / "run.json").exists())

    def test_receipt_mismatch_never_publishes_run(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            def changed(*args, **kwargs):
                receipt = self.epoch(*args, **kwargs)
                receipt["checkpointBytes"] = 99
                return receipt
            with patch("tools.voice_model_training.vocoder_epochs.train_reviewed_vocoder_epoch", changed):
                with self.assertRaises(ValueError):
                    run_reviewed_vocoder_epochs(None, [], None, None, output=root / "run", **self.options())
            self.assertFalse((root / "run" / "run.json").exists())

    def test_epoch_specific_bounds_are_preserved_and_capped_by_remaining_time(self):
        with tempfile.TemporaryDirectory() as temporary:
            root, clock, calls = Path(temporary), [10.0], []
            def epoch(*args, **kwargs):
                calls.append(kwargs)
                clock[0] += 1.0
                return self.epoch(*args, **kwargs)
            with patch("tools.voice_model_training.vocoder_epochs.time.monotonic", lambda: clock[0]), \
                    patch("tools.voice_model_training.vocoder_epochs.train_reviewed_vocoder_epoch", epoch):
                run_reviewed_vocoder_epochs(None, [], None, None, output=root / "run",
                    **self.options(maximum_run_seconds=2.5,
                        epoch_options=dict(maximum_seconds=2, maximum_checkpoint_total_bytes=125,
                                           maximum_checkpoint_file_bytes=75)))
            self.assertEqual([c["maximum_seconds"] for c in calls], [2, 1.5])
            self.assertEqual([c["maximum_checkpoint_total_bytes"] for c in calls], [125, 100])
            self.assertEqual([c["maximum_checkpoint_file_bytes"] for c in calls], [75, 75])

    def test_published_receipt_must_match_lineage_dataset_and_budget(self):
        for invalid in ("lineage", "dataset", "budget", "complete"):
            with self.subTest(invalid=invalid), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                def epoch(*args, **kwargs):
                    kwargs["output"].mkdir()
                    metadata = dict(datasetSha256="b" * 64, run=kwargs["run_metadata"])
                    result = dict(epochComplete=True, coverageVerified=True, updates=1,
                                  meanGeneratorLoss=.5, meanDiscriminatorLoss=.25)
                    if invalid == "lineage":
                        metadata["run"] = dict(metadata["run"], completedEpochs=99)
                    if invalid == "dataset":
                        metadata["datasetSha256"] = "c" * 64
                    if invalid == "complete":
                        result["epochComplete"] = False
                    receipt = dict(checkpointBytes=201 if invalid == "budget" else 100,
                                   checkpointSha256="a" * 64, metadata=metadata, epoch=result)
                    publish_new(kwargs["output"] / "checkpoint.json", receipt)
                    return receipt
                with patch("tools.voice_model_training.vocoder_epochs.train_reviewed_vocoder_epoch", epoch):
                    with self.assertRaisesRegex(ValueError, "receipt differs"):
                        run_reviewed_vocoder_epochs(None, [], None, None, output=root / "run",
                            **self.options(epoch_options=dict(expected_dataset_sha256="b" * 64)))
                self.assertFalse((root / "run" / "run.json").exists())

    def test_invalid_options_and_existing_output_rejected_before_epoch(self):
        changes = (dict(epochs=0), dict(epochs=True), dict(epochs=1001), dict(completed_epochs=-1),
                   dict(retain_checkpoints=0), dict(retain_checkpoints=True), dict(retain_checkpoints=1001),
                   dict(on_progress=False), dict(epoch_options=dict(on_progress=lambda event: None)),
                   dict(completed_epochs=1), dict(parent_receipt_sha256="a" * 64),
                   dict(completed_epochs=1, parent_receipt_sha256="X" * 64),
                   dict(maximum_run_seconds=float("nan")), dict(maximum_run_seconds=True),
                   dict(maximum_total_checkpoint_bytes=0), dict(maximum_total_checkpoint_bytes=True),
                   dict(cancelled=False), dict(metadata=[]), dict(epoch_options=[]),
                   dict(epoch_options=dict(output="forbidden")),
                   dict(epoch_options=dict(reconstruction_directory="forbidden")),
                   dict(epoch_options=dict(maximum_seconds=0)), dict(epoch_options=dict(cancelled=False)),
                   dict(epoch_options=dict(maximum_checkpoint_file_bytes=0)),
                   dict(epoch_options=dict(maximum_checkpoint_total_bytes=0)))
        with tempfile.TemporaryDirectory() as temporary, \
                patch("tools.voice_model_training.vocoder_epochs.train_reviewed_vocoder_epoch") as train:
            root = Path(temporary)
            for change in changes:
                with self.subTest(change=change), self.assertRaises(ValueError):
                    run_reviewed_vocoder_epochs(None, [], None, None, output=root / "invalid", **self.options(**change))
                self.assertFalse((root / "invalid").exists())
            with self.assertRaises(ValueError):
                run_reviewed_vocoder_epochs(None, [], None, None, output=root, **self.options())
            with self.assertRaises(RuntimeError):
                run_reviewed_vocoder_epochs(None, [], None, None, output=root / "cancelled",
                                           **self.options(cancelled=lambda: True))
            self.assertFalse((root / "cancelled").exists())
            train.assert_not_called()

    def binary_epoch(self, *args, **kwargs):
        destination = kwargs["output"]
        destination.mkdir()
        records = []
        for name in ("models.pt", "training.pt"):
            payload = b"x" * 50
            (destination / name).write_bytes(payload)
            records.append(dict(path=name, bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest()))
        receipt = dict(formatId="com.project-seam.gan-checkpoint", files=records,
            checkpointBytes=100, checkpointSha256="a" * 64,
            metadata=dict(datasetSha256="b" * 64, run=kwargs["run_metadata"]),
            epoch=dict(epochComplete=True, coverageVerified=True, updates=1,
                       meanGeneratorLoss=.5, meanDiscriminatorLoss=.25))
        publish_new(destination / "checkpoint.json", receipt)
        return receipt

    def test_rolling_retention_preserves_receipts_and_bounds_peak_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "run"
            peak = []
            def epoch(*args, **kwargs):
                receipt = self.binary_epoch(*args, **kwargs)
                peak.append(sum(path.stat().st_size for path in root.glob("*/*.pt")))
                return receipt
            with patch("tools.voice_model_training.vocoder_epochs.train_reviewed_vocoder_epoch", epoch):
                result = run_reviewed_vocoder_epochs(None, [], None, None, output=root,
                    **self.options(epochs=5, retain_checkpoints=1))
            self.assertEqual(peak, [100, 200, 200, 200, 200])
            self.assertEqual(result["schemaVersion"], 2)
            self.assertEqual(result["checkpointBytes"], 100)
            self.assertEqual(result["writtenCheckpointBytes"], 500)
            self.assertEqual([c["binariesRetained"] for c in result["checkpoints"]], [False]*4 + [True])
            for entry in result["checkpoints"]:
                directory = root / entry["path"]
                self.assertEqual(hashlib.sha256((directory / "checkpoint.json").read_bytes()).hexdigest(),
                                 entry["receiptSha256"])
                self.assertEqual((directory / "models.pt").exists(), entry["binariesRetained"])
                self.assertEqual((directory / "pruned-binaries.json").exists(), not entry["binariesRetained"])

    def test_failed_or_corrupt_successor_never_prunes_last_checkpoint(self):
        for corruption in ("failure", "bytes", "symlink"):
            with self.subTest(corruption=corruption), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary) / "run"
                def epoch(*args, **kwargs):
                    second = kwargs["run_metadata"]["completedEpochs"] == 2
                    if second and corruption == "failure":
                        raise RuntimeError("incomplete successor")
                    receipt = self.binary_epoch(*args, **kwargs)
                    if second:
                        path = kwargs["output"] / "training.pt"
                        if corruption == "bytes":
                            path.write_bytes(b"y" * 50)
                        else:
                            path.unlink()
                            path.symlink_to(root / "epoch-000001" / "training.pt")
                    return receipt
                with patch("tools.voice_model_training.vocoder_epochs.train_reviewed_vocoder_epoch", epoch):
                    with self.assertRaises((RuntimeError, ValueError)):
                        run_reviewed_vocoder_epochs(None, [], None, None, output=root,
                            **self.options(retain_checkpoints=1))
                self.assertTrue((root / "epoch-000001" / "models.pt").is_file())
                self.assertEqual((root / "epoch-000001" / "training.pt").read_bytes(), b"x" * 50)
                self.assertFalse((root / "run.json").exists())

    @unittest.skipUnless(importlib.util.find_spec("torch"), "Optional Torch environment required")
    def test_real_small_gan_checkpoint_roundtrip_and_cumulative_budget(self):
        import torch
        from tools.voice_model_training.vocoder_checkpoint import publish_vocoder_checkpoint, restore_vocoder_checkpoint
        generator, discriminator = torch.nn.Linear(1, 1), torch.nn.Linear(1, 1)
        go, do = torch.optim.AdamW(generator.parameters()), torch.optim.AdamW(discriminator.parameters())
        def epoch(g, ds, gopt, dopt, **kwargs):
            # Only reviewed admission/training is stubbed; persistence is real Torch.
            return publish_vocoder_checkpoint(g, ds, gopt, dopt, kwargs["output"],
                metadata=dict(datasetSha256="b" * 64, run=kwargs["run_metadata"]),
                epoch=dict(epochComplete=True, coverageVerified=True, updates=1,
                           meanGeneratorLoss=.5, meanDiscriminatorLoss=.25),
                maximum_bytes=kwargs["maximum_checkpoint_file_bytes"],
                maximum_total_bytes=kwargs["maximum_checkpoint_total_bytes"])
        with tempfile.TemporaryDirectory() as temporary, \
                patch("tools.voice_model_training.vocoder_epochs.train_reviewed_vocoder_epoch", epoch):
            root = Path(temporary)
            result = run_reviewed_vocoder_epochs(generator, [discriminator], go, do, output=root / "run",
                **self.options(maximum_total_checkpoint_bytes=1024 * 1024))
            first = root / "run" / "epoch-000001"
            first_size = sum(path.stat().st_size for path in first.glob("*.pt"))
            self.assertEqual(result["checkpointBytes"], 2 * first_size)
            restore_vocoder_checkpoint(generator, [discriminator], go, do, first,
                receipt_sha256=hashlib.sha256((first / "checkpoint.json").read_bytes()).hexdigest(),
                expected_metadata=dict(datasetSha256="b" * 64, run=self.options()["metadata"] |
                                       dict(completedEpochs=1, parentReceiptSha256=None)))
            with self.assertRaisesRegex(ValueError, "aggregate.*bound"):
                run_reviewed_vocoder_epochs(generator, [discriminator], go, do, output=root / "bounded",
                    **self.options(maximum_total_checkpoint_bytes=2 * first_size - 1))
            bounded = root / "bounded"
            self.assertTrue((bounded / "epoch-000001" / "checkpoint.json").exists())
            self.assertFalse((bounded / "epoch-000002" / "checkpoint.json").exists())
            self.assertFalse((bounded / "run.json").exists())
            self.assertLessEqual(sum(path.stat().st_size for path in bounded.glob("*/*.pt")), 2 * first_size - 1)
            rolling = run_reviewed_vocoder_epochs(generator, [discriminator], go, do, output=root / "rolling",
                **self.options(epochs=4, retain_checkpoints=1, maximum_total_checkpoint_bytes=2 * first_size))
            final = root / "rolling" / "epoch-000004"
            restore_vocoder_checkpoint(generator, [discriminator], go, do, final,
                receipt_sha256=rolling["receiptSha256"],
                expected_metadata=dict(datasetSha256="b" * 64, run=self.options()["metadata"] |
                    dict(completedEpochs=4, parentReceiptSha256=rolling["checkpoints"][-2]["receiptSha256"])))
            self.assertEqual(len(list((root / "rolling").glob("*/*.pt"))), 2)


if __name__ == "__main__":
    unittest.main()
