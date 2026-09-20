import unittest

from tools.voice_model_training.paired_vocoder_comparison import summarize, UNVOICED, VOICED


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


if __name__ == '__main__':
    unittest.main()
