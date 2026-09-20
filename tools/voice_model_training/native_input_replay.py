"""Bounded diagnostic replay; callers must bind graph/source/project identities.

Never cache sessions here: seeded ONNX random operators advance per Run, whereas
the production worker creates sessions for a single request. No qualification.
"""
import numpy as np

from .reconstruct_source_vocoder import checked_waveform


def prepare_inputs(captured, steps):
    if (captured.get('formatId') != 'com.project-seam.native-input-replay'
            or captured.get('schemaVersion') != 1
            or captured.get('workerTensorCapture') is not False
            or type(steps) is not int or not 1 <= steps <= 64):
        raise ValueError('Expected native replay inputs and 1..64 diagnostic steps')
    count = captured.get('outputSampleFrames')
    if type(count) is not int or not 1 <= count <= 480000:
        raise ValueError('Replay is bounded to ten seconds at 48 kHz')
    frames = (count + 255) // 256
    if captured.get('paddedSampleFrames') != frames * 256:
        raise ValueError('Replay padding differs from the native hop contract')
    tokens, durations = captured.get('tokens'), captured.get('durations')
    if (not isinstance(tokens, list) or not 1 <= len(tokens) <= 4096
            or not isinstance(durations, list) or len(durations) != len(tokens)
            or any(type(x) is not int or not 1 <= x <= 65535 for x in tokens)
            or any(type(x) is not int or not 0 <= x <= frames for x in durations)
            or sum(durations) != frames):
        raise ValueError('Invalid native token/duration geometry')
    def frame_values(name, maximum, optional=False):
        values = captured.get(name)
        if optional and values == []:
            return None
        if (not isinstance(values, list) or len(values) != frames
                or any(type(x) not in (int, float) or not np.isfinite(x)
                       or not 0 <= x <= maximum for x in values)):
            raise ValueError('Invalid native ' + name + ' frame values')
        return np.asarray([values], dtype=np.float32)
    f0 = frame_values('f0', 24000)
    breathiness = frame_values('breathiness', 1, optional=True)
    runs = captured.get('dynamicsRuns')
    if not isinstance(runs, list) or not 1 <= len(runs) <= count:
        raise ValueError('Exact native dynamics runs are required')
    previous = 0
    for run in runs:
        if (not isinstance(run, list) or len(run) != 2 or type(run[0]) is not int
                or not previous < run[0] <= count or type(run[1]) not in (int, float)
                or not np.isfinite(run[1]) or not 0 <= run[1] <= 1):
            raise ValueError('Invalid native dynamics run')
        previous = run[0]
    if previous != count:
        raise ValueError('Native dynamics must cover the complete output')
    gains = np.empty(count, dtype=np.float32)
    previous = 0
    for end, gain in runs:
        gains[previous:end] = gain
        previous = end
    inputs = dict(tokens=np.asarray([tokens], dtype=np.int64),
                  durations=np.asarray([durations], dtype=np.int64), f0=f0,
                  steps=np.asarray(steps, dtype=np.int64))
    return inputs, breathiness, gains


def replay(acoustic_graph, vocoder_graph, captured, *, steps, _runtime=None):
    """Return predicted mel and finalized mono PCM from fresh single-use sessions.

    Inputs are already captured/admitted graph bytes, never inferred file paths.
    Caller owns hash binding and native/master parity including mixer routing.
    """
    inputs, breathiness, gains = prepare_inputs(captured, steps)
    if any(not isinstance(graph, bytes) or not 1 <= len(graph) <= 256 * 1024 * 1024
           for graph in (acoustic_graph, vocoder_graph)):
        raise ValueError('Replay requires bounded captured graph bytes')
    if _runtime is None:
        import onnxruntime as _runtime
    _runtime.disable_telemetry_events()
    options = _runtime.SessionOptions()
    options.intra_op_num_threads = options.inter_op_num_threads = 1
    acoustic = _runtime.InferenceSession(acoustic_graph, options, providers=['CPUExecutionProvider'])
    vocoder = _runtime.InferenceSession(vocoder_graph, options, providers=['CPUExecutionProvider'])
    names = {value.name for value in acoustic.get_inputs()}
    if names == set(inputs) | {'breathiness'}:
        inputs['breathiness'] = np.zeros_like(inputs['f0']) if breathiness is None else breathiness
    elif names != set(inputs) or (breathiness is not None and np.any(breathiness != 0)):
        raise ValueError('Replay controls differ from the acoustic graph interface')
    mel = acoustic.run(['mel'], inputs)[0]
    if (mel.dtype != np.float32 or mel.shape != (1, inputs['f0'].shape[1], 80)
            or not np.isfinite(mel).all()):
        raise ValueError('Invalid acoustic replay mel')
    padded = vocoder.run(['waveform'], dict(mel=mel, f0=inputs['f0']))[0]
    wave = checked_waveform(padded, source_frames=len(gains)) * gains
    if not np.isfinite(wave).all() or np.max(np.abs(wave)) > 1:
        raise ValueError('Invalid finalized replay waveform')
    return mel, wave


def predicted_mel(acoustic_graph, captured, *, steps, _runtime=None):
    """Return only the acoustic mel for a captured replay request.

    Uses a fresh single-use session, matching the production worker's behavior;
    seeded random operators must not be reused across requests.
    """
    inputs, breathiness, _ = prepare_inputs(captured, steps)
    if any(not isinstance(graph, bytes) or not 1 <= len(graph) <= 256 * 1024 * 1024
           for graph in (acoustic_graph,)):
        raise ValueError('Replay requires bounded captured acoustic graph bytes')
    if _runtime is None:
        import onnxruntime as _runtime
    _runtime.disable_telemetry_events()
    options = _runtime.SessionOptions()
    options.intra_op_num_threads = options.inter_op_num_threads = 1
    acoustic = _runtime.InferenceSession(acoustic_graph, options, providers=['CPUExecutionProvider'])
    names = {value.name for value in acoustic.get_inputs()}
    if names == set(inputs) | {'breathiness'}:
        inputs['breathiness'] = (np.zeros_like(inputs['f0']) if breathiness is None else breathiness)
    elif names != set(inputs):
        raise ValueError('Replay controls differ from the acoustic graph interface')
    mel = acoustic.run(['mel'], inputs)[0]
    if (mel.dtype != np.float32 or mel.shape != (1, inputs['f0'].shape[1], 80)
            or not np.isfinite(mel).all()):
        raise ValueError('Invalid acoustic replay mel')
    return mel
