"""Real file/signature/batch/optimizer integration using a tiny test model."""
import unittest

try:
    import torch
    import numpy
except ImportError:
    torch = None


@unittest.skipIf(torch is None, "Optional Torch/NumPy training environment required")
class ReviewedIntegrationTests(unittest.TestCase):
    def test_complete_reviewed_run_without_mocking_admission_or_training(self):
        from tools.voice_model_training.check_reviewed_run import check_reviewed_run

        class Model(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.level = torch.nn.Parameter(torch.tensor(0., dtype=torch.float32))

        class Objective:
            objective_id = "integration-test-scalar-l2"

            def __call__(self, model, inputs, target):
                return (model.level.expand_as(target) - target).square()

        model = Model()
        result = check_reviewed_run(model, torch.optim.AdamW(model.parameters(), lr=.001),
                                    objective=Objective(), model_metadata={"testModel": "scalar"})
        self.assertTrue(result["passed"])
        self.assertEqual(result["partitionCounts"], dict(train=1, validation=1, test=1))
        self.assertEqual(result["epoch"]["coveredSourceFrames"], {"fixture-0": 16})
        self.assertNotIn(result["validation"][0]["sourceId"], result["epoch"]["coveredSourceFrames"])
        self.assertFalse(result["releaseEligible"])
        self.assertFalse(result["singerQualified"])


if __name__ == "__main__":
    unittest.main()
