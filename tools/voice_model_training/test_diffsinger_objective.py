import importlib.util
from types import SimpleNamespace
import unittest

from tools.voice_model_training.diffsinger_objective import DiffSingerDDPMObjective
from tools.voice_model_training.optimization import acoustic_training_step


@unittest.skipUnless(importlib.util.find_spec("torch"), "Optional Torch environment not installed")
class DiffSingerObjectiveTests(unittest.TestCase):
    def test_training_call_layout_and_rejected_chunk(self):
        import numpy as np
        import torch
        class Fixture(torch.nn.Module):
            diffusion_type = "ddpm"
            use_shallow_diffusion = False
            def __init__(self):
                super().__init__()
                self.bias = torch.nn.Parameter(torch.zeros(1, 1, 2, 3))
                self.fs2 = SimpleNamespace(**{flag: False for flag in (
                    "use_lang_id", "use_energy_embed", "use_breathiness_embed", "use_voicing_embed",
                    "use_tension_embed", "use_key_shift_embed", "use_speed_embed", "use_spk_id")})
                self.calls = 0
            def forward(self, tokens, *, mel2ph, f0, gt_mel, infer):
                self.calls += 1
                assert not infer and tokens.tolist() == [[1, 1]] and mel2ph.tolist() == [[1, 1, 2]]
                assert tuple(gt_mel.shape) == (1, 3, 2)
                return SimpleNamespace(aux_out=None, diff_out=(self.bias, torch.ones_like(self.bias)))
        model = Fixture()
        optimizer = torch.optim.SGD(model.parameters(), lr=.1)
        batch = dict(sourceId="interface-fixture", partition="train", hopSize=256,
            frameOffset=0, phraseAnalysisFrames=3, tokens=[1, 1], mel2ph=[1, 1, 2],
            columns=dict(phoneId=[1]*3, f0Hz=[220]*3, voiced=[True]*3, midi=[57]*3,
                         rest=[False]*3, slur=[False]*3, validSamples=[256]*3),
            melTargets=np.zeros((3, 2), dtype=np.float32))
        objective = DiffSingerDDPMObjective("l2")
        def step():
            return acoustic_training_step(model, optimizer, batch, vocabulary_size=1,
                objective=objective, objective_id=objective.objective_id)
        self.assertAlmostEqual(step()["loss"], 1)
        self.assertTrue(torch.all(model.bias > 0))
        batch["phraseAnalysisFrames"] = 4
        with self.assertRaises(ValueError): step()
        self.assertEqual(model.calls, 1)
        batch["phraseAnalysisFrames"] = 3
        model.fs2.use_spk_id = True
        with self.assertRaises(ValueError): step()
        self.assertEqual(model.calls, 1)


if __name__ == "__main__":
    unittest.main()
