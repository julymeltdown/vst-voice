"""Measure a PCM application master against a mono source without alignment or gain fitting.

This is a diagnostic, not singer qualification. Stereo masters are explicitly
averaged; channel peaks/RMS remain visible so cancellation is not hidden.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import tempfile
import wave

import numpy as np
from scipy.io import wavfile

from .__main__ import publish_new
from .audio_source import decode_pcm_source
from .pitch_comparison import _capture, compare_wavs
from .vocoder_reconstruction import compute_stft_spectral_distance


def decode_master(payload):
    with wave.open(io.BytesIO(payload), 'rb') as reader:
        channels, width, rate, frames = (reader.getnchannels(), reader.getsampwidth(),
                                        reader.getframerate(), reader.getnframes())
        if (channels not in (1, 2) or width not in (2, 3, 4)
                or rate != 48000 or not 2048 <= frames <= 16000000
                or reader.getcomptype() != 'NONE'):
            raise ValueError('Expected bounded 48 kHz mono/stereo PCM16/24/32 master')
        pcm = reader.readframes(frames)
    if len(pcm) != channels * width * frames:
        raise ValueError('Truncated application master')
    if width == 3:
        octets = np.frombuffer(pcm, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        integers = octets[:, 0] | (octets[:, 1] << 8) | (octets[:, 2] << 16)
        integers = (integers ^ 0x800000) - 0x800000
    else:
        integers = np.frombuffer(pcm, dtype='<i2' if width == 2 else '<i4')
    samples = integers.reshape(frames, channels).astype(np.float64) / (1 << (8 * width - 1))
    return dict(sampleRate=rate, frameCount=frames, channels=channels, sampleWidthBytes=width), samples


def measure(reference, candidate, *, executable):
    source_bytes, source_hash = _capture(reference, 64 * 1024 * 1024)
    master_bytes, master_hash = _capture(candidate, 64 * 1024 * 1024)
    source_identity, source = decode_pcm_source(source_bytes,
        expected_sha256=source_hash, sample_rate=48000)
    master_identity, channels = decode_master(master_bytes)
    if source_identity['frameCount'] != master_identity['frameCount']:
        raise ValueError('Source/master lengths differ; no trimming, shifting or resampling')
    mono = channels.mean(axis=1)
    # Extract from captured data, not mutable original paths. Both derived inputs
    # use the same float32 encoding and the receipt retains the original hashes.
    with tempfile.TemporaryDirectory(prefix='seam-master-comparison-') as directory:
        source_path, master_path = (Path(directory) / name for name in ('source.wav', 'master.wav'))
        wavfile.write(source_path, 48000, source.astype(np.float32))
        wavfile.write(master_path, 48000, mono.astype(np.float32))
        pitch = compare_wavs(source_path, master_path, executable=executable)
    return dict(formatId='com.project-seam.application-audio-comparison', schemaVersion=1,
        referenceSha256=source_hash, candidateSha256=master_hash,
        reference=source_identity, candidate=master_identity,
        channelPolicy='arithmetic-mean-no-gain-compensation',
        alignment='exact-source-frame-no-shift-no-warp',
        pitchInputEncoding='ieee-float32-le',
        spectralDistance=compute_stft_spectral_distance(mono, source),
        sourceRms=float(np.sqrt(np.mean(source ** 2))),
        downmixedRms=float(np.sqrt(np.mean(mono ** 2))),
        sourcePeak=float(np.max(np.abs(source))), downmixedPeak=float(np.max(np.abs(mono))),
        channelRms=np.sqrt(np.mean(channels ** 2, axis=0)).tolist(),
        channelPeak=np.max(np.abs(channels), axis=0).tolist(),
        channelNonzeroSamples=np.count_nonzero(channels, axis=0).tolist(),
        pitch=pitch, singerQualified=False, releaseEligible=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('reference', 'candidate', 'pitch-executable', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists() or args.output.is_symlink():
        parser.error('Output must be a new report path')
    report = measure(args.reference, args.candidate, executable=args.pitch_executable)
    publish_new(args.output, report)
    print(json.dumps(dict(output=str(args.output),
        reportSha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),
        spectralDistance=report['spectralDistance'],
        pitch={key: report['pitch']['comparison'][key] for key in (
            'status', 'measurableVoicedPairs', 'withinToleranceFrames',
            'unmeasurableFrames', 'meanAbsoluteCents')},
        singerQualified=False), allow_nan=False))


if __name__ == '__main__':
    main()
