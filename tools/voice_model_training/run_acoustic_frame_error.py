"""Mel-domain error vs a constant-spectrum baseline, per phone, per song.

Diagnostic only: loads the admitted acoustic graph and captured replay inputs,
never trains or promotes. Uncovered frames, short intervals and empty classes
stay explicit rather than being folded into a favorable average.
"""
import argparse
import hashlib
import json
import statistics
from pathlib import Path

from .__main__ import load_config, publish_new
from .acoustic_frame_error import frame_error
from .acoustics import wav_log_mel_targets
from .native_input_replay import predicted_mel

UNVOICED = ('h', 'f', 'k', 's', 'sh', 't', 'ch', 'ts')
VOICED = ('a', 'i', 'u', 'e', 'o', 'N', 'm', 'n', 'r', 'w', 'j')


def analyze_song(root, song, replay, acoustic_graph, steps):
    """Return per-phone error rows for one captured source."""
    directory = Path(root) / song['directory']
    payload = (directory / 'source.wav').read_bytes()
    if hashlib.sha256(payload).hexdigest() != song['sourceSha256']:
        raise ValueError('Captured source changed since preparation')
    labels = load_config(directory / 'label-config.json', song['artifacts']['label-config.json'])
    capture = load_config(replay, hashlib.sha256(Path(replay).read_bytes()).hexdigest())
    _, reference_mel = wav_log_mel_targets(payload, expected_sha256=song['sourceSha256'],
                                           sample_rate=48000)
    predicted = predicted_mel(acoustic_graph, capture, steps=steps)
    frames = min(len(reference_mel), predicted.shape[1])
    phones = [dict(symbol=row['symbol'], startFrame=row['startFrame'], endFrame=row['endFrame'])
              for row in labels['labels'][0]['label']['phonemes']]
    rows, uncovered = frame_error(reference_mel[:frames], predicted[0, :frames], phones)
    return dict(sourceId=song['sourceId'], comparedFrames=frames, partition=song.get('partition'),
                rows=rows, uncoveredAnalysisFrames=uncovered)


def aggregate(songs):
    """Summarize by phone class across songs; empty classes stay null."""
    result = {}
    for name, symbols in (('unvoiced', UNVOICED), ('voiced', VOICED)):
        rows = [row for song in songs for row in song['rows']
                if row['phone'] in symbols and row['measurements'] == 'MEASURED']
        ratios = [row['modelRatioToConstant'] for row in rows if row['modelRatioToConstant'] is not None]
        result[name] = dict(phoneWindows=len(rows),
            meanModelError=(statistics.fmean(row['modelMeanAbsoluteError'] for row in rows) if rows else None),
            meanConstantError=(statistics.fmean(row['constantMeanAbsoluteError'] for row in rows) if rows else None),
            meanRatioToConstant=(statistics.fmean(ratios) if ratios else None),
            windowsNoBetterThanConstant=sum(1 for value in ratios if value >= 1.0),
            meanReferenceTemporalSpread=(statistics.fmean(
                row['referenceTemporalSpread'] for row in rows) if rows else None))
    return result


def run(*, corpus, corpus_sha256, root, replays, acoustic_export, steps=4):
    from .acoustic_export import load_acoustic_graph
    corpus = load_config(corpus, corpus_sha256)
    acoustic = load_acoustic_graph(acoustic_export)
    songs, seen = [], set()
    for replay in replays:
        capture = load_config(replay, hashlib.sha256(Path(replay).read_bytes()).hexdigest())
        song = next((row for row in corpus['songs']
                     if row['projectSha256'] == capture['projectSha256']), None)
        if song is None or song['sourceId'] in seen:
            raise ValueError('Replay inputs must map to distinct captured sources')
        seen.add(song['sourceId'])
        songs.append(analyze_song(root, song, replay, acoustic, steps))
    return dict(formatId='com.project-seam.acoustic-frame-error-diagnostic', schemaVersion=1,
        corpusSha256=corpus_sha256, steps=steps, songs=songs, aggregate=aggregate(songs),
        policy='Per-bin constant-spectrum baseline over whole phone intervals; mel is ln-amplitude',
        singerQualified=False, releaseEligible=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--corpus-sha256', required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--acoustic-export', type=Path, required=True)
    parser.add_argument('--replay', type=Path, action='append', required=True)
    parser.add_argument('--steps', type=int, default=4)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.steps <= 64:
        parser.error('Steps must be between 1 and 64')
    report = run(corpus=args.corpus, corpus_sha256=args.corpus_sha256, root=args.root,
                 replays=args.replay, acoustic_export=args.acoustic_export, steps=args.steps)
    publish_new(args.output, report)
    print(json.dumps(report['aggregate']))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
