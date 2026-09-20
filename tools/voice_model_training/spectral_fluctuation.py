"""Frame-to-frame spectral fluctuation per phone; a noise-vs-smooth diagnostic.

Noise-like phones (fricatives) change rapidly between adjacent analysis frames;
a model that predicts a smooth conditional mean keeps that change small. Mean
absolute frame-to-frame difference is scale-dependent, so the reference and
candidate are always reported together and never compared across different
amplitude encodings.
"""
import numpy as np


def fluctuation(frames, *, normalize=True):
    """Adjacent-frame change, relative by default so scale cannot inflate it.

    Raw mean absolute difference is scale-dependent: a louder prediction looks
    more fluent or less fluent purely by gain. Normalizing each frame by its own
    mean magnitude removes overall level and leaves spectral shape change.
    """
    frames = np.asarray(frames, np.float64)
    if frames.ndim != 2 or not 2 <= frames.shape[0] <= 65536 or not 1 <= frames.shape[1] <= 4096:
        raise ValueError('Expected a bounded two-dimensional frame matrix')
    if not np.isfinite(frames).all():
        raise ValueError('Frames must be finite')
    if normalize:
        level = np.abs(frames).mean(axis=1, keepdims=True)
        frames = np.divide(frames, level, out=np.zeros_like(frames), where=level > 0)
    return np.abs(np.diff(frames, axis=0)).mean(axis=1)


def per_phone(reference, candidate, phones, *, minimum_frames=2):
    """Pair reference and candidate fluctuation over identical phone intervals."""
    reference, candidate = np.asarray(reference, np.float64), np.asarray(candidate, np.float64)
    if reference.shape != candidate.shape:
        raise ValueError('Reference and candidate fluctuation must share geometry')
    if (reference.ndim != 1 or not 1 <= reference.size <= 65536
            or not np.isfinite(reference).all() or not np.isfinite(candidate).all()
            or type(minimum_frames) is not int or minimum_frames < 1
            or not isinstance(phones, list) or not phones):
        raise ValueError('Expected bounded fluctuation arrays and captured phones')
    end, rows, covered = 0, [], np.zeros(reference.size, bool)
    limit = (reference.size + 1) * 256
    for phone in phones:
        start, stop, symbol = phone['startFrame'], phone['endFrame'], phone['symbol']
        if (type(start) is not int or type(stop) is not int or start != end
                or not start < stop <= limit):
            raise ValueError('Phones must cover the frames contiguously')
        end = stop
        left, right = max(0, start // 256), min(reference.size, stop // 256)
        if right > left:
            covered[left:right] = True
        row = dict(phone=symbol, startFrame=start, endFrame=stop, analysisFrames=right - left)
        if right - left >= minimum_frames:
            row.update(measurements='MEASURED',
                       referenceFluctuation=float(reference[left:right].mean()),
                       candidateFluctuation=float(candidate[left:right].mean()),
                       candidateMinusReference=float(candidate[left:right].mean()
                                                    - reference[left:right].mean()),
                       retainedFraction=(float(candidate[left:right].mean()
                                              / reference[left:right].mean())
                                         if reference[left:right].mean() else None))
        else:
            row.update(measurements='TOO_FEW_FRAMES', referenceFluctuation=None,
                       candidateFluctuation=None, candidateMinusReference=None,
                       retainedFraction=None)
        rows.append(row)
    return rows, int((~covered).sum())
