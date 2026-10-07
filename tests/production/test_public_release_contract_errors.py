from __future__ import annotations

from contextlib import contextmanager
import unittest

from tools.public_release.contracts import ReleaseGateInputError


class PublicReleaseInputErrorTests(unittest.TestCase):
    def test_context_manager_preserves_the_original_gate_refusal(self):
        refusal = ReleaseGateInputError("supervised soak evidence is required")

        @contextmanager
        def restored_archive():
            yield

        with self.assertRaises(ReleaseGateInputError) as caught:
            with restored_archive():
                raise refusal

        self.assertIs(refusal, caught.exception)
        self.assertEqual(("supervised soak evidence is required",), refusal.args)
        self.assertEqual("supervised soak evidence is required", str(refusal))

    def test_unittest_reports_the_gate_refusal_without_masking_it(self):
        class RefusedPromotion(unittest.TestCase):
            def runTest(self):
                raise ReleaseGateInputError("supervised soak evidence is required")

        result = unittest.TestResult()
        RefusedPromotion().run(result)

        self.assertEqual(1, result.testsRun)
        self.assertEqual([], result.failures)
        self.assertEqual(1, len(result.errors))
        self.assertIn("ReleaseGateInputError: supervised soak evidence is required", result.errors[0][1])


if __name__ == "__main__":
    unittest.main()
