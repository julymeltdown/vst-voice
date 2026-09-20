import unittest
from unittest.mock import patch
import numpy as np

from tools.voice_model_training.paired_vocoder_evaluation import run_graph, clip_phones


class GraphRunTests(unittest.TestCase):
    def test_tail_padding_is_excluded_not_compared(self):
        labels=[dict(phone='s',startFrame=0,endFrame=1024),
                dict(phone='a',startFrame=1024,endFrame=2048)]
        rows=clip_phones(labels,1500)
        self.assertEqual([(r['startFrame'],r['endFrame']) for r in rows],[(0,1024),(1024,1500)])
        for valid in (0,2049,True):
            with self.assertRaises(ValueError):clip_phones(labels,valid)
        with self.assertRaises(ValueError):
            clip_phones([dict(phone='a',startFrame=0,endFrame=512)],2048)
        with self.assertRaises(ValueError):
            clip_phones([dict(phone='a',startFrame=5,endFrame=2048)],2048)

    def test_single_use_session_and_exact_padding(self):
        import onnxruntime as ort
        class Session:
            def __init__(self): self.runs=[]
            def run(self, outputs, inputs):
                self.runs.append((outputs, inputs))
                return [np.zeros((1, inputs['mel'].shape[2]*256), np.float32)]
        session=Session();options=type('O',(),{'intra_op_num_threads':0,'inter_op_num_threads':0})()
        runtime=type('R',(),{'disable_telemetry_events':staticmethod(lambda:None),
            'SessionOptions':staticmethod(lambda:options),'InferenceSession':staticmethod(lambda *a,**k:session)})()
        wave=run_graph(b'graph',np.zeros((1,80,3),np.float32),np.zeros((1,3),np.float32),frames=3,_runtime=runtime)
        self.assertEqual(wave.shape,(768,));self.assertEqual(session.runs[0][0],['waveform'])
        self.assertEqual(options.intra_op_num_threads,1)
        for bad in (dict(frames=4),dict(mel=np.zeros((1,80,3),np.float64)),):
            with self.assertRaises(ValueError):
                run_graph(b'graph',bad.get('mel',np.zeros((1,80,3),np.float32)),
                          np.zeros((1,3),np.float32),frames=bad.get('frames',3),_runtime=runtime)
        with self.assertRaises(ValueError):run_graph(b'',np.zeros((1,80,3),np.float32),np.zeros((1,3),np.float32),frames=3,_runtime=runtime)

    def test_fresh_session_per_call(self):
        created=[]
        class Runtime:
            @staticmethod
            def disable_telemetry_events(): pass
            @staticmethod
            def SessionOptions(): return type('O',(),{'intra_op_num_threads':0,'inter_op_num_threads':0})()
            @staticmethod
            def InferenceSession(*a,**k):
                created.append(a)
                return type('S',(),{'run':staticmethod(lambda o,i:[np.zeros((1,i['mel'].shape[2]*256),np.float32)])})()
        for _ in range(2):run_graph(b'g',np.zeros((1,80,2),np.float32),np.zeros((1,2),np.float32),frames=2,_runtime=Runtime)
        self.assertEqual(len(created),2)


if __name__=='__main__':unittest.main()
