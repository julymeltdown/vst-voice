"""Compare predicted and reference flatness beyond per-class means.

A class mean can hide whether a model ever reaches the reference's noise-like
range at all. This reports percentiles and threshold exceedance so "never flat"
and "flat but biased" are distinguishable. Descriptive only.
"""
import numpy as np


def percentiles(values, points=(5, 25, 50, 75, 95)):
    """Report named percentiles of a bounded value array."""
    values = np.asarray(values, np.float64)
    if (values.ndim != 1 or not 1 <= values.size <= 65536
            or not np.isfinite(values).all() or not isinstance(points, (tuple, list))
            or any(type(p) not in (int, float) or not 0 <= p <= 100 for p in points)):
        raise ValueError('Expected bounded finite values and percentiles in [0, 100]')
    return {f'p{int(p) if float(p).is_integer() else p}': float(np.percentile(values, p))
            for p in points}


def exceedance(values, threshold):
    """Fraction of values strictly above a threshold."""
    values = np.asarray(values, np.float64)
    if (values.ndim != 1 or not 1 <= values.size <= 65536 or not np.isfinite(values).all()
            or type(threshold) not in (int, float) or not np.isfinite(threshold)):
        raise ValueError('Expected bounded finite values and a finite threshold')
    count = int(np.count_nonzero(values > threshold))
    return dict(threshold=float(threshold), count=count, total=int(values.size),
                fraction=count / values.size)


def compare(reference, candidate, *, thresholds=(0.1, 0.3, 0.5)):
    """Overall comparison plus explicit threshold exceedance for both arms."""
    reference, candidate = np.asarray(reference, np.float64), np.asarray(candidate, np.float64)
    if reference.shape != candidate.shape or reference.ndim != 1:
        raise ValueError('Reference and candidate flatness must share one-dimensional geometry')
    return dict(formatId='com.project-seam.flatness-distribution', schemaVersion=1,
        frameCount=int(reference.size),
        reference=dict(percentiles=percentiles(reference),
                       exceedance=[exceedance(reference, t) for t in thresholds]),
        candidate=dict(percentiles=percentiles(candidate),
                       exceedance=[exceedance(candidate, t) for t in thresholds]),
        policy='Per-frame flatness over all frames; exceedance is strict greater-than',
        singerQualified=False, releaseEligible=False)
