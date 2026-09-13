import hashlib
import errno
import importlib.util
from pathlib import Path
import random
import tempfile
import unittest
from unittest.mock import patch

from tools.voice_model_training.vocoder_checkpoint import publish_vocoder_checkpoint, restore_vocoder_checkpoint


@unittest.skipUnless(importlib.util.find_spec("torch"), "Optional Torch environment required")
class VocoderCheckpointTests(unittest.TestCase):
    def test_serialization_finalizer_preserves_write_failure(self):
        import torch
        from tools.voice_model_training.gan_checkpoint_storage import publish_checkpoint

        class BrokenStream:
            def __enter__(self):
                return self
            def __exit__(self, *args):
                return False
            def write(self, data):
                raise OSError(errno.ENOSPC, "Fixture disk full")

        def masked_save(state, writer):
            try:
                writer.write(b"fixture")
            finally:
                raise RuntimeError("Fixture ZIP finalizer unexpected pos")

        model = torch.nn.Linear(1, 1)
        optimizer = torch.optim.AdamW(model.parameters())
        with tempfile.TemporaryDirectory() as root:
            output = Path(root) / "failed"
            with patch.object(Path, "open", return_value=BrokenStream()), patch("torch.save", masked_save):
                with self.assertRaises(OSError) as raised:
                    publish_checkpoint(model, optimizer, output, metadata={},
                                       epoch=dict(epochComplete=True, coverageVerified=True))
            self.assertEqual(raised.exception.errno, errno.ENOSPC)
            self.assertIsInstance(raised.exception.__cause__, RuntimeError)
            self.assertFalse((output / "checkpoint.json").exists())

    def test_real_serializer_preserves_file_bound(self):
        import torch
        from tools.voice_model_training.gan_checkpoint_storage import publish_checkpoint
        model = torch.nn.Linear(1, 1)
        optimizer = torch.optim.AdamW(model.parameters())
        with tempfile.TemporaryDirectory() as root:
            output = Path(root) / "bounded"
            with self.assertRaisesRegex(ValueError, "GAN state file exceeds bound"):
                publish_checkpoint(model, optimizer, output, metadata={}, maximum_bytes=1,
                                   epoch=dict(epochComplete=True, coverageVerified=True))
            self.assertFalse((output / "checkpoint.json").exists())

    def test_complete_gan_continuation_and_rng(self):
        import numpy as np
        import torch
        from tools.voice_model_training.test_vocoder_optimization import VocoderOptimizationTests
        from tools.voice_model_training.vocoder_optimization import vocoder_gan_step
        def setup():
            g, d, _, _, inputs = VocoderOptimizationTests().fixture()
            go, do = torch.optim.AdamW(g.parameters(), lr=.01), torch.optim.AdamW(d.parameters(), lr=.01)
            schedules = dict(generator=torch.optim.lr_scheduler.StepLR(go, 1, .9),
                             discriminator=torch.optim.lr_scheduler.StepLR(do, 1, .8))
            return g, d, go, do, schedules, inputs
        def advance(parts):
            g, d, go, do, schedules, inputs = parts
            result = vocoder_gan_step(g, [d], go, do, **inputs)
            for value in schedules.values():
                value.step()
            return result
        parts = setup()
        advance(parts)
        g, d, go, do, schedules, _ = parts
        metadata = dict(datasetSha256="fixture", profileSha256="fixture", configuration="test-only")
        with tempfile.TemporaryDirectory() as root:
            output = Path(root) / "checkpoint"
            publish_vocoder_checkpoint(g, [d], go, do, output, metadata=metadata,
                epoch=dict(epochComplete=True, coverageVerified=True), schedulers=schedules)
            receipt_hash = hashlib.sha256((output / "checkpoint.json").read_bytes()).hexdigest()
            expected_rng = (random.random(), np.random.random(), torch.rand(3))
            expected = advance(parts)
            resumed = setup()
            rg, rd, rgo, rdo, rs, _ = resumed
            restore_vocoder_checkpoint(rg, [rd], rgo, rdo, output, receipt_sha256=receipt_hash,
                                      expected_metadata=metadata, schedulers=rs)
            self.assertEqual(random.random(), expected_rng[0])
            self.assertEqual(np.random.random(), expected_rng[1])
            self.assertTrue(torch.equal(torch.rand(3), expected_rng[2]))
            self.assertEqual(advance(resumed), expected)
            for first, second in ((g, rg), (d, rd)):
                for key, value in first.state_dict().items():
                    self.assertTrue(torch.equal(value, second.state_dict()[key]), key)
            self.assertEqual(schedules["generator"].state_dict(), rs["generator"].state_dict())
            self.assertEqual(schedules["discriminator"].state_dict(), rs["discriminator"].state_dict())
            with self.assertRaises(ValueError):
                restore_vocoder_checkpoint(rg, [rd], rgo, rdo, output, receipt_sha256=receipt_hash,
                                          expected_metadata=dict(metadata, configuration="changed"), schedulers=rs)
            with self.assertRaises(ValueError):
                restore_vocoder_checkpoint(rg, [rd], rgo, rdo, output, receipt_sha256=receipt_hash,
                                          expected_metadata=metadata)
            wrong_go = torch.optim.SGD(rg.parameters(), lr=.01)
            wrong_schedulers = dict(rs, generator=torch.optim.lr_scheduler.StepLR(wrong_go, 1, .9))
            with self.assertRaises(ValueError):
                restore_vocoder_checkpoint(rg, [rd], wrong_go, rdo, output, receipt_sha256=receipt_hash,
                                          expected_metadata=metadata, schedulers=wrong_schedulers)
            self.assertTrue((output / "models.pt").is_file())
            self.assertTrue((output / "training.pt").is_file())
            # A damaged second file is rejected before either payload is decoded.
            with (output / "training.pt").open("ab") as stream:
                stream.write(b"changed")
            with patch("torch.load", side_effect=AssertionError("must validate both files first")):
                with self.assertRaises(ValueError):
                    restore_vocoder_checkpoint(rg, [rd], rgo, rdo, output, receipt_sha256=receipt_hash,
                                              expected_metadata=metadata, schedulers=rs)
            failed = Path(root) / "failed"
            def expired():
                raise ValueError("Fixture source authority expired before publication")
            with self.assertRaises(ValueError):
                publish_vocoder_checkpoint(g, [d], go, do, failed, metadata=metadata,
                    epoch=dict(epochComplete=True, coverageVerified=True), before_publish=expired)
            self.assertFalse((failed / "checkpoint.json").exists())


if __name__ == "__main__":
    unittest.main()
