"""Compare predicted vs reference mel flatness per phone across captured songs.

Loads the admitted acoustic graph and captured replay inputs; never trains,
promotes or qualifies anything. Coverage, short intervals and empty classes stay
explicit rather than being folded into a favorable average.
"""
import argparse
import hashlib
import json
import statistics
from pathlib import Path

import numpy as np

from .__main__ import load_config, publish_new
from .acoustics import wav_log_mel_targets
from .acoustic_export import load_acoustic_graph
from .native_input_replay import predicted_mel
from .spectral_flatness_diagnostic import compare, spectral_flatness

UNVOICED = ('h', 'f', 'k', 's', 'sh', 't', 'ch', 'ts')
VOICED = ('a', 'i', 'u', 'e', 'o', 'N', 'm', 'n', 'r', 'w', 'j')


def analyze_song(root, song, replay, acoustic_graph, steps):
    """Return per-phone flatness rows for one captured source."""
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
    report = compare(spectral_flatness(np.exp(np.asarray(reference_mel[:frames], np.float64))),
                     spectral_flatness(np.exp(np.asarray(predicted[0, :frames], np.float64))), phones)
    return dict(sourceId=song['sourceId'], comparedFrames=frames, rows=report['rows'],
                measurable=report['measuredRows'],
                uncoveredAnalysisFrames=report['uncoveredAnalysisFrames'])


def aggregate(songs):
    """Summarize by phone class across songs; empty classes stay null."""
    result = {}
    for name, symbols in (('unvoiced', UNVOICED), ('voiced', VOICED)):
        rows = [row for song in songs for row in song['rows']
                if row['phone'] in symbols and row['measurements'] == 'MEASURED']
        deltas = [row['candidateMinusReference'] for row in rows]
        result[name] = dict(phoneWindows=len(rows),
            meanReferenceFlatness=(statistics.fmean(row['referenceFlatness'] for row in rows) if rows else None),
            meanCandidateFlatness=(statistics.fmean(row['candidateFlatness'] for row in rows) if rows else None),
            meanDelta=(statistics.fmean(deltas) if deltas else None),
            worsenedWindows=sum(1 for value in deltas if value < 0),
            improvedWindows=sum(1 for value in deltas if value > 0))
    return result


def run(*, corpus, corpus_sha256, root, replays, acoustic_export, vocoder_export, steps=10):
    """Analyze every selected song; the vocoder graph is needed by the replay."""
    from .prepare_bundle import VOCODER_FORMAT, read_report
    corpus = load_config(corpus, corpus_sha256)
    acoustic = load_acoustic_graph(acoustic_export)
    _, vocoder = read_report(vocoder_export, VOCODER_FORMAT,
                             'vocoderPath', 'vocoderSha256', 'vocoderBytes')
    songs, seen = [], set()
    replay_ids = []
    for replay in replays:
        replay_sha = hashlib.sha256(Path(replay).read_bytes()).hexdigest()
        capture = load_config(replay, replay_sha)
        replay_ids.append(dict(path=str(Path(replay).resolve()), sha256=replay_sha,
                               projectSha256=capture['projectSha256']))
        song = next((row for row in corpus['songs']
                     if row['projectSha256'] == capture['projectSha256']), None)
        if song is None or song['sourceId'] in seen:
            raise ValueError('Replay inputs must map to distinct captured sources')
        seen.add(song['sourceId'])
        songs.append(analyze_song(root, song, replay, acoustic, steps))
    acoustic_report = json.loads((Path(acoustic_export) / 'export.json').read_bytes())
    return dict(formatId='com.project-seam.predicted-flatness-diagnostic', schemaVersion=1,
        corpusSha256=corpus_sha256, steps=steps, songs=songs, aggregate=aggregate(songs),
        acousticExportSha256=hashlib.sha256(
            (Path(acoustic_export) / 'export.json').read_bytes()).hexdigest(),
        acousticSha256=acoustic_report.get('acousticSha256'),
        replays=replay_ids,
        policy='Whole phone intervals on the captured hop grid; ln-amplitude inverted before flatness',
        singerQualified=False, releaseEligible=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--corpus-sha256', required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--acoustic-export', type=Path, required=True)
    parser.add_argument('--vocoder-export', type=Path, required=True)
    parser.add_argument('--replay', type=Path, action='append', required=True)
    parser.add_argument('--steps', type=int, default=10)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.steps <= 64:
        parser.error('Steps must be between 1 and 64')
    report = run(corpus=args.corpus, corpus_sha256=args.corpus_sha256, root=args.root,
                 replays=args.replay, acoustic_export=args.acoustic_export,
                 vocoder_export=args.vocoder_export, steps=args.steps)
    publish_new(args.output, report)
    print(json.dumps(report['aggregate']))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
