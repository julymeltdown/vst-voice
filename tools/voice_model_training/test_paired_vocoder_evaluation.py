import importlib.util
import unittest
from unittest.mock import patch
import numpy as np

# This module imports ONNX-backed code, so it can only run in the training
# environment. Skip instead of erroring where that environment is absent.
if importlib.util.find_spec("onnx") is None:
    raise unittest.SkipTest("Optional ONNX environment not installed")

from tools.voice_model_training.paired_vocoder_evaluation import (
    run_graph, clip_phones, derive_arm_noise, validate_noise_spec)


class GraphRunTests(unittest.TestCase):
    def test_tail_padding_is_excluded_not_compared(self):
        from tools.voice_model_training.paired_vocoder_evaluation import clip_phones
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

    def test_partial_final_hop_is_allowed_but_large_truncation_is_not(self):
        import numpy as np
        from tools.voice_model_training import paired_vocoder_evaluation as module
        with self.assertRaises((ValueError,OSError,TypeError)):
            module.evaluate('missing.wav',[],{'arm':'missing'},mel_input=np.zeros((1,2,80),np.float32),
                f0=np.zeros((1,2),np.float32),gains=np.zeros(512,np.float32),executable=None)

    def test_single_use_session_and_exact_padding(self):
        import onnxruntime as ort
        class Session:
            def __init__(self): self.runs=[]
            def run(self, outputs, inputs):
                self.runs.append((outputs, inputs))
                return [np.zeros((1, inputs['mel'].shape[1]*256), np.float32)]
        session=Session();options=type('O',(),{'intra_op_num_threads':0,'inter_op_num_threads':0})()
        runtime=type('R',(),{'disable_telemetry_events':staticmethod(lambda:None),
            'SessionOptions':staticmethod(lambda:options),'InferenceSession':staticmethod(lambda *a,**k:session)})()
        wave=run_graph(b'graph',np.zeros((1,3,80),np.float32),np.zeros((1,3),np.float32),frames=3,_runtime=runtime)
        self.assertEqual(wave.shape,(768,));self.assertEqual(session.runs[0][0],['waveform'])
        self.assertEqual(options.intra_op_num_threads,1)
        for bad in (dict(frames=4),dict(mel=np.zeros((1,3,80),np.float64)),dict(mel=np.zeros((1,80,3),np.float32))):
            with self.assertRaises(ValueError):
                run_graph(b'graph',bad.get('mel',np.zeros((1,3,80),np.float32)),
                          np.zeros((1,3),np.float32),frames=bad.get('frames',3),_runtime=runtime)
        with self.assertRaises(ValueError):run_graph(b'',np.zeros((1,3,80),np.float32),np.zeros((1,3),np.float32),frames=3,_runtime=runtime)

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
                return type('S',(),{'run':staticmethod(lambda o,i:[np.zeros((1,i['mel'].shape[1]*256),np.float32)])})()
        for _ in range(2):run_graph(b'g',np.zeros((1,2,80),np.float32),np.zeros((1,2),np.float32),frames=2,_runtime=Runtime)
        self.assertEqual(len(created),2)

    def test_noise_feed_reaches_declared_input_and_is_rejected_otherwise(self):
        feeds_seen=[]
        class Session:
            def get_inputs(self):
                return [type('I',(),{'name':n}) for n in ('mel','f0','noise')]
            def run(self, outputs, inputs):
                feeds_seen.append(inputs)
                return [np.zeros((1, inputs['mel'].shape[1]*256), np.float32)]
        runtime=type('R',(),{'disable_telemetry_events':staticmethod(lambda:None),
            'SessionOptions':staticmethod(lambda:type('O',(),{'intra_op_num_threads':0,'inter_op_num_threads':0})()),
            'InferenceSession':staticmethod(lambda *a,**k:Session())})
        noise=np.zeros((1,2*64),np.float32)
        run_graph(b'g',np.zeros((1,2,80),np.float32),np.zeros((1,2),np.float32),frames=2,noise=noise,_runtime=runtime)
        self.assertIn('noise',feeds_seen[0])
        with self.assertRaises(ValueError):
            run_graph(b'g',np.zeros((1,2,80),np.float32),np.zeros((1,2),np.float32),frames=2,_runtime=runtime)
        with self.assertRaises(ValueError):
            run_graph(b'g',np.zeros((1,2,80),np.float32),np.zeros((1,2),np.float32),frames=2,
                      noise=np.zeros((1,64),np.float32),_runtime=runtime)

