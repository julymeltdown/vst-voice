"""Explicit full-hop log-mel targets; optional NumPy dependency, no model claim."""
import math
import hashlib
import json

from .audio_source import decode_pcm_source


def wav_log_mel_targets(payload: bytes, *, expected_sha256: str, sample_rate: int,
                        fft_size: int = 1024, hop_size: int = 256, bins: int = 80,
                        minimum_hz: float = 20, maximum_hz: float | None = None):
    """Return byte-bound target metadata and owned float32 mel data, without writes.

    Targets are row-major little-endian float32 for hashing/storage. No dither,
    normalization, resampling, source permission or model compatibility is inferred.
    """
    import numpy as np
    source, samples = decode_pcm_source(payload, expected_sha256=expected_sha256, sample_rate=sample_rate)
    targets = log_mel_targets(samples, sample_rate=sample_rate, fft_size=fft_size,
                             hop_size=hop_size, bins=bins, minimum_hz=minimum_hz, maximum_hz=maximum_hz)
    profile = dict(profileId="seam-full-hop-slaney-v1", sampleRate=sample_rate,
                   fftSize=fft_size, windowSize=fft_size, hopSize=hop_size, bins=bins,
                   minimumHz=minimum_hz, maximumHz=sample_rate / 2 if maximum_hz is None else maximum_hz,
                   tailPadding="zero-to-whole-hop", boundaryPadding="reflect-fft-minus-hop",
                   window="periodic-hann", spectrum="unnormalized-magnitude", melNormalization="slaney-area",
                   melFrequencyScale="slaney", amplitudeScale="ln-amplitude", floor=1e-5,
                   layout="TF", dtype="float32-le")
    raw = targets.astype("<f4", copy=False).tobytes(order="C")
    record = dict(formatId="com.project-seam.training-acoustic-target", schemaVersion=1,
                  sourceSha256=source["sourceSha256"], audioSha256=source["audioSha256"],
                  sourceFrameCount=source["frameCount"], analysisFrameCount=len(targets),
                  profile=profile, numpyVersion=np.__version__, targetBytes=len(raw),
                  targetSha256=hashlib.sha256(raw).hexdigest(),
                  profileSha256=hashlib.sha256(json.dumps(profile, sort_keys=True, separators=(",", ":")).encode()).hexdigest(),
                  trainingAdmitted=False, releaseEligible=False)
    return record, targets


def log_mel_targets(samples, *, sample_rate: int, fft_size: int = 1024,
                    hop_size: int = 256, bins: int = 80, minimum_hz: float = 20,
                    maximum_hz: float | None = None):
    """SEAM full-hop Slaney profile, not automatic compatibility with any weights.

    Zero-pad the right edge to a whole hop, then reflect by (FFT-hop)/2.
    This yields ceil(N/hop) frames with a periodic Hann window of FFT length.
    Reject clips too short for reflect padding. Work is blocked in 128 frames.
    """
    import numpy as np
    if (type(sample_rate) is not int or not 8000 <= sample_rate <= 192000
            or type(fft_size) is not int or not 64 <= fft_size <= 8192 or fft_size & (fft_size - 1)
            or type(hop_size) is not int or not 1 <= hop_size <= fft_size
            or type(bins) is not int or not 1 <= bins <= 512):
        raise ValueError("Invalid acoustic feature geometry")
    maximum_hz = sample_rate / 2 if maximum_hz is None else maximum_hz
    if (any(type(x) not in (int, float) or not math.isfinite(x) for x in (minimum_hz, maximum_hz))
            or not 0 <= minimum_hz < maximum_hz <= sample_rate / 2):
        raise ValueError("Invalid mel frequency interval")
    audio = np.asarray(samples)
    if (audio.ndim != 1 or audio.dtype.kind not in "fi" or not 1 <= audio.size <= 16000000
            or not np.isfinite(audio).all() or np.max(np.abs(audio.astype(np.float64))) > 1):
        raise ValueError("Expected bounded mono normalized finite samples")
    count = (audio.size + hop_size - 1) // hop_size
    if count > 65536 or count * bins > 8388608 or count * bins * (fft_size // 2 + 1) > 536870912:
        raise ValueError("Acoustic output or projection work budget exceeded")
    left, right = (fft_size - hop_size) // 2, (fft_size - hop_size + 1) // 2
    if audio.size <= max(left, right):
        raise ValueError("Source is too short for the profile's reflection padding")
    def mel(hz):
        return hz / (200 / 3) if hz < 1000 else 15 + math.log(hz / 1000) / (math.log(6.4) / 27)
    points = np.linspace(mel(minimum_hz), mel(maximum_hz), bins + 2)
    hz = np.where(points < 15, points * (200 / 3), 1000 * np.exp((points - 15) * math.log(6.4) / 27))
    frequencies = np.arange(fft_size // 2 + 1) * sample_rate / fft_size
    lower = (frequencies[None, :] - hz[:-2, None]) / (hz[1:-1] - hz[:-2])[:, None]
    upper = (hz[2:, None] - frequencies[None, :]) / (hz[2:] - hz[1:-1])[:, None]
    filters = np.maximum(0, np.minimum(lower, upper)) * (2 / (hz[2:] - hz[:-2]))[:, None]
    if np.any(filters.max(axis=1) <= 0):
        raise ValueError("Mel profile contains an empty filter")
    audio = np.pad(audio.astype(np.float64), (0, count * hop_size - audio.size))
    audio = np.pad(audio, (left, right), mode="reflect")
    window = .5 - .5 * np.cos(2 * np.pi * np.arange(fft_size) / fft_size)
    output = np.empty((count, bins), dtype=np.float32)
    for start in range(0, count, 128):
        indices = np.arange(start, min(count, start + 128))[:, None] * hop_size + np.arange(fft_size)[None, :]
        magnitude = np.abs(np.fft.rfft(audio[indices] * window, axis=1))
        output[start:start + len(indices)] = np.log(np.maximum(magnitude @ filters.T, 1e-5))
    return output
