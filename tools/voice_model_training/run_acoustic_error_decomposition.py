"""Decompose per-phone acoustic mel error into bias and residual structure.

Diagnostic only. The per-phone bias is fit on the phone being scored, so it is an
upper bound on what a constant correction could remove, not an achievable gain,

and it is labelled that way in the receipt.
"""
import argparse
import hashlib
import json
import statistics
from pathlib import Path

from .__main__ import load_config, publish_new
from .acoustic_error_decomposition import decompose
from .acoustics import wav_log_mel_targets
from .native_input_replay import predicted_mel

UNVOICED = ('h', 'f', 'k', 's', 'sh', 't', 'ch', 'ts')
VOICED = ('a', 'i', 'u', 'e', 'o', 'N', 'm', 'n', 'r', 'w', 'j')


def run(*, corpus, corpus_sha256, root, replays, acoustic_export, steps=4):
    from .acoustic_export import load_acoustic_graph
    corpus = load_config(corpus, corpus_sha256)
    graph = load_acoustic_graph(acoustic_export)
    songs, seen = [], set()
    for replay in replays:
        capture = load_config(replay, hashlib.sha256(Path(replay).read_bytes()).hexdigest())
        song = next((row for row in corpus['songs']
                     if row['projectSha256'] == capture['projectSha256']), None)
        if song is None or song['sourceId'] in seen:
            raise ValueError('Replay inputs must map to distinct captured sources')
        seen.add(song['sourceId'])
        directory = Path(root) / song['directory']
        payload = (directory / 'source.wav').read_bytes()
        if hashlib.sha256(payload).hexdigest() != song['sourceSha256']:
            raise ValueError('Captured source changed since preparation')
        _, reference = wav_log_mel_targets(payload, expected_sha256=song['sourceSha256'],
                                           sample_rate=48000)
        predicted = predicted_mel(graph, capture, steps=steps)
        frames = min(len(reference), predicted.shape[1])
        phones = [dict(symbol=row['symbol'], startFrame=row['startFrame'], endFrame=row['endFrame'])
                  for row in load_config(directory / 'label-config.json',
                        song['artifacts']['label-config.json'])['labels'][0]['label']['phonemes']]
        rows, uncovered = decompose(reference[:frames], predicted[0, :frames], phones)
        songs.append(dict(sourceId=song['sourceId'], comparedFrames=frames, rows=rows,
                          uncoveredAnalysisFrames=uncovered))
    aggregate = {}
    for name, symbols in (('unvoiced', UNVOICED), ('voiced', VOICED)):
        selected = [row for song in songs for row in song['rows']
                    if row['phone'] in symbols and row['measurements'] == 'MEASURED']
        aggregate[name] = dict(windows=len(selected),
            meanError=(statistics.fmean(row['meanAbsoluteError'] for row in selected) if selected else None),
            meanBiasMagnitude=(statistics.fmean(row['biasMagnitude'] for row in selected) if selected else None),
            meanStructuredError=(statistics.fmean(row['structuredError'] for row in selected) if selected else None),
            meanBiasExplainedFraction=(statistics.fmean(
                row['biasExplainedFraction'] for row in selected) if selected else None),
            meanBiasConcentration=(statistics.fmean(
                row['biasConcentration'] for row in selected) if selected else None))
    return dict(formatId='com.project-seam.acoustic-error-decomposition', schemaVersion=1,
        corpusSha256=corpus_sha256, steps=steps, songs=songs, aggregate=aggregate,
        policy='Per-phone bias fit on the scored phone: an upper bound, not an achievable correction',
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
