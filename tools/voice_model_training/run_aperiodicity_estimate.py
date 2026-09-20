"""Report derived per-frame aperiodicity for captured sources.

Diagnostic only: derives a candidate channel from reference audio and reports it
per symbol. It does not admit the channel as supervision or train anything.
"""
import argparse
import collections
import hashlib
import json
import statistics
from pathlib import Path

from .__main__ import load_config, publish_new
from .aperiodicity import estimate
from .audio_source import decode_pcm_source

UNVOICED = ('h', 'f', 'k', 's', 'sh', 't', 'ch', 'ts')
VOICED = ('a', 'i', 'u', 'e', 'o', 'N', 'm', 'n', 'r', 'w', 'j')


def run(*, corpus, corpus_sha256, root, limit=None):
    corpus = load_config(corpus, corpus_sha256)
    selections = corpus['songs'] if limit is None else corpus['songs'][:limit]
    by_symbol, unvoiced, voiced = collections.defaultdict(list), [], []
    for song in selections:
        directory = Path(root) / song['directory']
        payload = (directory / 'source.wav').read_bytes()
        if hashlib.sha256(payload).hexdigest() != song['sourceSha256']:
            raise ValueError('Captured source changed since preparation')
        label = load_config(directory / 'label-config.json',
                            song['artifacts']['label-config.json'])['labels'][0]['label']
        _, audio = decode_pcm_source(payload, expected_sha256=song['sourceSha256'],
                                     sample_rate=48000)
        values = estimate(audio, frame_count=len(label['f0Hz']), hop_size=label['hopSize'])
        for phone in label['phonemes']:
            left, right = phone['startFrame'] // 256, phone['endFrame'] // 256
            if right - left < 2:
                continue
            mean = float(np_mean(values[left:right]))
            by_symbol[phone['symbol']].append(mean)
            if phone['symbol'] in UNVOICED:
                unvoiced.append(mean)
            elif phone['symbol'] in VOICED:
                voiced.append(mean)
    return dict(formatId='com.project-seam.aperiodicity-estimate', schemaVersion=1,
        corpusSha256=corpus_sha256, songsAnalyzed=len(selections),
        unvoicedWindows=len(unvoiced), voicedWindows=len(voiced),
        unvoicedMean=(statistics.fmean(unvoiced) if unvoiced else None),
        voicedMean=(statistics.fmean(voiced) if voiced else None),
        bySymbol={symbol: dict(windows=len(values), mean=statistics.fmean(values))
                  for symbol, values in sorted(by_symbol.items()) if len(values) >= 3},
        policy='Zero-crossing rate and spectral flatness averaged on the label hop clock',
        supervisionAdmitted=False, singerQualified=False, releaseEligible=False)


def np_mean(values):
    return sum(values) / len(values)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--corpus-sha256', required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--limit', type=int)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = run(corpus=args.corpus, corpus_sha256=args.corpus_sha256, root=args.root,
                 limit=args.limit)
    publish_new(args.output, report)
    print(json.dumps({key: report[key] for key in
                      ('songsAnalyzed', 'unvoicedMean', 'voicedMean')}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
