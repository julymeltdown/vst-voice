import importlib.util
import unittest

import numpy as np

# This module imports ONNX-backed code, so it can only run in the training
# environment. Skip instead of erroring where that environment is absent.
if importlib.util.find_spec("onnx") is None:
    raise unittest.SkipTest("Optional ONNX environment not installed")

from tools.voice_model_training.paired_vocoder_comparison import (
    summarize, UNVOICED, VOICED, phone_index_frames, source_noise_spec)


def row(phone, reference, candidate, ratio=1.):
    return dict(phone=phone, rmsRatio=ratio,
                reference=dict(lagCorrelation=reference),
                candidate=dict(lagCorrelation=candidate))


class SummaryTests(unittest.TestCase):
    def test_classes_stay_separate_and_direction_is_signed(self):
        rows=[row('s',.05,.95),row('h',-.06,.93),row('a',.9,.88),row('m',.8,.7)]
        summary=summarize(rows)
        self.assertEqual(summary['unvoiced']['measuredWindows'],2)
        self.assertAlmostEqual(summary['unvoiced']['meanCandidateMinusReferenceCorrelation'],.945)
        self.assertAlmostEqual(summary['voiced']['meanCandidateMinusReferenceCorrelation'],-.06)
        self.assertAlmostEqual(summary['voiced']['meanReferenceLagCorrelation'],.85)

    def test_missing_or_empty_measurements_do_not_become_zero(self):
        rows=[row('s',None,None),row('a',None,.5),row('k',.2,None)]
        summary=summarize(rows)
        self.assertEqual(summary['unvoiced']['measuredWindows'],0)
        self.assertIsNone(summary['unvoiced']['meanCandidateLagCorrelation'])
        self.assertIsNone(summary['voiced']['meanCandidateMinusReferenceCorrelation'])
        self.assertEqual(summary['voiced']['phoneWindows'],1)
        self.assertIsNone(summarize([])['unvoiced']['meanRmsRatio'])

    def test_class_inventories_do_not_overlap(self):
        self.assertFalse(set(UNVOICED) & set(VOICED))

    def test_class_keys_hold_their_own_measurements(self):
        # Guards against nesting one class summary inside the other.
        rows=[row('s',.0,.5),row('a',.4,.35)]
        unvoiced, voiced = summarize(rows)['unvoiced'], summarize(rows)['voiced']
        self.assertEqual(unvoiced['phoneWindows'],1)
        self.assertEqual(voiced['phoneWindows'],1)
        self.assertAlmostEqual(unvoiced['meanCandidateMinusReferenceCorrelation'],.5)
        self.assertAlmostEqual(voiced['meanCandidateMinusReferenceCorrelation'],-.05)


class NoiseSpecTests(unittest.TestCase):
    def phones(self):
        return [dict(symbol='t', startFrame=0, endFrame=2880),
                dict(symbol='e', startFrame=2880, endFrame=24000),
                dict(symbol='s', startFrame=24000, endFrame=44880),
                dict(symbol='u', startFrame=44880, endFrame=66000)]

    def test_left_edge_ownership_matches_conditioning_rule(self):
        # Frame 11 starts at sample 2816 inside phone t; frame 12 at 3072
        # falls into phone e. Frame 257 (65792) is still inside u at 66000.
        indices = phone_index_frames(self.phones(), 258)
        self.assertEqual(len(indices), 258)
        self.assertEqual(indices[0], 0)
        self.assertEqual(indices[11], 0)
        self.assertEqual(indices[12], 1)
        self.assertEqual(indices[257], 3)
        with self.assertRaises(ValueError):
            phone_index_frames(self.phones(), 259)

    def test_spec_shapes_gate_and_seed_reproducibility(self):
        spec = source_noise_spec(self.phones(), 258, 933)
        self.assertEqual(spec['seed'], 933)
        self.assertEqual(spec['rawDraw'].shape, (1, 1, 258 * 64))
        gate = spec['unvoicedFrames']
        self.assertEqual(gate.shape, (258,))
        # t and s are admitted unvoiced; e and u are not.
        self.assertTrue(gate[0] and not gate[12])
        self.assertTrue(gate[94] and gate[175] and not gate[200])
        again = source_noise_spec(self.phones(), 258, 933)
        np.testing.assert_array_equal(again['rawDraw'], spec['rawDraw'])


if __name__ == '__main__':
    unittest.main()
