"""Compare two vocoder graphs on identical features; never selects a winner.

Fresh single-use sessions per run, byte-bound graphs, explicit phone ownership.
Descriptive per-arm and per-phone results only; no promotion or qualification.
"""
import hashlib
import numpy as np

from .pitch_comparison import _capture, compare_wavs
from .phone_periodicity import measure as measure_phones
from .prepare_bundle import read_report, VOCODER_FORMAT
from .reconstruct_source_vocoder import checked_waveform


def run_graph(graph, mel, f0, *, frames, noise=None, _runtime=None):
    """Execute one bounded vocoder graph with a fresh session."""
    if (not isinstance(graph, bytes) or not 1 <= len(graph) <= 256 * 1024 * 1024
            or mel.ndim != 3 or mel.shape != (1, frames, 80)
            or f0.shape != (1, frames) or mel.dtype != np.float32 or f0.dtype != np.float32
            or not np.isfinite(mel).all() or not np.isfinite(f0).all()
            or type(frames) is not int or not 1 <= frames <= 4096):
        raise ValueError('Expected bounded graph bytes and float32 mel/F0 at matching frames')
    if _runtime is None:
        import onnxruntime as _runtime
    _runtime.disable_telemetry_events()
    options = _runtime.SessionOptions()
    options.intra_op_num_threads = options.inter_op_num_threads = 1
    session = _runtime.InferenceSession(graph, options, providers=['CPUExecutionProvider'])
    feeds = dict(mel=mel, f0=f0)
    declared = ({item.name for item in session.get_inputs()}
                if hasattr(session, 'get_inputs') else {'mel', 'f0'})
    if 'noise' in declared:
        if noise is None:
            raise ValueError('Vocoder graph declares a noise input; supply a realization')
        noise = np.asarray(noise, dtype=np.float32)
        if noise.shape != (1, frames * 64) or not np.isfinite(noise).all():
            raise ValueError('Expected finite float32 source-rate noise matching the frames')
        feeds['noise'] = noise
    elif noise is not None:
        raise ValueError('Noise supplied to a graph without a noise input')
    predicted = session.run(['waveform'], feeds)[0]
    # The pinned graph consumes batch-first (frames, 80) mel, not channel-first.
    return checked_waveform(predicted, source_frames=frames * 256)


def clip_phones(labels, valid_samples):
    """Honest tail policy: drop phone frames that fall in excluded padding."""
    if type(valid_samples) is not int or valid_samples <= 0:
        raise ValueError('Compared sample count must be a positive integer')
    end, rows = 0, []
    for phone in labels:
        start, stop = phone['startFrame'], phone['endFrame']
        if start != end or not start < stop:
            raise ValueError('Label phones must cover the source contiguously')
        end = stop
        left, right = min(start, valid_samples), min(stop, valid_samples)
        if left < right:
            rows.append(dict(phone, startFrame=left, endFrame=right))
    if end < valid_samples:
        raise ValueError('Label phones do not cover the compared samples')
    return rows


def validate_arm_noises(noises, arms):
    """Per-arm noise map rules for excitation experiments.

    Keys must be arm names, and no two arms may share an identical nonzero
    realization: a common nonzero tensor would excite a zero-noise control
    and destroy the contrast the experiment exists to measure.
    """
    if noises is None:
        return
    if not isinstance(noises, dict) or set(noises) - set(arms):
        raise ValueError('Noise realizations must be keyed by arm name')
    nonzero = {name: value for name, value in noises.items()
               if value is not None and np.asarray(value).any()}
    if len({np.asarray(v).tobytes() for v in nonzero.values()}) < len(nonzero):
        raise ValueError('Arms must not share an identical nonzero noise realization')


