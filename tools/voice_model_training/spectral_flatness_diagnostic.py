"""Compare predicted vs reference spectral flatness per phone; descriptive only.

Fricatives are noise-like (high spectral flatness) while vowels are harmonic
(low flatness). A model that makes fricatives tonal shows time-domain periodicity,
which this measures without any pitch estimator or voicing label.
"""
import numpy as np


def spectral_flatness(frames, *, floor=1e-10):
    """Geometric-over-arithmetic mean magnitude per frame; no normalization."""
    frames = np.asarray(frames, np.float64)
    if frames.ndim != 2 or not 1 <= frames.shape[0] <= 65536 or not 1 <= frames.shape[1] <= 4096:
        raise ValueError('Expected a bounded two-dimensional magnitude matrix')
    if not np.isfinite(frames).all() or np.any(frames < 0):
        raise ValueError('Magnitudes must be finite and non-negative')
    shifted = np.maximum(frames, floor)
    geometric = np.exp(np.mean(np.log(shifted), axis=1))
    arithmetic = np.mean(shifted, axis=1)
    return geometric / arithmetic


def per_phone_flatness(flatness, phones, *, minimum_frames=2):
    """Mean flatness over whole phone intervals; unmeasured phones stay explicit."""
    flatness = np.asarray(flatness, np.float64)
    if (flatness.ndim != 1 or not 1 <= flatness.size <= 65536
            # A flatness ratio is mathematically within [0, 1]; allow float rounding.
            or not np.isfinite(flatness).all() or np.any(flatness < 0) or np.any(flatness > 1 + 1e-9)
            or type(minimum_frames) is not int or minimum_frames < 1
            or not isinstance(phones, list) or not phones):
        raise ValueError('Expected bounded per-frame flatness and captured phones')
    end, rows, covered = 0, [], np.zeros(flatness.size, bool)
    # Phone intervals are sample frames; flatness rows are hop frames.
    sample_limit = flatness.size * 256
    for phone in phones:
        start, stop, symbol = phone['startFrame'], phone['endFrame'], phone['symbol']
        if (type(start) is not int or type(stop) is not int or start != end
                or not start < stop <= sample_limit):
            raise ValueError('Phones must cover the frames contiguously')
        end = stop
        # Analysis frames are hop-spaced, so map sample frames to analysis rows.
        left, right = (start + 255) // 256, stop // 256
        row = dict(phone=symbol, startFrame=start, endFrame=stop,
                   analysisFrames=max(0, right - left))
        if right - left >= minimum_frames:
            covered[left:right] = True
            row.update(measurements='MEASURED', meanFlatness=float(np.mean(flatness[left:right])),
                       minimumFlatness=float(np.min(flatness[left:right])),
                       maximumFlatness=float(np.max(flatness[left:right])))
        else:
            row.update(measurements='TOO_FEW_FRAMES', meanFlatness=None,
                       minimumFlatness=None, maximumFlatness=None)
        rows.append(row)
    # Any analysis frame outside every phone is reported, never assumed silent.
    return rows, int((~covered).sum())


def compare(reference_flatness, candidate_flatness, phones, *, minimum_frames=2):
    """Report both arms per phone with an explicit signed difference."""
    left, uncovered = per_phone_flatness(reference_flatness, phones, minimum_frames=minimum_frames)
    right, candidate_uncovered = per_phone_flatness(candidate_flatness, phones, minimum_frames=minimum_frames)
    if uncovered != candidate_uncovered:
        raise ValueError('Arms must share identical phone coverage')
    rows = []
    for a, b in zip(left, right):
        if (a['phone'] != b['phone'] or a['startFrame'] != b['startFrame']
                or a['endFrame'] != b['endFrame'] or a['measurements'] != b['measurements']):
            raise ValueError('Phone rows must align exactly')
        row = dict(a, candidateFlatness=b['meanFlatness'],
                   referenceFlatness=a['meanFlatness'])
        row['candidateMinusReference'] = (None if a['meanFlatness'] is None or b['meanFlatness'] is None
                                         else b['meanFlatness'] - a['meanFlatness'])
        rows.append(row)
    return dict(formatId='com.project-seam.spectral-flatness-diagnostic', schemaVersion=1,
        rows=rows, measuredRows=sum(1 for row in rows if row['measurements'] == 'MEASURED'),
        uncoveredAnalysisFrames=uncovered,
        policy='Whole phone intervals on the captured hop grid; no frame exclusion',
        singerQualified=False, releaseEligible=False)
