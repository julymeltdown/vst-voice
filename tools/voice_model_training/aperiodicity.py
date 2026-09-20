"""Derive a bounded per-frame aperiodicity estimate from captured reference PCM.

The acoustic model has no aperiodicity channel, so frication has nowhere to
attach. This produces one candidate channel from the source audio itself, on the
same hop clock as the labels, using zero-crossing and spectral flatness evidence.
It is a measurement, not admitted supervision: the caller must bind it to source
and label identities and review it before training on it.
"""
import numpy as np


def _spectral_flatness(frame, floor=1e-10):
    shifted = np.maximum(np.abs(frame), floor)
    return float(np.exp(np.mean(np.log(shifted))) / np.mean(shifted))


def estimate(samples, *, frame_count, hop_size, frame_size=1024):
    """Return per-frame aperiodicity in [0, 1] on the label analysis clock.

    Uses two independent noise-like cues: normalized zero-crossing rate and
    frame-local spectral flatness. Both are high for frication and low for voiced
    sound. Cues are combined by average; the result is bounded and finite by
    construction rather than by clipping an unbounded statistic.
    """
    samples = np.asarray(samples, np.float64)
    if (samples.ndim != 1 or not 2048 <= samples.size <= 16000000
            or not np.isfinite(samples).all()
            or type(frame_count) is not int or not 1 <= frame_count <= 65536
            or type(hop_size) is not int or not 1 <= hop_size <= 8192
            or type(frame_size) is not int or not 2 <= frame_size <= 8192):
        raise ValueError('Expected bounded finite mono samples and analysis geometry')
    window = np.hanning(frame_size)
    rows = []
    for index in range(frame_count):
        start = index * hop_size
        stop = start + frame_size
        if stop > samples.size:
            # A short final frame is zero-padded explicitly, matching the
            # profile's whole-hop tail policy rather than dropping the frame.
            chunk = np.zeros(frame_size, np.float64)
            available = max(0, samples.size - start)
            chunk[:available] = samples[start:start + available]
        else:
            chunk = samples[start:stop].copy()
        centered = chunk - chunk.mean()
        energy = float(np.sqrt(np.mean(centered ** 2)))
        if energy <= 1e-9:
            # Silence carries no aperiodicity information; report 0, not NaN.
            rows.append(0.0)
            continue
        crossings = np.count_nonzero(np.diff(np.signbit(centered))) / (frame_size - 1)
        # Random signs cross about half the time; 2x maps the plausible rate to [0, 1].
        crossing_cue = min(1.0, crossings * 2.0)
        magnitude = np.abs(np.fft.rfft(centered * window))
        flatness_cue = min(1.0, _spectral_flatness(magnitude))
        rows.append(float(np.clip(0.5 * (crossing_cue + flatness_cue), 0.0, 1.0)))
    return rows
