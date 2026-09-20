"""Per-phone waveform statistics, independent of pitch-estimator voicing labels.

Callers must bind source/audio/phone-label identities. A low correlation is not
intelligibility, naturalness, or evidence that a consonant was preserved.
"""
import numpy as np


def measure(reference, candidate, phones, *, lag=256):
    reference, candidate = np.asarray(reference), np.asarray(candidate)
    if (reference.ndim != 1 or candidate.ndim != 1 or reference.shape != candidate.shape
            or not 1 <= len(reference) <= 1048576
            or not np.isfinite(reference).all() or not np.isfinite(candidate).all()
            or np.max(np.abs(reference)) > 1 or np.max(np.abs(candidate)) > 1
            or type(lag) is not int or not 1 <= lag <= 4096
            or not isinstance(phones, list) or not 1 <= len(phones) <= 4096):
        raise ValueError('Expected bounded paired normalized mono PCM and phone intervals')
    end, rows = 0, []
    for phone in phones:
        start, stop, symbol = phone['startFrame'], phone['endFrame'], phone['symbol']
        if (type(start) is not int or type(stop) is not int or start != end
                or not start < stop <= len(reference) or not isinstance(symbol,str)
                or not 1 <= len(symbol.encode()) <= 128):
            raise ValueError('Expected contiguous complete bounded phone ownership')
        end = stop
        row = dict(phone=symbol, startFrame=start, endFrame=stop)
        for role, audio in (('reference', reference), ('candidate', candidate)):
            segment = audio[start:stop].astype(np.float64)
            rms = float(np.sqrt(np.mean(segment**2)))
            peak = float(np.max(np.abs(segment)))
            dc = float(segment.mean())
            centered = (np.zeros_like(segment) if np.min(segment) == np.max(segment)
                        else segment-dc)
            values = dict(rms=rms, peak=peak, dc=dc,
                          centeredRms=float(np.sqrt(np.mean(centered**2))),
                          lagCorrelation=None, lagDifferenceRmsRatio=None, status='TOO_SHORT')
            if len(segment) >= lag*2:
                left, right = centered[:-lag], centered[lag:]
                denominator = float(np.linalg.norm(left)*np.linalg.norm(right))
                if denominator == 0:
                    values['status'] = 'NO_CENTERED_ENERGY'
                else:
                    values.update(status='MEASURED',
                        lagCorrelation=float(np.clip(left@right/denominator,-1.,1.)),
                        lagDifferenceRmsRatio=float(np.sqrt(np.mean((left-right)**2))/values['centeredRms']))
            row[role] = values
        # No infinity, favorable score, or hidden row for a muted/noisy substitute.
        row['rmsRatio'] = (row['candidate']['rms']/row['reference']['rms']
                           if row['reference']['rms'] else None)
        rows.append(row)
    if end != len(reference):
        raise ValueError('Phone ownership does not cover the complete waveform')
    return dict(lagSamples=lag, rows=rows, frameCount=len(reference),
                policy='Full phone intervals, no alignment fitting, DC removed for correlation only',
                singerQualified=False, releaseEligible=False)
