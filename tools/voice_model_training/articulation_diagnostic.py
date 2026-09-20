"""Japanese pilot phone-region diagnostics, not perceptual qualification.

Every analysis frame receives a region and an independent status for each track.
Renderer-intent labels and written MIDI are not expressive acoustic ground truth.
"""
import math
import statistics


def diagnose(notes, phones, reference, candidate, *, frame_count, window, confidence=.6):
    """Consume tracks already validated by compare_pitch_tracks and bound labels."""
    if len(reference) != len(candidate) or not notes or not phones:
        raise ValueError('Expected equal pitch grids and captured intervals')
    for intervals in (notes, phones):
        end = 0
        for interval in intervals:
            if (type(interval['startFrame']) is not int or type(interval['endFrame']) is not int
                    or interval['startFrame'] != end or not end < interval['endFrame'] <= frame_count):
                raise ValueError('Intervals must cover the source contiguously')
            end = interval['endFrame']
        if end != frame_count:
            raise ValueError('Intervals must cover the full source')
    if type(window) is not int or window <= 0 or not .32 <= confidence <= 1:
        raise ValueError('Invalid analysis policy')
    groups = {}
    note_index = phone_index = 0
    previous = -1
    for left, right in zip(reference, candidate):
        start = left['sourceFrame']
        if (type(start) is not int or not previous < start < frame_count
                or start != right['sourceFrame']):
            raise ValueError('Invalid paired analysis offsets')
        previous = start
        while notes[note_index]['endFrame'] <= start:
            note_index += 1
        while phones[phone_index]['endFrame'] <= start:
            phone_index += 1
        note, phone = notes[note_index], phones[phone_index]
        stop = start + window
        region = ('padded' if stop > frame_count else
                  'boundary' if stop > min(note['endFrame'], phone['endFrame']) else
                  'rest' if note['midi'] is None else
                  'vowelInterior' if phone['symbol'] in ('a', 'i', 'u', 'e', 'o') else 'otherPhoneInterior')
        key = (region, phone['symbol'])
        if key not in groups:
            groups[key] = dict(region=region, phone=phone['symbol'], analysisFrames=0,
                reference=dict(measurable=0, unvoiced=0, lowConfidence=0, notScored=0, errors=[]),
                candidate=dict(measurable=0, unvoiced=0, lowConfidence=0, notScored=0, errors=[]))
        group = groups[key]
        group['analysisFrames'] += 1
        for role, row in (('reference', left), ('candidate', right)):
            result = group[role]
            if region in ('padded', 'boundary', 'rest'):
                result['notScored'] += 1
            elif not row['voiced']:
                result['unvoiced'] += 1
            elif row['confidence'] < confidence:
                result['lowConfidence'] += 1
            else:
                expected = 440 * 2 ** ((note['midi'] - 69) / 12)
                result['errors'].append(1200 * math.log2(row['f0Hz'] / expected))
                result['measurable'] += 1
    for group in groups.values():
        for role in ('reference', 'candidate'):
            result = group[role]
            errors = result.pop('errors')
            result.update(medianAbsoluteCents=statistics.median(map(abs, errors)) if errors else None,
                          within50=sum(abs(error) <= 50 for error in errors),
                          meanAbsoluteCents=sum(map(abs, errors))/len(errors) if errors else None)
    return dict(groups=list(groups.values()), analysisFrames=len(reference),
        policy='Whole window inside captured phone and note; no octave correction; all coverage retained',
        scoreIsExpressivePitchGroundTruth=False, singerQualified=False)
