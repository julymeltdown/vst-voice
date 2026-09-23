"""Real optimizer/checkpoint loop with fixture admission and deterministic batches."""
import hashlib
import importlib.util
from pathlib import Path
import random
import tempfile
import unittest
from unittest.mock import patch

from tools.voice_model_training.vocoder_training_run import train_reviewed_vocoder_epoch


@unittest.skipUnless(importlib.util.find_spec("torch") and importlib.util.find_spec("numpy"), "Torch/NumPy required")
class LoopRecoveryTests(unittest.TestCase):
    def test_noise_excitation_partial_resume_preserves_full_epoch_identity(self):
        import numpy as np
        import torch

        class Generator(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.gain = torch.nn.Parameter(torch.tensor(.2))
            def forward(self, mel, f0, noise):
                return (self.gain * mel[:, :1].repeat_interleave(2, dim=2)
                        + noise.mean(dim=2, keepdim=True) * .01)

        class Discriminator(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.conv = torch.nn.Conv1d(1, 2, 3, padding=1)
            def forward(self, pcm):
                features = self.conv(pcm)
                return [features.mean(dim=1)], [[features]]

        def setup():
            torch.manual_seed(42)
            random.seed(17)
            np.random.seed(19)
            generator, discriminator = Generator(), Discriminator()
            go = torch.optim.AdamW(generator.parameters(), lr=.01)
            do = torch.optim.AdamW(discriminator.parameters(), lr=.02)
            return generator, [discriminator], go, do

        def loss(a, b):
            return (a-b).abs().mean() * (random.random() + float(np.random.random()) + torch.rand(()))

        snapshot = dict(schemaVersion=3, preparationIssues=[], sourcePermissionsAdmitted=True,
            labelsAdmitted=True, expiresAt=200, datasetSha256="a"*64,
            sources=[dict(sourceId="s", frameCount=96)],
            conditioning=[dict(sourceId="s", frameCount=48)],
            bindings=dict(split=dict(groups=[dict(partition="train", sourceIds=["s"])])))
        admission = dict.fromkeys(("permission_config", "permission_hash", "label_config", "label_hash", "root",
            "rights_review", "rights_policy", "rights_anchor", "label_review", "label_policy", "label_anchor",
            "seed", "held_out_songs"))
        batches = [dict(sourceId="s", partition="train", datasetSha256="a"*64, profileSha256="b"*64,
            frameOffset=i*16, mel=torch.full((1, 2, 16), 1.+i), f0=torch.full((1, 16), 220.),
            pcm=torch.zeros(1, 1, 32), unvoicedFrames=torch.ones(1, 16, dtype=torch.bool),
            hopSize=2, validSamples=32) for i in range(3)]
        prefix = "tools.voice_model_training.vocoder_training_run."
        with tempfile.TemporaryDirectory() as directory, patch(prefix+"time.time", return_value=100), \
                patch(prefix+"assemble_dataset", return_value=snapshot), \
                patch(prefix+"require_disk_headroom"), \
                patch(prefix+"iter_vocoder_batches", side_effect=lambda *a, **k: iter(batches)):
            root = Path(directory)
            options = dict(dataset_inputs=admission, conditioning_directory=root,
                targets={"s": (dict(profile=dict(hopSize=2)), None)}, pcm_sources={},
                expected_profile_sha256="b"*64, run_metadata=dict(fixture=True),
                reconstruction_loss=loss, objective_id="noise-fixture", maximum_updates=3,
                training_segment_frames=16, maximum_checkpoint_total_bytes=1024**2,
                excitation_noise_id="uv-gated-v1")
            baseline = setup()
            expected = train_reviewed_vocoder_epoch(*baseline, output=root/"baseline", **options)
            interrupted = setup()
            def stop(event):
                if event["stage"] == "partial-checkpoint" and event["completedUpdates"] == 2:
                    raise RuntimeError("fixture interruption after noise cursor")
            with self.assertRaisesRegex(RuntimeError, "fixture interruption"):
                train_reviewed_vocoder_epoch(*interrupted, output=root/"interrupted",
                    recovery_directory=root/"recovery", checkpoint_interval_updates=1,
                    maximum_recovery_bytes=1024**2, on_progress=stop, **options)
            saved = root/"recovery/update-000002"
            digest = hashlib.sha256((saved/"checkpoint.json").read_bytes()).hexdigest()
            resumed = setup()
            from tools.voice_model_training.vocoder_optimization import vocoder_gan_step
            with patch(prefix+"vocoder_gan_step", wraps=vocoder_gan_step) as steps:
                result = train_reviewed_vocoder_epoch(*resumed, output=root/"resumed",
                    resume_partial=saved, resume_partial_sha256=digest, **options)
            self.assertEqual(steps.call_count, 1)
            for field in ("excitationNoiseId", "excitationDigestAlgorithm",
                          "excitationRawDrawSha256", "excitationRealizedSha256"):
                self.assertEqual(result["epoch"][field], expected["epoch"][field], field)
            self.assertEqual(result["epoch"], expected["epoch"])
            for original, restored in ((baseline[0], resumed[0]), (baseline[1][0], resumed[1][0])):
                for key, value in original.state_dict().items():
                    self.assertTrue(torch.equal(value, restored.state_dict()[key]), key)
            wrong = setup()
            with patch(prefix+"vocoder_gan_step") as steps, self.assertRaisesRegex(ValueError, "excitation identity"):
                train_reviewed_vocoder_epoch(*wrong, output=root/"wrong",
                    resume_partial=saved, resume_partial_sha256=digest,
                    **dict(options, excitation_noise_id="zero-v1"))
            steps.assert_not_called()

    def test_interrupt_restore_matches_uninterrupted_segmented_epoch(self):
        import torch
        import numpy as np
        from tools.voice_model_training.test_vocoder_optimization import VocoderOptimizationTests
        from tools.voice_model_training.vocoder_optimization import vocoder_gan_step
        def setup():
            g, d, _, _, _ = VocoderOptimizationTests().fixture()
            random.seed(17); np.random.seed(19)
            go, do = torch.optim.AdamW(g.parameters(), lr=.01), torch.optim.AdamW(d.parameters(), lr=.02)
            schedules = dict(generator=torch.optim.lr_scheduler.StepLR(go, 1, .9),
                             discriminator=torch.optim.lr_scheduler.StepLR(do, 1, .8))
            return g, [d], go, do, schedules
        def loss(a, b):
            return (a-b).abs().mean() * (random.random() + float(np.random.random()) + torch.rand(()))
        snapshot = dict(schemaVersion=3, preparationIssues=[], sourcePermissionsAdmitted=True,
            labelsAdmitted=True, expiresAt=200, datasetSha256="a"*64,
            sources=[dict(sourceId="s", frameCount=96)], conditioning=[dict(sourceId="s", frameCount=48)],
            bindings=dict(split=dict(groups=[dict(partition="train", sourceIds=["s"])])))
        admission = dict.fromkeys(("permission_config", "permission_hash", "label_config", "label_hash", "root",
            "rights_review", "rights_policy", "rights_anchor", "label_review", "label_policy", "label_anchor",
            "seed", "held_out_songs"))
        batches = [dict(sourceId="s", partition="train", datasetSha256="a"*64, profileSha256="b"*64,
            frameOffset=i*16, mel=torch.full((1, 2, 16), 1.+i), f0=torch.full((1, 16), 220.),
            pcm=torch.zeros(1, 1, 32), hopSize=2, validSamples=32) for i in range(3)]
        prefix = "tools.voice_model_training.vocoder_training_run."
        with tempfile.TemporaryDirectory() as directory, patch(prefix+"time.time", return_value=100), \
                patch(prefix+"assemble_dataset", return_value=snapshot), \
                patch(prefix+"require_disk_headroom"), \
                patch(prefix+"iter_vocoder_batches", side_effect=lambda *a, **k: iter(batches)) as reader:
            root = Path(directory)
            options = dict(dataset_inputs=admission, conditioning_directory=root,
                targets={"s": (dict(profile=dict(hopSize=2)), None)}, pcm_sources={},
                expected_profile_sha256="b"*64, run_metadata=dict(fixture=True),
                reconstruction_loss=loss, objective_id="stochastic-fixture", maximum_updates=3,
                training_segment_frames=16, maximum_checkpoint_total_bytes=1024**2)
            original = setup()
            expected = train_reviewed_vocoder_epoch(*original[:4], schedulers=original[4],
                                                   output=root/"baseline", **options)
            expected_rng = (random.random(), float(np.random.random()), torch.rand(3))
            interrupted = setup()
            def stop(event):
                if event["stage"] == "partial-checkpoint":
                    raise RuntimeError("fixture interruption after durable partial")
            with self.assertRaisesRegex(RuntimeError, "fixture interruption"):
                train_reviewed_vocoder_epoch(*interrupted[:4], schedulers=interrupted[4],
                    output=root/"interrupted", recovery_directory=root/"recovery",
                    checkpoint_interval_updates=1, maximum_recovery_bytes=1024**2, on_progress=stop, **options)
            saved = root/"recovery/update-000001"
            digest = hashlib.sha256((saved/"checkpoint.json").read_bytes()).hexdigest()
            self.assertFalse((root/"interrupted/checkpoint.json").exists())
            resumed = setup()
            events = []
            with patch(prefix+"vocoder_gan_step", wraps=vocoder_gan_step) as actual_steps:
                result = train_reviewed_vocoder_epoch(*resumed[:4], schedulers=resumed[4],
                    output=root/"resumed", resume_partial=saved, resume_partial_sha256=digest,
                    recovery_directory=root/"continued-recovery", checkpoint_interval_updates=1,
                    maximum_recovery_bytes=1024**2, on_progress=events.append, **options)
            self.assertEqual(actual_steps.call_count, 2)
            self.assertTrue((root/"continued-recovery/update-000002/checkpoint.json").is_file())
            self.assertFalse((root/"continued-recovery/update-000001").exists())
            self.assertEqual(result["epoch"], expected["epoch"])
            self.assertEqual(result["epoch"]["updates"], 3)
            self.assertEqual(result["epoch"]["validSamples"], 96)
            self.assertEqual(random.random(), expected_rng[0])
            self.assertEqual(float(np.random.random()), expected_rng[1])
            self.assertTrue(torch.equal(torch.rand(3), expected_rng[2]))
            for old, new in ((original[0], resumed[0]), (original[1][0], resumed[1][0])):
                for key, tensor in old.state_dict().items():
                    self.assertTrue(torch.equal(tensor, new.state_dict()[key]), key)
            for key in original[4]:
                self.assertEqual(original[4][key].state_dict(), resumed[4][key].state_dict())
            self.assertEqual([e["completedUpdates"] for e in events if e["stage"]=="partial-restored"], [1])
            retained_model = setup()
            retained_events = []
            retained_result = train_reviewed_vocoder_epoch(*retained_model[:4], schedulers=retained_model[4],
                output=root/"retained-complete", recovery_directory=root/"retained-recovery",
                checkpoint_interval_updates=1, retain_partial_checkpoints=1,
                maximum_recovery_bytes=1024**2, on_progress=retained_events.append, **options)
            self.assertEqual(retained_result["epoch"], expected["epoch"])
            self.assertTrue((root/"retained-recovery/update-000001/pruned-binaries.json").is_file())
            self.assertFalse((root/"retained-recovery/update-000001/models.pt").exists())
            self.assertFalse((root/"retained-recovery/update-000002/models.pt").exists())
            self.assertTrue((root/"retained-recovery/update-000002/pruned-binaries.json").is_file())
            completed_retention = [e for e in retained_events if e["stage"] == "partial-retention-completed"]
            self.assertEqual(completed_retention[0]["recoveryBytes"], 0)
            self.assertTrue((saved/"models.pt").is_file())  # External resume remains intact.
            saves = [e for e in retained_events if e["stage"] == "partial-checkpoint"]
            self.assertGreater(saves[-1]["recoveryWrittenBytes"], saves[-1]["recoveryBytes"])
            # A changed already-completed prefix is still read and rejected, never silently skipped.
            reader.side_effect = lambda *a, **k: iter([batches[0] | dict(validSamples=31), *batches[1:]])
            with patch(prefix+"vocoder_gan_step") as step, self.assertRaises(ValueError):
                bad = setup()
                train_reviewed_vocoder_epoch(*bad[:4], schedulers=bad[4], output=root/"bad",
                    resume_partial=saved, resume_partial_sha256=digest, **options)
            step.assert_not_called()
