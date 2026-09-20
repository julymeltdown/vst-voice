"""Locate measured pitch errors against a captured score and inspect exact rests.

Written-note pitch is a diagnostic, not ground truth for expressive pitch or
unvoiced consonants. No frame is removed from the original strict comparison.
"""
import argparse
import json
import math

import numpy as np
from pathlib import Path

from .__main__ import publish_new
from .compare_application_export import decode_master
from .labels import score_report
from .pitch_comparison import _capture, compare_pitch_tracks


def locate_errors(notes, reference, candidate, errors, *, frame_count, window):
    if not (len(reference) == len(candidate) == len(errors)):
        raise ValueError('Unequal pitch diagnostic grids')
    rows = []
    note_index = 0
    counts = dict(mismatches=0, noteInterior=0, transition=0, rest=0, padded=0,
                  referenceWithin50=0, candidateWithin50=0, bothOutside50=0)
    for source, rendered, error in zip(reference, candidate, errors):
        start = source['sourceFrame']
        if start != rendered['sourceFrame']:
            raise ValueError('Unequal pitch diagnostic offsets')
        while note_index + 1 < len(notes) and notes[note_index]['endFrame'] <= start:
            note_index += 1
        if error is None or abs(error) <= 50:
            continue
        counts['mismatches'] += 1
        note = notes[note_index]
        row = dict(sourceFrame=start, referenceCandidateCents=error)
        if start + window > frame_count:
            kind = 'padded'
        elif start + window > note['endFrame']:
            kind = 'transition'
        elif note['midi'] is None:
            kind = 'rest'
        else:
            kind = 'noteInterior'
            expected = 440 * 2 ** ((note['midi'] - 69) / 12)
            source_error = 1200 * math.log2(source['f0Hz'] / expected)
            candidate_error = 1200 * math.log2(rendered['f0Hz'] / expected)
            row.update(midi=note['midi'], referenceScoreCents=source_error,
                       candidateScoreCents=candidate_error)
            counts['referenceWithin50'] += abs(source_error) <= 50
            counts['candidateWithin50'] += abs(candidate_error) <= 50
            counts['bothOutside50'] += abs(source_error) > 50 and abs(candidate_error) > 50
        counts[kind] += 1
        rows.append(dict(row, location=kind))
    return dict(counts=counts, mismatchingFrames=rows,
        policy='Whole analysis window inside note; no octave correction or metric exclusions',
        scoreIsExpressivePitchGroundTruth=False)


def measure_rests(notes, channels):
    rests = []
    for note in notes:
        if note['midi'] is not None:
            continue
        start, end = note['startFrame'], note['endFrame']
        if not 0 <= start < end <= len(channels):
            raise ValueError('Rest outside captured master')
        segment = channels[start:end]
        rests.append(dict(startFrame=start, endFrame=end,
            channelNonzeroSamples=np.count_nonzero(segment, axis=0).tolist(),
            channelPeak=np.max(np.abs(segment), axis=0).tolist()))
    return dict(restCount=len(rests), rests=rests,
        allRestsExactlyZero=all(not any(row['channelNonzeroSamples']) for row in rests)
                            if rests else None)


def inspect(comparison, comparison_sha256, labels, labels_sha256, master):
    comparison_bytes, digest = _capture(comparison, 8 * 1024 * 1024)
    label_bytes, label_digest = _capture(labels, 8 * 1024 * 1024)
    if digest != comparison_sha256 or label_digest != labels_sha256:
        raise ValueError('Comparison/label receipt SHA-256 mismatch')
    captured = json.loads(comparison_bytes)
    label_config = json.loads(label_bytes)
    if (label_config.get('formatId') != 'com.project-seam.voice-training-label-config'
            or label_config.get('schemaVersion') != 3):
        raise ValueError('Expected captured explicit-silence score configuration')
    entries = label_config['labels']
    if len(entries) != 1:
        raise ValueError('Expected exactly one captured label source')
    entry = entries[0]
    if (captured.get('formatId') != 'com.project-seam.application-audio-comparison'
            or captured.get('schemaVersion') != 1
            or captured['referenceSha256'] != entry['sourceSha256']):
        raise ValueError('Score/source identity mismatch')
    payload, master_digest = _capture(master, 64 * 1024 * 1024)
    identity, channels = decode_master(payload)
    if master_digest != captured['candidateSha256'] or identity != captured['candidate']:
        raise ValueError('Master differs from measured candidate')
    label = entry['label']
    if label['frameCount'] != identity['frameCount'] or label_config['sampleRate'] != identity['sampleRate']:
        raise ValueError('Score/master clocks differ')
    score_report(entry['score'], frame_count=identity['frameCount'],
                 phoneme_count=len(label['phonemes']), explicit_silence=True)
    pitch = captured['pitch']
    # Revalidate both full grids and recompute, rather than trust stored errors.
    strict = compare_pitch_tracks(pitch['referenceTrack'], pitch['candidateTrack'],
        reference_sha256=pitch['reference']['sourceSha256'],
        candidate_sha256=pitch['candidate']['sourceSha256'], sample_rate=identity['sampleRate'],
        frame_count=identity['frameCount'])
    notes = entry['score']['notes']
    return dict(formatId='com.project-seam.score-export-diagnostic', schemaVersion=1,
        comparisonSha256=digest, labelsSha256=label_digest, masterSha256=master_digest,
        sourceSha256=entry['sourceSha256'], strictPitchStatus=strict['status'],
        strictPitchSummary={key: strict[key] for key in ('measurableVoicedPairs',
            'withinToleranceFrames', 'unmeasurableFrames', 'voicingMismatchFrames')},
        errorLocations=locate_errors(notes, pitch['referenceTrack']['pitchFrames'],
            pitch['candidateTrack']['pitchFrames'], strict['frameErrorsCents'],
            frame_count=identity['frameCount'], window=strict['windowFrames']),
        silence=measure_rests(notes, channels), singerQualified=False, releaseEligible=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('comparison', 'labels', 'master', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ('comparison-sha256', 'labels-sha256'):
        parser.add_argument('--' + name, required=True)
    args = vars(parser.parse_args())
    output = args.pop('output')
    if output.exists() or output.is_symlink():
        parser.error('Output must be a new report')
    result = inspect(**args)
    publish_new(output, result)
    print(json.dumps(dict(counts=result['errorLocations']['counts'], silence=result['silence'],
                          singerQualified=False)))


if __name__ == '__main__':
    main()
