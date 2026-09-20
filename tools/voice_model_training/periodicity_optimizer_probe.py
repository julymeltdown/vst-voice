"""Bounded single-phrase A/B probe: does the periodicity term reduce the artifact?

Loads a verified checkpoint into memory only, runs an identical seed/steps/phrase
pair with and without the auxiliary loss, then reports raw waveform statistics.
Deliberately does not clamp or normalize generated audio; peak is reported so an
unbounded output cannot pass as a clean measurement.
"""
import hashlib
import numpy as np

from .audio_source import decode_pcm_source
from .phone_periodicity import measure


UNVOICED = ('h', 'f', 'k', 's', 'sh', 't', 'ch', 'ts')


def unvoiced_statistics(reference, candidate, phones, *, lag=256):
    """Raw clustered statistics without the normalized-amplitude precondition.

    Returns peak/rms/flags so an over-range or silent output is visible rather
    than rejected into an unmeasurable gap or silently normalized away.
    """
    reference, candidate = np.asarray(reference), np.asarray(candidate)
    length_mismatch = None
    if candidate.ndim == 1 and reference.ndim == 1 and candidate.size != reference.size:
        # A length disagreement is a real defect; report it and compare the
        # shared prefix rather than hiding it behind an exception.
        length_mismatch = dict(referenceSamples=int(reference.size),
                               candidateSamples=int(candidate.size),
                               comparedSamples=int(min(reference.size, candidate.size)))
        shorter = min(reference.size, candidate.size)
        reference, candidate = reference[:shorter], candidate[:shorter]
    if reference.ndim != 1 or not 1 <= reference.size <= 16000000:
        raise ValueError('Expected bounded mono pair')
    if not np.isfinite(candidate).all():
        # Divergence must be reported, not raised: a crashed measurement would
        # otherwise hide an unstable arm behind an exception.
        return dict(windows=0, measured=0, meanLagCorrelation=None, lengthMismatch=length_mismatch,
            candidatePeak=None, overRange=None, finite=False,
            nonFiniteSamples=int(candidate.size - np.isfinite(candidate).sum()), rows=[])
    rows, correlations = [], []
    end = 0
    for phone in phones:
        start, stop, symbol = phone['startFrame'], phone['endFrame'], phone['symbol']
        if type(start) is not int or type(stop) is not int or start != end or not start < stop <= reference.size:
            raise ValueError('Phones must cover the compared samples contiguously')
        end = stop
        if symbol not in UNVOICED:
            continue
        segment = candidate[start:stop].astype(np.float64)
        centered = segment - segment.mean()
        row = dict(phone=symbol, startFrame=start, endFrame=stop,
                   candidateRms=float(np.sqrt(np.mean(segment ** 2))),
                   candidatePeak=float(np.max(np.abs(segment))))
        if len(segment) >= lag * 2:
            left, right = centered[:-lag], centered[lag:]
            denominator = float(np.linalg.norm(left) * np.linalg.norm(right))
            row['lagCorrelation'] = (float(left @ right / denominator) if denominator else None)
        else:
            row['lagCorrelation'] = None
        if row['lagCorrelation'] is not None:
            correlations.append(row['lagCorrelation'])
        rows.append(row)
    if end != reference.size:
        raise ValueError('Phones must cover the compared samples contiguously')
    if not np.isfinite(reference).all():
        raise ValueError('Reference PCM must be finite')
    return dict(windows=len(rows), measured=len(correlations), finite=True,
        lengthMismatch=length_mismatch,
        meanLagCorrelation=(float(np.mean(correlations)) if correlations else None),
        candidatePeak=float(np.max(np.abs(candidate))),
        overRange=bool(np.max(np.abs(candidate)) > 1 or np.min(candidate) < -1),
        rows=rows)


def decode_reference(payload, expected_sha256):
    _, audio = decode_pcm_source(payload, expected_sha256=expected_sha256, sample_rate=48000)
    return audio


def unbounded_measure(source, candidate, phones):
    """Reuse the strict helper only when amplitude is in range, else raw stats."""
    peak = max(float(np.max(np.abs(source))), float(np.max(np.abs(candidate))))
    if peak <= 1:
        report = measure(np.asarray(source, np.float32), np.asarray(candidate, np.float32), phones)
        return dict(strict=True, peak=peak, rows=report['rows'])
    return dict(strict=False, peak=peak, rows=unvoiced_statistics(source, candidate, phones)['rows'])
