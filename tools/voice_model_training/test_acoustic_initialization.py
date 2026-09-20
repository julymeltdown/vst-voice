import copy
import contextlib
import io
import unittest
from unittest.mock import patch

try:
    import torch
except ImportError:
    torch = None

from tools.voice_model_training.train import initialize_checkpoint, model_settings, main


@unittest.skipIf(torch is None, "Torch is optional outside the training environment")
class InitializationTests(unittest.TestCase):
    def setUp(self):
        settings = dict(formatId="com.project-seam.ddpm-training-config", schemaVersion=1,
                        hiddenSize=32, encoderLayers=1, channels=32, layers=2, timesteps=8,
                        seed=17, learningRate=.001, maximumUpdates=3, maximumSeconds=60, loss="l1")
        self.metadata = dict(settings=settings, configuration=model_settings(settings),
                             trainingConfigurationSha256="a" * 64, vocabulary=["a"],
                             assemblyConfigurationSha256="b" * 64, targetInventorySha256="c" * 64,
                             revision="pinned", torchVersion=str(torch.__version__), numpyVersion="test")
        source = torch.nn.Linear(2, 1)
        optimizer = torch.optim.AdamW(source.parameters(), lr=.001)
        source(torch.ones(1, 2)).sum().backward()
        optimizer.step()
        self.state = dict(model=copy.deepcopy(source.state_dict()), optimizer=optimizer.state_dict(),
                          rng=torch.get_rng_state().clone())
        self.receipt = dict(checkpointSha256="d" * 64,
            metadata=dict(run=dict(self.metadata, completedEpochs=21, parentReceiptSha256="e" * 64),
                          profileSha256="f" * 64, datasetSha256="1" * 64),
            epoch=dict(epochComplete=True, coverageVerified=True, datasetSha256="1" * 64,
                       objectiveId="diffsinger-ddpm-l1-whole-phrase-v2"))

    def initialize(self, *, metadata=None, receipt=None, warm=False, optimizer=None):
        model = torch.nn.Linear(2, 1)
        optimizer = optimizer or torch.optim.AdamW(model.parameters(),
            lr=(metadata or self.metadata)["settings"]["learningRate"])
        result = initialize_checkpoint(model, optimizer, self.state, receipt or self.receipt,
            metadata=metadata or self.metadata, profile="f" * 64, receipt_sha256="2" * 64,
            warm_start=warm)
        return model, optimizer, result

    def changed(self, **updates):
        metadata = copy.deepcopy(self.metadata)
        metadata["settings"].update(updates or dict(loss="l2"))
        metadata["configuration"] = model_settings(metadata["settings"])
        metadata["trainingConfigurationSha256"] = "3" * 64
        return metadata

    def test_resume_restores_optimizer_rng_and_epoch(self):
        model, optimizer, (metadata, dataset, completed) = self.initialize()
        self.assertEqual(completed, 21)
        self.assertEqual(dataset, "1" * 64)
        self.assertEqual(metadata, self.metadata)
        self.assertTrue(optimizer.state)
        self.assertEqual(optimizer.param_groups[0]["lr"], .001)
        self.assertTrue(torch.equal(torch.get_rng_state(), self.state["rng"]))
        for name, value in model.state_dict().items():
            self.assertTrue(torch.equal(value, self.state["model"][name]))

    def test_warm_start_loads_only_weights_and_starts_new_lineage(self):
        before = copy.deepcopy(self.receipt)
        current = self.changed(loss="l2", learningRate=.0001)
        model, optimizer, (metadata, dataset, completed) = self.initialize(metadata=current, warm=True)
        self.assertEqual(completed, 0)
        self.assertEqual(dataset, "1" * 64)
        self.assertFalse(optimizer.state)
        self.assertEqual(optimizer.param_groups[0]["lr"], .0001)
        self.assertTrue(torch.equal(torch.get_rng_state(), torch.Generator().manual_seed(17).get_state()))
        self.assertEqual(metadata["warmStart"]["sourceCompletedEpochs"], 21)
        self.assertEqual(metadata["warmStart"]["sourceReceiptSha256"], "2" * 64)
        self.assertEqual(metadata["warmStart"]["sourceCheckpointSha256"], "d" * 64)
        self.assertEqual(self.receipt, before)
        self.assertNotIn("warmStart", current)
        for name, value in model.state_dict().items():
            self.assertTrue(torch.equal(value, self.state["model"][name]))

    def test_changed_settings_still_fail_exact_resume(self):
        for settings in (dict(loss="l2"), dict(learningRate=.0001)):
            with self.subTest(settings=settings), self.assertRaises(ValueError):
                self.initialize(metadata=self.changed(**settings))

    def test_warm_start_refuses_other_changes(self):
        for settings in (dict(seed=18), dict(channels=64), dict(timesteps=16), dict(maximumUpdates=4)):
            with self.subTest(settings=settings), self.assertRaises(ValueError):
                self.initialize(metadata=self.changed(**settings), warm=True)
        for key in ("vocabulary", "assemblyConfigurationSha256", "targetInventorySha256", "revision", "torchVersion"):
            changed = self.changed()
            changed[key] = ["i"] if key == "vocabulary" else "different"
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.initialize(metadata=changed, warm=True)

    def test_unchanged_settings_allowed_for_reset_optimizer_control_arm(self):
        _, optimizer, (metadata, _, completed) = self.initialize(warm=True)
        self.assertEqual(completed, 0)
        self.assertFalse(optimizer.state)
        self.assertEqual(metadata["settings"], self.metadata["settings"])
        self.assertTrue(metadata["warmStart"]["optimizerReset"])

    def test_invalid_completion_profile_and_existing_optimizer_refused(self):
        for field in ("epochComplete", "coverageVerified"):
            receipt = copy.deepcopy(self.receipt)
            receipt["epoch"][field] = False
            with self.assertRaises(ValueError):
                self.initialize(receipt=receipt, metadata=self.changed(), warm=True)
        receipt = copy.deepcopy(self.receipt)
        receipt["metadata"]["profileSha256"] = "4" * 64
        with self.assertRaises(ValueError):
            self.initialize(receipt=receipt, metadata=self.changed(), warm=True)
        model = torch.nn.Linear(2, 1)
        optimizer = torch.optim.AdamW(model.parameters())
        optimizer.load_state_dict(self.state["optimizer"])
        with self.assertRaises(ValueError):
            self.initialize(metadata=self.changed(), warm=True, optimizer=optimizer)

    def test_exact_resume_preserves_valid_warm_start_origin(self):
        current = self.changed()
        _, _, (metadata, _, _) = self.initialize(metadata=current, warm=True)
        receipt = copy.deepcopy(self.receipt)
        receipt["metadata"]["run"] = dict(metadata, completedEpochs=2, parentReceiptSha256="5" * 64)
        receipt["epoch"]["objectiveId"] = "diffsinger-ddpm-l2-whole-phrase-v2"
        _, _, (resumed, _, completed) = self.initialize(metadata=current, receipt=receipt)
        self.assertEqual(completed, 2)
        self.assertEqual(resumed["warmStart"], metadata["warmStart"])
        receipt["metadata"]["run"]["warmStart"]["optimizerReset"] = False
        with self.assertRaises(ValueError):
            self.initialize(metadata=current, receipt=receipt)

    def test_cli_requires_paired_and_exclusive_initialization_modes(self):
        argv = ["train"]
        for name in ("training-config", "dataset-config", "targets", "source-root", "conditioning",
                     "trusted-checkout", "output", "training-sha256", "dataset-sha256", "targets-sha256",
                     "rights-policy-sha256", "label-policy-sha256"):
            argv.extend(["--" + name, "unused"])
        for extra in (["--warm-start", "unused"], ["--warm-start-receipt-sha256", "a" * 64],
                      ["--resume", "unused"], ["--resume-receipt-sha256", "a" * 64]):
            with patch("sys.argv", argv + extra), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(main(), 2)
        with patch("sys.argv", argv + ["--resume", "unused", "--warm-start", "unused"]), \
                contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
            main()
        self.assertEqual(error.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
