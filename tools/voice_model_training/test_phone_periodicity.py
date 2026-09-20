import unittest
import numpy as np
from tools.voice_model_training.phone_periodicity import measure


class PhonePeriodicityTests(unittest.TestCase):
    def test_periodic_signal_differs_from_noise_without_level_hiding(self):
        reference=np.random.default_rng(71).normal(0,.02,4096)
        candidate=.02*np.sin(2*np.pi*np.arange(4096)/256)
        row=measure(reference,candidate,[dict(symbol='s',startFrame=0,endFrame=4096)])['rows'][0]
        self.assertLess(abs(row['reference']['lagCorrelation']),.1)
        self.assertAlmostEqual(row['candidate']['lagCorrelation'],1.)
        self.assertGreater(row['rmsRatio'],0)

    def test_muting_dc_and_short_phones_remain_explicit(self):
        phones=[dict(symbol='s',startFrame=0,endFrame=512),dict(symbol='a',startFrame=512,endFrame=600)]
        rows=measure(np.ones(600)*.1,np.zeros(600),phones)['rows']
        self.assertEqual(rows[0]['candidate']['status'],'NO_CENTERED_ENERGY')
        self.assertEqual(rows[0]['reference']['status'],'NO_CENTERED_ENERGY')
        self.assertIsNone(rows[0]['candidate']['lagCorrelation'])
        self.assertEqual(rows[0]['rmsRatio'],0)
        self.assertEqual(rows[1]['reference']['status'],'TOO_SHORT')
        self.assertAlmostEqual(rows[0]['reference']['dc'],.1)

    def test_invalid_geometry_and_nonfinite_rejected(self):
        audio=np.zeros(1024)
        for phones in ([dict(symbol='a',startFrame=1,endFrame=1024)],
                       [dict(symbol='a',startFrame=0,endFrame=512)]):
            with self.assertRaises(ValueError):measure(audio,audio,phones)
        with self.assertRaises(ValueError):
            measure(audio,np.full(1024,np.nan),[dict(symbol='a',startFrame=0,endFrame=1024)])


if __name__=='__main__':unittest.main()
