import hashlib
import importlib.util
from pathlib import Path
import random
import tempfile
import unittest

from tools.voice_model_training.vocoder_checkpoint import publish_vocoder_checkpoint, restore_vocoder_checkpoint


@unittest.skipUnless(importlib.util.find_spec("torch"), "Optional Torch environment required")
class VocoderCheckpointTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
