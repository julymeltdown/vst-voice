"""Split per-phone mel error into binwise bias and residual structure.

Bias here is the mean residual over the phone's own frames, which is a
*post-hoc* fit on the same phone being scored. It bounds how much of the error a
per-phone constant correction could remove; it is not an available correction and
is never presented as achievable gain. Whatever remains after that bound is
structure the model got wrong frame by frame.
"""
import numpy as np


def decompose(reference, candidate, phones, *, minimum_frames=2):
    """Return per-phone bias and residual magnitudes on paired mel matrices."""
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
            residual = predicted - target
            total = float(np.abs(residual).mean())
            # Per-bin mean of the residual on this phone; a bound, not a fix.
            bias = residual.mean(axis=0)
            bias_only_error = float(np.abs(bias[None, :] - residual).mean())
            # Remaining structure the bias cannot explain.
            structured = float(np.abs(residual - bias[None, :]).mean())
            row.update(measurements='MEASURED', meanAbsoluteError=total,
                biasMagnitude=float(np.abs(bias).mean()),
                biasExplainedFraction=(1.0 - structured / total if total else None),
                structuredError=structured,
                referenceBinStd=float(target.std(axis=0).mean()),
                # |mean residual| vs mean |residual|: near 1 means a constant
                # offset dominates; near 0 means frame-level structure dominates.
                biasConcentration=(float(np.abs(bias).mean()) / total if total else None))
        else:
            row.update(measurements='TOO_FEW_FRAMES', meanAbsoluteError=None,
                biasMagnitude=None, biasExplainedFraction=None, structuredError=None,
                referenceBinStd=None, biasConcentration=None)
        rows.append(row)
    return rows, int((~covered).sum())


def error_share(reference, candidate, phones, symbols, *, minimum_frames=2):
    """Compare a phone class's share of frames with its share of total error.

    The acoustic objective weights frames by valid samples per hop, so a class
    whose error share exceeds its frame share is already receiving proportionally
    more gradient pressure, not less. That distinction decides whether frame
    weighting can explain an underfit class. Thresholds are explicit inputs.
    """
    rows, uncovered = decompose(reference, candidate, phones, minimum_frames=minimum_frames)
    if not isinstance(symbols, (tuple, list)) or not symbols:
        raise ValueError('Select at least one phone symbol')
    wanted = set(symbols)
    class_frames = class_error = total_frames = total_error = 0.0
    for row in rows:
        if row['measurements'] != 'MEASURED':
            continue
        frames = row['analysisFrames']
        error = row['meanAbsoluteError'] * frames
        total_frames += frames
        total_error += error
        if row['phone'] in wanted:
            class_frames += frames
            class_error += error
    return dict(formatId=None, symbols=sorted(wanted), classFrames=class_frames,
        totalFrames=total_frames, classError=class_error, totalError=total_error,
        frameShare=(class_frames / total_frames if total_frames else None),
        errorShare=(class_error / total_error if total_error else None),
        # > 1 means the class contributes more error than it occupies frames.
        errorToFrameShareRatio=((class_error / total_error) / (class_frames / total_frames)
                                if total_error and total_frames and class_frames else None),
        uncoveredAnalysisFrames=uncovered)
