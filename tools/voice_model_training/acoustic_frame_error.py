"""Per-phone mel error against a constant-prediction baseline.

The baseline predicts the phone's own per-bin temporal mean. If a model's error
is no better than that baseline on a phone class, the model has not represented
that class's within-phone variation at all. Descriptive: no training, no
qualification. Mel is ln-amplitude, but L1 in mel units is monotone in the
amplitude ratio and stays comparable between reference and candidate.
"""
import numpy as np


def frame_error(reference, candidate, phones, *, minimum_frames=2):
    """Compare model error to a constant-spectrum baseline over phone intervals."""
    reference = np.asarray(reference, np.float64)
    candidate = np.asarray(candidate, np.float64)
    if (reference.ndim != 2 or reference.shape != candidate.shape
            or not 1 <= reference.shape[0] <= 65536 or not 1 <= reference.shape[1] <= 4096
            or not np.isfinite(reference).all() or not np.isfinite(candidate).all()
            or type(minimum_frames) is not int or minimum_frames < 1
            or not isinstance(phones, list) or not phones):
        raise ValueError('Expected bounded paired frame matrices and captured phones')
    end, rows, covered = 0, [], np.zeros(reference.shape[0], bool)
    limit = (reference.shape[0] + 1) * 256
    for phone in phones:
        start, stop, symbol = phone['startFrame'], phone['endFrame'], phone['symbol']
        if (type(start) is not int or type(stop) is not int or start != end
                or not start < stop <= limit):
            raise ValueError('Phones must cover the frames contiguously')
        end = stop
        left, right = max(0, start // 256), min(reference.shape[0], stop // 256)
        if right > left:
            covered[left:right] = True
        row = dict(phone=symbol, startFrame=start, endFrame=stop, analysisFrames=right - left)
        if right - left >= minimum_frames:
            target, predicted = reference[left:right], candidate[left:right]
            constant = target.mean(axis=0, keepdims=True)
            model = float(np.abs(predicted - target).mean())
            baseline = float(np.abs(constant - target).mean())
            row.update(measurements='MEASURED', modelMeanAbsoluteError=model,
                       constantMeanAbsoluteError=baseline,
                       # < 1 means the model beats a single constant spectrum for
                       # this phone; >= 1 means it has collapsed to that mean.
                       modelRatioToConstant=(model / baseline if baseline else None),
                       referenceTemporalSpread=float(target.std(axis=0).mean()))
        else:
            row.update(measurements='TOO_FEW_FRAMES', modelMeanAbsoluteError=None,
                       constantMeanAbsoluteError=None, modelRatioToConstant=None,
                       referenceTemporalSpread=None)
        rows.append(row)
    return rows, int((~covered).sum())
