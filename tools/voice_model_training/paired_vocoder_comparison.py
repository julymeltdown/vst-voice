"""Aggregate paired vocoder results across frozen sources; never selects a winner.

Both arms receive the identical captured mel, F0, dynamics and phone ownership.
Per-phone statistics stay separated by arm and phone; no aggregate hides a
degraded consonant, and source/phone identities remain visible.
"""
import argparse
import hashlib
import json
import statistics
from pathlib import Path

from .__main__ import load_config, publish_new
from .acoustics import wav_log_mel_targets
from .native_input_replay import prepare_inputs
from .paired_vocoder_evaluation import evaluate


# Declared policy inventory. `sh` and `j` are listed for completeness but do not
# occur in the current Japanese procedural corpus; a class with no measured
# window stays null rather than being reported as a clean result.
UNVOICED = ('h', 'f', 'k', 's', 'sh', 't', 'ch', 'ts')
VOICED = ('a', 'i', 'u', 'e', 'o', 'N', 'm', 'n', 'r', 'w', 'j')


def summarize(rows):
    """Group by phone class; a missing class or empty measurement stays null."""
    result = {}
    for label, symbols in (('unvoiced', UNVOICED), ('voiced', VOICED)):
        selected = [row for row in rows if row['phone'] in symbols]
        measured = [(row['reference']['lagCorrelation'], row['candidate']['lagCorrelation'])
                    for row in selected
                    if row['reference']['lagCorrelation'] is not None
                    and row['candidate']['lagCorrelation'] is not None]
        ratios = [row['rmsRatio'] for row in selected if row['rmsRatio'] is not None]
        result[label] = dict(phoneWindows=len(selected), measuredWindows=len(measured),
            meanReferenceLagCorrelation=(statistics.fmean(pair[0] for pair in measured) if measured else None),
            meanCandidateLagCorrelation=(statistics.fmean(pair[1] for pair in measured) if measured else None),
            meanCandidateMinusReferenceCorrelation=
                (statistics.fmean(pair[1] - pair[0] for pair in measured) if measured else None),
            meanRmsRatio=(statistics.fmean(ratios) if ratios else None))
    return result


def compare(arms, replays, corpus, executable, *, output):
    """Evaluate every arm on every frozen replay source and aggregate honestly."""
    if not isinstance(arms, dict) or not 1 <= len(arms) <= 4:
        raise ValueError('Provide 1..4 exported arms keyed by name')
    if not isinstance(replays, list) or not 1 <= len(replays) <= 32:
        raise ValueError('Provide 1..32 frozen replay inputs')
    songs = {song['projectSha256']: song for song in corpus['songs']}
    per_source, per_arm = [], {name: {'unvoiced': [], 'voiced': []} for name in arms}
    identities = {}
    for replay in replays:
        path = Path(replay['path'])
        capture = load_config(path, replay['sha256'])
        song = songs.get(capture.get('projectSha256'))
        if song is None:
            raise ValueError('Replay input does not match the captured corpus')
        directory = corpus['root'] / song['directory']
        source = directory / 'source.wav'
        payload = source.read_bytes()
        if hashlib.sha256(payload).hexdigest() != song['sourceSha256']:
            raise ValueError('Captured source changed since preparation')
        labels = load_config(directory / 'label-config.json', song['artifacts']['label-config.json'])
        _, mel = wav_log_mel_targets(payload, expected_sha256=song['sourceSha256'], sample_rate=48000)
        inputs, _, gains = prepare_inputs(capture, capture['steps'])
        report = evaluate(source, labels['labels'][0]['label']['phonemes'], arms,
            mel_input=mel[None], f0=inputs['f0'], gains=gains, executable=executable,
            valid_samples=capture['outputSampleFrames'])
        per_source.append(dict(sourceId=song['sourceId'], replaySha256=replay['sha256'],
                               comparedSamples=report['comparedSamples'],
                               excludedTailSamples=report['excludedTailSamples'],
                               arms={name: dict(pitch=arm['pitch'], waveSha256=arm['waveSha256'],
                                                phones=arm['phones'])
                                     for name, arm in report['arms'].items()}))
        for name, arm in report['arms'].items():
            previous = identities.setdefault(name, arm['vocoderSha256'])
            if previous != arm['vocoderSha256']:
                raise ValueError('An arm changed vocoder graph between sources')
            per_arm[name]['unvoiced'].extend(
                row for row in arm['phones'] if row['phone'] in UNVOICED)
            per_arm[name]['voiced'].extend(
                row for row in arm['phones'] if row['phone'] in VOICED)
    aggregate = {}
    for name, groups in per_arm.items():
        aggregate[name] = {label: summarize(rows) for label, rows in groups.items()}
    report = dict(formatId='com.project-seam.paired-vocoder-comparison', schemaVersion=1,
        armVocoderSha256=identities,
        sources=[dict(sourceId=item['sourceId'], replaySha256=item['replaySha256'])
                 for item in per_source],
        perSource=per_source, aggregate=aggregate,
        policy='Identical frozen features/controls; descriptive per-arm and per-phone only',
        singerQualified=False, releaseEligible=False)
    if output is None:
        return report
    publish_new(Path(output), report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--config-sha256', required=True)
    parser.add_argument('--pitch-executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    config = load_config(args.config, args.config_sha256)
    if (config.get('formatId') != 'com.project-seam.paired-vocoder-comparison-config'
            or config.get('releaseEligible') is not False):
        parser.error('Expected a paired comparison configuration')
    corpus = load_config(Path(config['corpusPath']), config['corpusSha256'])
    corpus = dict(corpus, root=Path(config['corpusPath']).parent)
    report = compare(config['arms'], config['replays'], corpus, args.pitch_executable,
                     output=args.output)
    print(json.dumps(report['aggregate']))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
