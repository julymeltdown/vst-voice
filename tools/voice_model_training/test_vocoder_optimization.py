import importlib.util
import unittest

from tools.voice_model_training.vocoder_optimization import vocoder_gan_step


@unittest.skipUnless(importlib.util.find_spec("torch"), "Optional Torch environment required")
class VocoderOptimizationTests(unittest.TestCase):
    def fixture(self):
        import torch
        class Generator(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.gain = torch.nn.Parameter(torch.tensor(.2))
            def forward(self, mel, f0):
                return self.gain * mel[:, :1].repeat_interleave(2, dim=2)
        class Discriminator(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.conv = torch.nn.Conv1d(1, 2, 3, padding=1)
            def forward(self, pcm):
                features = self.conv(pcm)
                return [features.mean(dim=1)], [[features]]
        torch.manual_seed(42)
        g, d = Generator(), Discriminator()
        go, do = torch.optim.SGD(g.parameters(), lr=.01), torch.optim.SGD(d.parameters(), lr=.01)
        inputs = dict(mel=torch.ones(1, 2, 4), f0=torch.ones(1, 4) * 220,
                      pcm=torch.zeros(1, 1, 8), hop_size=2, partition="train",
                      reconstruction_loss=lambda a, b: (a - b).abs().mean())
        return g, d, go, do, inputs

    def test_real_alternating_updates_and_restored_modes(self):
        import torch
        g, d, go, do, inputs = self.fixture()
        g.eval()
        d.train()
        d.conv.eval()
        before_g = g.gain.detach().clone()
        before_d = d.conv.weight.detach().clone()
        report = vocoder_gan_step(g, [d], go, do, **inputs)
        self.assertNotIn('periodicityLoss', report)
        self.assertTrue(report["gradientOwnershipVerified"])
        self.assertFalse(report["trainingAdmitted"])
        self.assertFalse(torch.equal(before_g, g.gain))
        self.assertFalse(torch.equal(before_d, d.conv.weight))
        self.assertTrue(all(p.grad is None and p.requires_grad for p in d.parameters()))
        self.assertFalse(g.training)
        self.assertTrue(d.training)
        self.assertFalse(d.conv.training)

    def test_reject_invalid_data_before_update(self):
        import torch
        for patch in (dict(partition="validation"), dict(pcm=torch.zeros(1, 1, 7)),
                      dict(f0=torch.full((1, 4), float("nan"))), dict(hop_size=True)):
            g, d, go, do, inputs = self.fixture()
            before = g.gain.detach().clone()
            with self.assertRaises(ValueError):
                vocoder_gan_step(g, [d], go, do, **dict(inputs, **patch))
            self.assertTrue(torch.equal(before, g.gain))

    def test_periodicity_opt_in_and_invalid_mask_before_updates(self):
        import torch
        g,d,go,do,inputs=self.fixture()
        before=d.conv.weight.detach().clone()
        with self.assertRaises(ValueError):
            vocoder_gan_step(g,[d],go,do,**inputs,periodicity_mask=torch.zeros(1,1,8,dtype=torch.bool))
        self.assertTrue(torch.equal(before,d.conv.weight))
        generator=torch.Generator().manual_seed(4)
        inputs.update(mel=torch.sin(torch.arange(2048)*2*torch.pi/128).reshape(1,1,-1).repeat(1,2,1),
                      f0=torch.zeros(1,2048),pcm=torch.randn(1,1,4096,generator=generator)*.02)
        result=vocoder_gan_step(g,[d],go,do,**inputs,periodicity_mask=torch.ones(1,1,4096,dtype=torch.bool))
        self.assertGreater(result['periodicityLoss'],0)
        self.assertEqual(result['periodicityCoverage']['selectedWindows'],13)
        self.assertTrue(result['gradientOwnershipVerified'])

    def test_failed_generator_phase_restores_flags_but_requires_discard(self):
        import torch
        g, d, go, do, inputs = self.fixture()
        d.eval()
        inputs["reconstruction_loss"] = lambda a, b: a.sum() * float("nan")
        with self.assertRaises(ValueError):
            vocoder_gan_step(g, [d], go, do, **inputs)
        self.assertFalse(d.training)
        self.assertTrue(all(p.requires_grad for p in d.parameters()))


if __name__ == "__main__":
    unittest.main()
