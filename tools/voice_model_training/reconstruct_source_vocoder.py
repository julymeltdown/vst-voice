"""Isolate an exported vocoder using source-derived mel and measured native F0.

Diagnostic inference only: no acoustic prediction, score conditioning, training,
gain fitting or singer qualification. Use an explicitly trusted local export.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import tempfile

import numpy as np
from scipy.io import wavfile

from .__main__ import publish_new
from .acoustics import wav_log_mel_targets
from .audio_source import decode_pcm_source
from .native_features import extract_pitch
from .pitch_comparison import _capture, _track, compare_wavs
from .prepare_bundle import read_report, VOCODER_FORMAT
from .vocoder_reconstruction import compute_stft_spectral_distance


def checked_waveform(value, *, source_frames, hop=256):
    padded = ((source_frames + hop - 1) // hop) * hop
    value = np.asarray(value)
    if (value.shape != (1, padded) or not np.isfinite(value).all()
            or np.max(np.abs(value)) > 1):
        raise ValueError('Vocoder output must be finite normalized mono with exact whole-hop padding')
    return value[0, :source_frames].astype(np.float32, copy=True)


def reconstruct(source, export, *, source_sha256, executable, output):
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError('Output must be a new directory with an existing parent')
    payload, digest = _capture(source, 8 * 1024 * 1024)
    if digest != source_sha256:
        raise ValueError('Source differs from the independently selected digest')
    identity, audio = decode_pcm_source(payload, expected_sha256=digest, sample_rate=48000)
    if not 2048 <= len(audio) <= 4096 * 256:
        raise ValueError('Select a complete source between 2048 samples and 4096 hops')
    exported, graph = read_report(export, VOCODER_FORMAT,
        'vocoderPath', 'vocoderSha256', 'vocoderBytes')
    target, mel = wav_log_mel_targets(payload, expected_sha256=digest, sample_rate=48000)
    if target['profileSha256'] != exported['profileSha256']:
        raise ValueError('Source analysis profile differs from the exported vocoder')
    _, extractor_hash = _capture(executable, 128 * 1024 * 1024)
    with tempfile.TemporaryDirectory(prefix='seam-vocoder-source-') as directory:
        captured = Path(directory) / 'source.wav'
        captured.write_bytes(payload)
        track = extract_pitch(executable, captured)
    rows, _ = _track(track, digest, 48000, len(audio), 256)
    f0 = np.asarray([[row['f0Hz'] for row in rows]], dtype=np.float32)
    import onnxruntime as ort
    ort.disable_telemetry_events()
    options = ort.SessionOptions()
    options.intra_op_num_threads = options.inter_op_num_threads = 1
    session = ort.InferenceSession(graph, options, providers=['CPUExecutionProvider'])
    predicted = session.run(['waveform'], {'mel': mel[None], 'f0': f0})[0]
    rendered = checked_waveform(predicted, source_frames=len(audio))
    buffer = io.BytesIO()
    wavfile.write(buffer, 48000, rendered)
    output.mkdir(mode=0o700)
    (output / 'source.wav').write_bytes(payload)
    (output / 'reconstruction.wav').write_bytes(buffer.getvalue())
    comparison = compare_wavs(output / 'source.wav', output / 'reconstruction.wav', executable=executable)
    if comparison['extractor']['sha256'] != extractor_hash:
        raise ValueError('Pitch extractor changed during reconstruction')
    report = dict(formatId='com.project-seam.source-vocoder-diagnostic', schemaVersion=1,
        sourceSha256=digest, source=identity, vocoderSha256=exported['vocoderSha256'],
        checkpointReceiptSha256=exported['checkpointReceiptSha256'],
        targetSha256=target['targetSha256'], profileSha256=target['profileSha256'],
        f0Sha256=hashlib.sha256(f0.astype('<f4').tobytes()).hexdigest(),
        conditioning='source-derived-mel-and-native-measured-f0',
        outputSha256=hashlib.sha256(buffer.getvalue()).hexdigest(),
        spectralDistance=compute_stft_spectral_distance(rendered, audio), pitch=comparison,
        runtimeVersion=ort.__version__, acousticModelEvaluated=False, trainingPerformed=False,
        sourceRightsRevalidated=False, singerQualified=False, releaseEligible=False)
    publish_new(output / 'diagnostic.json', report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'vocoder-export', 'pitch-executable', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--source-sha256', required=True)
    args = parser.parse_args()
    report = reconstruct(args.source, args.vocoder_export, source_sha256=args.source_sha256,
                         executable=args.pitch_executable, output=args.output)
    pitch = report['pitch']['comparison']
    print(json.dumps(dict(output=str(args.output), spectralDistance=report['spectralDistance'],
        pitch={key: pitch[key] for key in ('status', 'measurableVoicedPairs',
            'withinToleranceFrames', 'unmeasurableFrames', 'meanAbsoluteCents')}, singerQualified=False)))


if __name__ == '__main__':
    main()
