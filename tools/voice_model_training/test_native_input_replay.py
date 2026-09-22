import copy
import importlib.util
import types
import unittest
import numpy as np

# This module imports ONNX-backed code, so it can only run in the training
# environment. Skip instead of erroring where that environment is absent.
if importlib.util.find_spec("onnx") is None:
    raise unittest.SkipTest("Optional ONNX environment not installed")

from tools.voice_model_training.native_input_replay import prepare_inputs, replay


def capture():
    return dict(formatId='com.project-seam.native-input-replay', schemaVersion=1,
                workerTensorCapture=False, outputSampleFrames=2050, paddedSampleFrames=2304,
                tokens=[1, 2], durations=[4, 5], f0=[440.] * 9, breathiness=[],
                dynamicsRuns=[[1024, 1.], [2050, 0.]])


class ReplayTests(unittest.TestCase):
    def test_geometry_controls_and_exact_gains(self):
        inputs, breathiness, gains = prepare_inputs(capture(), 10)
        self.assertEqual(inputs['steps'].shape, ())
        self.assertEqual(inputs['tokens'].dtype, np.int64)
        self.assertIsNone(breathiness)
        np.testing.assert_array_equal(gains[:1024], 1)
        np.testing.assert_array_equal(gains[1024:], 0)

    def test_bad_capture_is_refused(self):
        cases = dict(outputSampleFrames=480001, paddedSampleFrames=2050, tokens=[True, 2],
                     durations=[4, 4], f0=[float('nan')] * 9, breathiness=[2.] * 9,
                     dynamicsRuns=[[1024, 1.]], workerTensorCapture=True)
        for field, value in cases.items():
            with self.subTest(field=field), self.assertRaises(ValueError):
                prepare_inputs(dict(capture(), **{field: value}), 10)
        for steps in (True, 0, 65, 1.5):
            with self.assertRaises(ValueError):
                prepare_inputs(capture(), steps)

    def test_nonmonotone_nonfinite_or_missing_dynamics_refused(self):
        for runs in (None, [], [[1024, 1], [1024, 0]], [[2050, float('nan')]], [[2051, 1]]):
            with self.assertRaises(ValueError):
                prepare_inputs(dict(capture(), dynamicsRuns=runs), 10)

    def test_each_replay_uses_fresh_single_call_sessions(self):
        sessions = []
        class Session:
            def __init__(self, graph, options, providers):
                self.graph, self.calls = graph, 0
                sessions.append(self)
            def get_inputs(self):
                return [types.SimpleNamespace(name=x) for x in ('tokens', 'durations', 'f0', 'steps')]
            def run(self, outputs, inputs):
                self.calls += 1
                if self.graph == b'acoustic':
                    return [np.full((1, 9, 80), self.calls, dtype=np.float32)]
                return [np.full((1, 2304), .25, dtype=np.float32)]
        runtime = types.SimpleNamespace(disable_telemetry_events=lambda: None,
                    SessionOptions=types.SimpleNamespace, InferenceSession=Session)
        original = capture()
        # Native preparation can emit a zero-filled optional vector even when
        # the admitted four-input graph has no breathiness control.
        original['breathiness'] = [0.] * 9
        saved = copy.deepcopy(original)
        first = replay(b'acoustic', b'vocoder', original, steps=10, _runtime=runtime)
        second = replay(b'acoustic', b'vocoder', original, steps=10, _runtime=runtime)
        self.assertEqual(len(sessions), 4)
        self.assertTrue(all(s.calls == 1 for s in sessions))
        np.testing.assert_array_equal(first[0], second[0])
        np.testing.assert_array_equal(first[1][:1024], .25)
        np.testing.assert_array_equal(first[1][1024:], 0)
        self.assertEqual(original, saved)
        with self.assertRaises(ValueError):
            replay(b'acoustic', b'vocoder', dict(original, breathiness=[.2] * 9),
                   steps=10, _runtime=runtime)


if __name__ == '__main__':
    unittest.main()