class ArmNoiseTests(unittest.TestCase):
    def spec(self, frames=8):
        rng = np.random.default_rng(5)
        return dict(seed=5,
            rawDraw=rng.standard_normal((1, 1, frames * 64)).astype(np.float32),
            unvoicedFrames=np.array([True] * (frames // 2) + [False] * (frames - frames // 2)))

    def test_zero_arm_receives_zeros_regardless_of_raw_draw(self):
        exported = dict(architectureConfiguration=dict(excitationNoiseId='zero-v1'))
        noise, ident = derive_arm_noise(exported, self.spec(), 8)
        self.assertEqual(ident, 'zero-v1')
        self.assertFalse(np.asarray(noise).any())

    def test_uvnoise_arm_receives_gated_draw_and_off_gate_is_zero(self):
        exported = dict(architectureConfiguration=dict(excitationNoiseId='uv-gated-v1'))
        spec = self.spec()
        noise, ident = derive_arm_noise(exported, spec, 8)
        self.assertEqual(ident, 'uv-gated-v1')
        gate = np.repeat(np.asarray(spec['unvoicedFrames'], np.float32), 64)
        expected = spec['rawDraw'].reshape(-1) * gate * (1.0 / 3.0)
        np.testing.assert_allclose(np.asarray(noise).reshape(-1), expected, rtol=0, atol=0)
        self.assertFalse(np.asarray(noise).reshape(-1)[4 * 64:].any())

    def test_missing_spec_and_missing_identity(self):
        exported = dict(architectureConfiguration=dict(excitationNoiseId='uv-gated-v1'))
        with self.assertRaises(ValueError):
            derive_arm_noise(exported, None, 8)
        plain = dict(architectureConfiguration=dict())
        noise, ident = derive_arm_noise(plain, self.spec(), 8)
        self.assertIsNone(noise)
        self.assertIsNone(ident)

    def test_spec_shape_validation(self):
        bad = self.spec()
        bad['rawDraw'] = np.zeros((1, 1, 8), np.float32)
        with self.assertRaises(ValueError):
            validate_noise_spec(bad, 8)
        bad2 = self.spec()
        bad2['unvoicedFrames'] = np.array([True] * 4)
        with self.assertRaises(ValueError):
            validate_noise_spec(bad2, 8)

    def test_derived_feed_reaches_session_at_graph_rank(self):
        # Connected regression: derive_arm_noise -> run_graph must hand the
        # session a rank-2 [1, frames*64] array, not the training rank.
        feeds_seen = []
        class Session:
            def get_inputs(self):
                return [type('I', (), {'name': n}) for n in ('mel', 'f0', 'noise')]
            def run(self, outputs, inputs):
                feeds_seen.append(inputs)
                return [np.zeros((1, inputs['mel'].shape[1] * 256), np.float32)]
        runtime = type('R', (), {'disable_telemetry_events': staticmethod(lambda: None),
            'SessionOptions': staticmethod(lambda: type('O', (), {'intra_op_num_threads': 0, 'inter_op_num_threads': 0})()),
            'InferenceSession': staticmethod(lambda *a, **k: Session())})
        for frames in (1, 8):
            for identity in ('zero-v1', 'uv-gated-v1'):
                spec = self.spec(frames)
                exported = dict(architectureConfiguration=dict(excitationNoiseId=identity))
                noise, _ = derive_arm_noise(exported, spec, frames)
                self.assertEqual(np.asarray(noise).shape, (1, frames * 64))
                feeds_seen.clear()
                run_graph(b'g', np.zeros((1, frames, 80), np.float32),
                          np.zeros((1, frames), np.float32), frames=frames,
                          noise=noise, _runtime=runtime)
                fed = feeds_seen[0]['noise']
                self.assertEqual(fed.shape, (1, frames * 64))
                np.testing.assert_array_equal(fed, noise)
                if identity == 'uv-gated-v1' and frames == 8:
                    self.assertFalse(fed.reshape(-1)[4 * 64:].any())


if __name__=='__main__':unittest.main()