def evaluate(source, labels, arms, *, mel_input, f0, gains, executable,
             valid_samples=None, noises=None, _runtime=None):
    """Return per-arm pitch and per-phone waveform comparison for one source."""
    import tempfile
    from pathlib import Path
    from scipy.io import wavfile
    payload, source_hash = _capture(source, 64 * 1024 * 1024)
    mel, f0 = np.asarray(mel_input, np.float32), np.asarray(f0, np.float32)
    gains = np.asarray(gains, np.float32)
    frames = mel.shape[1] if mel.ndim == 3 else -1
    padded = frames * 256
    if (mel.ndim != 3 or mel.shape[0] != 1 or mel.shape[2] != 80
            or f0.shape != (1, frames) or gains.shape != (len(gains),) or not 1 <= len(gains) <= padded
            or not isinstance(arms, dict) or not 1 <= len(arms) <= 4
            or not isinstance(labels, list) or not 1 <= len(labels) <= 128):
        raise ValueError('Expected captured source, bounded arms and complete label rows')
    reference = reference_wave(source)
    if valid_samples is None:
        valid_samples = len(reference)
    # The captured source may be shorter than the vocoder's whole-hop output;
    # the excluded tail is reported explicitly and never compared as audio.
    if (type(valid_samples) is not int or not 1 <= valid_samples <= min(len(reference), padded)
            or padded - len(reference) > 255):
        raise ValueError('Compared samples must lie inside both captured source and padded output')
    reference, phones = reference[:valid_samples], clip_phones(labels, valid_samples)
    graph_hashes, results = {}, {}
    validate_arm_noises(noises, arms)
    for name, export in arms.items():
        exported, graph = read_report(Path(export), VOCODER_FORMAT,
            'vocoderPath', 'vocoderSha256', 'vocoderBytes')
        if exported['vocoderSha256'] in graph_hashes.values():
            raise ValueError('Arms must use distinct vocoder graphs')
        graph_hashes[name] = exported['vocoderSha256']
        # Dynamics ownership covers the captured samples; the padded tail is
        # zero-padded here rather than scaled by an unrelated gain value.
        owned = np.zeros(padded, np.float32)
        owned[:len(gains)] = gains
        arm_noise = None if noises is None else noises.get(name)
        wave = (run_graph(graph, mel, f0, frames=frames,
                          noise=arm_noise,
                          _runtime=_runtime)
                * owned)[:valid_samples]
        if not np.isfinite(wave).all() or float(np.max(np.abs(wave))) > 1:
            raise ValueError('Vocoder output is nonfinite or unnormalized')
        with tempfile.TemporaryDirectory(prefix='seam-paired-vocoder-') as directory:
            candidate = Path(directory) / f'{name}.wav'
            wavfile.write(candidate, 48000, wave)
            comparison = compare_wavs(source, candidate, executable=executable)
        if comparison['reference']['sourceSha256'] != source_hash:
            raise ValueError('Measured source changed during comparison')
        results[name] = dict(vocoderSha256=exported['vocoderSha256'],
            objectiveId=exported.get('objectiveId'), waveSha256=hashlib.sha256(
                wave.astype('<f4').tobytes()).hexdigest(),
            noiseSha256=(None if arm_noise is None else hashlib.sha256(
                np.asarray(arm_noise, dtype=np.float32).tobytes()).hexdigest()),
            pitch={key: comparison['comparison'][key] for key in (
                'status', 'meanAbsoluteCents', 'measurableVoicedPairs',
                'withinToleranceFrames', 'unmeasurableFrames', 'voicingMismatchFrames')},
            phones=measure_phones(reference, wave, phones)['rows'])
    return dict(formatId='com.project-seam.paired-vocoder-evaluation', schemaVersion=1,
        sourceSha256=source_hash, frames=frames, comparedSamples=valid_samples,
        excludedTailSamples=padded - valid_samples, arms=results,
        policy='Identical features and controls; fresh session per arm; descriptive only',
        singerQualified=False, releaseEligible=False)


def reference_wave(source):
    """Decode the reference PCM once, without resampling or normalization."""
    from .audio_source import decode_pcm_source
    payload, digest = _capture(source, 64 * 1024 * 1024)
    _, audio = decode_pcm_source(payload, expected_sha256=digest, sample_rate=48000)
    return audio
