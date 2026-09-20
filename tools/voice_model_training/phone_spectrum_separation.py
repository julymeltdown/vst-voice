"""Measure whether distinct phone types produce distinct predicted spectra.

All unvoiced phones share an f0 of zero under the base conditioning, so the only
signal distinguishing /s/ from /t/ is the phone token. If predicted mean spectra
for different unvoiced phones are far more similar to each other than the
reference spectra are, the model has collapsed them into one average spectrum.
Descriptive only: no training, no qualification.
"""
import numpy as np


def _mean_spectra(matrix, phones, symbols):
    """Per-symbol mean spectrum over all its measured frames."""
    collected = {}
    for phone in phones:
        if phone['symbol'] not in symbols:
            continue
        left, right = max(0, phone['startFrame'] // 256), min(matrix.shape[0], phone['endFrame'] // 256)
        if right - left < 2:
            continue
        collected.setdefault(phone['symbol'], []).append(matrix[left:right])
    return {symbol: np.concatenate(parts).mean(axis=0) for symbol, parts in collected.items()}


def _pairwise_distance(spectra):
    """Mean L1 distance between every pair of distinct symbol spectra."""
    symbols = sorted(spectra)
    values = []
    for index, left in enumerate(symbols):
        for right in symbols[index + 1:]:
            values.append(float(np.abs(spectra[left] - spectra[right]).mean()))
    return (float(np.mean(values)) if values else None), len(values), symbols


def separation(reference, candidate, phones, symbols, *, minimum_frames=2):
    """Compare reference vs predicted between-symbol separation."""
    reference = np.asarray(reference, np.float64)
    candidate = np.asarray(candidate, np.float64)
    if (reference.ndim != 2 or reference.shape != candidate.shape
            or not 1 <= reference.shape[0] <= 65536 or not 1 <= reference.shape[1] <= 4096
            or not np.isfinite(reference).all() or not np.isfinite(candidate).all()
            or type(minimum_frames) is not int or minimum_frames < 1
            or not isinstance(phones, list) or len(phones) < 2
            or not isinstance(symbols, (tuple, list)) or len(set(symbols)) < 2):
        raise ValueError('Expected paired frames, contiguous phones and >=2 distinct symbols')
    end = 0
    for phone in phones:
        if (type(phone['startFrame']) is not int or type(phone['endFrame']) is not int
                or phone['startFrame'] != end or not end < phone['endFrame']):
            raise ValueError('Phones must cover the frames contiguously')
        end = phone['endFrame']
    reference_spectra = _mean_spectra(reference, phones, set(symbols))
    candidate_spectra = _mean_spectra(candidate, phones, set(symbols))
    if sorted(reference_spectra) != sorted(candidate_spectra):
        raise ValueError('Both matrices must contain the same measured symbols')
    reference_distance, pairs, used = _pairwise_distance(reference_spectra)
    candidate_distance, _, _ = _pairwise_distance(candidate_spectra)
    # Within-symbol spread sets the scale a distance has to beat to matter.
    reference_spread = float(np.mean([part.std(axis=0).mean()
        for parts in _collect(reference, phones, set(symbols)).values()
        for part in [np.concatenate(parts)]])) if reference_spectra else None
    return dict(formatId='com.project-seam.phone-spectrum-separation', schemaVersion=1,
        symbols=used, comparedPairs=pairs, referenceMeanDistance=reference_distance,
        candidateMeanDistance=candidate_distance,
        # < 1 means the model separates phone types less than the data does.
        separationRatio=(candidate_distance / reference_distance
                         if reference_distance else None),
        referenceWithinSymbolSpread=reference_spread,
        policy='Per-symbol mean spectrum over whole measured phones; L1 distance in ln-amplitude mel',
        singerQualified=False, releaseEligible=False)


def _collect(matrix, phones, symbols):
    collected = {}
    for phone in phones:
        if phone['symbol'] not in symbols:
            continue
        left, right = max(0, phone['startFrame'] // 256), min(matrix.shape[0], phone['endFrame'] // 256)
        if right - left < 2:
            continue
        collected.setdefault(phone['symbol'], []).append(matrix[left:right])
    return collected
