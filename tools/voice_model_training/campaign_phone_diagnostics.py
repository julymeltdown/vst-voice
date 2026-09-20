"""Per-phone waveform diagnostics over a validation campaign's rendered audio.

Measures lag-256 periodicity and energy per labeled phone on each PASSED song's
export against its captured source, then aggregates the unvoiced and voiced
classes separately. This is a descriptive fricative-cohort diagnostic: a low
correlation is not intelligibility, naturalness, or qualification, and the
receipt never promotes a model.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np

from .__main__ import load_config, publish_new
from .phone_periodicity import measure as measure_phones
from .paired_vocoder_comparison import summarize, UNVOICED, VOICED
from .paired_vocoder_evaluation import clip_phones
from .pitch_comparison import _capture


def load_wave(path, maximum_bytes=64 * 1024 * 1024):
    """Decode a campaign WAV to normalized mono float64 without resampling."""
    payload, digest = _capture(Path(path), maximum_bytes)
    from scipy.io import wavfile
    import io
    rate, data = wavfile.read(io.BytesIO(payload))
    if rate != 48000 or data.size == 0:
        raise ValueError('Expected nonempty 48 kHz campaign audio')
    if np.issubdtype(data.dtype, np.integer):
        data = data.astype(np.float64) / (1 << (8 * data.dtype.itemsize - 1))
    else:
        data = data.astype(np.float64)
    if data.ndim == 2:
        data = data.mean(axis=1)
    if not np.isfinite(data).all() or np.max(np.abs(data)) > 1:
        raise ValueError('Campaign audio is nonfinite or unnormalized')
    return data, digest


def diagnose(campaign_path, campaign_sha256, corpus_path, corpus_sha256, *, output=None):
    """Measure every PASSED campaign song against its corpus labels."""
    campaign_path, corpus_path = Path(campaign_path), Path(corpus_path)
    if campaign_path.parent.is_symlink() or corpus_path.parent.is_symlink():
        raise ValueError('Campaign and corpus directories cannot be symlinks')
    campaign = load_config(campaign_path, campaign_sha256)
    if (campaign.get('formatId') != 'com.project-seam.validation-campaign'
            or type(campaign.get('schemaVersion')) is not int or campaign['schemaVersion'] != 1
            or campaign.get('singerQualified') is not False
            or campaign.get('releaseEligible') is not False):
        raise ValueError('Expected an unqualified validation campaign')
    corpus = load_config(corpus_path, corpus_sha256)
    root = corpus_path.parent
    songs = {song['projectSha256']: song for song in corpus['songs']}
    per_song, unvoiced, voiced = [], [], []
    for index, item in enumerate(campaign.get('items') or []):
        if item.get('execution') != 'PASSED':
            per_song.append(dict(directory=item.get('directory'), execution=item.get('execution'),
                                 measured=False))
            continue
        song = songs.get(item.get('projectSha256'))
        if song is None:
            raise ValueError('Campaign item does not match the captured corpus')
        directory = root / song['directory']
        labels = load_config(directory / 'label-config.json', song['artifacts']['label-config.json'])
        phones = [dict(symbol=row['symbol'], startFrame=row['startFrame'], endFrame=row['endFrame'])
                  for row in labels['labels'][0]['label']['phonemes']]
        source, source_hash = load_wave(directory / 'source.wav')
        if source_hash != song['sourceSha256']:
            raise ValueError('Captured source changed since preparation')
        export_dir = campaign_path.parent / item['directory'] / 'export'
        if export_dir.is_symlink():
            raise ValueError('Export directory cannot be a symlink')
        candidate, _ = load_wave(export_dir / 'master.wav')
        valid = min(len(source), len(candidate))
        rows = measure_phones(source[:valid], candidate[:valid], clip_phones(phones, valid))['rows']
        summary = summarize(rows)
        unvoiced.extend(row for row in rows if row['phone'] in UNVOICED)
        voiced.extend(row for row in rows if row['phone'] in VOICED)
        per_song.append(dict(directory=item['directory'], sourceId=song['sourceId'],
                             execution='PASSED', measured=True, comparedSamples=valid,
                             excludedTailSamples=len(candidate) - valid,
                             unvoiced=summary['unvoiced'], voiced=summary['voiced'], rows=rows))
    report = dict(formatId='com.project-seam.campaign-phone-diagnostics', schemaVersion=1,
                  campaignSha256=campaign_sha256, corpusSha256=corpus_sha256,
                  songs=per_song,
                  aggregate=dict(unvoiced=summarize(unvoiced)['unvoiced'],
                                 voiced=summarize(voiced)['voiced']),
                  policy='Per-phone lag-256 periodicity and energy on rendered exports; descriptive only',
                  singerQualified=False, releaseEligible=False)
    if output is not None:
        publish_new(Path(output), report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--campaign', type=Path, required=True)
    parser.add_argument('--campaign-sha256', required=True)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--corpus-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = diagnose(args.campaign, args.campaign_sha256, args.corpus, args.corpus_sha256,
                      output=args.output)
    print(json.dumps(report['aggregate']))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
