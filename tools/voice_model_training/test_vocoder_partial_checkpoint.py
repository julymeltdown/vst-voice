"""Real small Torch-state continuation, not large-model or training-loop recovery."""
import hashlib
import importlib.util
from pathlib import Path
import random
import tempfile
import unittest
from unittest.mock import patch

from tools.voice_model_training.__main__ import encode_report
from tools.voice_model_training.vocoder_checkpoint import (
    publish_vocoder_partial_checkpoint, restore_vocoder_partial_checkpoint, restore_vocoder_checkpoint,
)
from tools.voice_model_training.vocoder_recovery_cursor import build_recovery_plan, partial_cursor


@unittest.skipUnless(importlib.util.find_spec("torch") and importlib.util.find_spec("numpy"), "Torch/NumPy required")
class PartialCheckpointTests(unittest.TestCase):
    def test_partial_state_continuation_and_complete_loader_refusal(self):
        import torch
        import numpy as np
        torch.manual_seed(7)
        random.seed(8)
        np.random.seed(9)
        def setup():
            g, d = torch.nn.Linear(2, 2), torch.nn.Linear(2, 1)
            go, do = torch.optim.AdamW(g.parameters(), lr=.01), torch.optim.AdamW(d.parameters(), lr=.02)
            schedules = dict(generator=torch.optim.lr_scheduler.StepLR(go, 1, .9),
                             discriminator=torch.optim.lr_scheduler.StepLR(do, 1, .8))
            return g, d, go, do, schedules
        def advance(parts):
            g, d, go, do, _ = parts
            go.zero_grad(); do.zero_grad()
            x = torch.randn(3, 2) * (random.random() + float(np.random.random()))
            loss = d(g(x)).square().mean()
            loss.backward()
            go.step(); do.step()
            return float(loss.detach())
        original = setup()
        advance(original)
        g, d, go, do, schedulers = original
        for scheduler in schedulers.values():
            scheduler.step()
        run = dict(configuration="fixture", completedEpochs=2)
        metadata = dict(run=run, datasetSha256="a"*64, profileSha256="b"*64)
        plan = build_recovery_plan([dict(sourceId="s", analysisFrames=64, sourceSamples=16384)],
            dataset_sha256="a"*64, profile_sha256="b"*64,
            run_sha256=hashlib.sha256(encode_report(run)).hexdigest(), segment_frames=16, hop_size=256)
        cursor = partial_cursor(plan, completed_updates=1, generator_loss_sum=1., discriminator_loss_sum=2.)
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "partial"
            receipt = publish_vocoder_partial_checkpoint(g, [d], go, do, output, metadata=metadata,
                recovery_plan=plan, cursor=cursor, schedulers=schedulers)
            self.assertEqual(receipt["formatId"], "com.project-seam.gan-partial-checkpoint")
            self.assertFalse(receipt["epoch"]["epochComplete"])
            bad = Path(temporary) / "false-completion"
            with self.assertRaises(ValueError):
                publish_vocoder_partial_checkpoint(g, [d], go, do, bad, metadata=metadata,
                    recovery_plan=plan, cursor=cursor | dict(epochComplete=True), schedulers=schedulers)
            self.assertFalse(bad.exists())
            bounded = Path(temporary) / "bounded"
            with self.assertRaisesRegex(ValueError, "file exceeds bound"):
                publish_vocoder_partial_checkpoint(g, [d], go, do, bounded, metadata=metadata,
                    recovery_plan=plan, cursor=cursor, schedulers=schedulers, maximum_bytes=1)
            self.assertFalse((bounded / "checkpoint.json").exists())
            digest = hashlib.sha256((output / "checkpoint.json").read_bytes()).hexdigest()
            expected = [advance(original), advance(original)]
            expected_rng = (random.random(), float(np.random.random()), torch.rand(4))
            resumed = setup()
            rg, rd, rgo, rdo, rs = resumed
            options = dict(receipt_sha256=digest, expected_metadata=metadata, schedulers=rs)
            with patch("torch.load", side_effect=AssertionError("partial rejected before decoding")):
                with self.assertRaises(ValueError):
                    restore_vocoder_checkpoint(rg, [rd], rgo, rdo, output, **options)
            restored = restore_vocoder_partial_checkpoint(rg, [rd], rgo, rdo, output,
                                                          recovery_plan=plan, **options)
            self.assertEqual(restored["epoch"], cursor)
            self.assertEqual([advance(resumed), advance(resumed)], expected)
            self.assertEqual(random.random(), expected_rng[0])
            self.assertEqual(float(np.random.random()), expected_rng[1])
            self.assertTrue(torch.equal(torch.rand(4), expected_rng[2]))
            for old, new in ((g, rg), (d, rd)):
                for name, tensor in old.state_dict().items():
                    self.assertTrue(torch.equal(tensor, new.state_dict()[name]))
            for name in schedulers:
                self.assertEqual(schedulers[name].state_dict(), rs[name].state_dict())
            with self.assertRaises(ValueError):
                restore_vocoder_partial_checkpoint(rg, [rd], rgo, rdo, output,
                    recovery_plan=plan, **(options | dict(expected_metadata=metadata | dict(run=dict(configuration="changed")))))
            changed_plan = dict(plan, datasetSha256="e"*64)
            with self.assertRaises(ValueError):
                restore_vocoder_partial_checkpoint(rg, [rd], rgo, rdo, output,
                                                  recovery_plan=changed_plan, **options)
            with (output / "training.pt").open("ab") as stream:
                stream.write(b"corruption")
            with patch("torch.load", side_effect=AssertionError("verify both files first")):
                with self.assertRaises(ValueError):
                    restore_vocoder_partial_checkpoint(rg, [rd], rgo, rdo, output,
                                                      recovery_plan=plan, **options)
