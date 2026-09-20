"""Add derived breathiness supervision to a captured label config as schema 4.

Produces a new configuration file; it never edits an existing one and never
admits training by itself. The channel is derived from captured reference audio
and recorded with its own provenance so label review can accept or reject it.
"""
import argparse
import hashlib
import json
import os
import stat
from pathlib import Path

from .__main__ import load_config, publish_new
from .aperiodicity import estimate
from .audio_source import decode_pcm_source

CONDITIONING_REVISION = 2


def _captured(root, relative, expected_sha256, limit=64 * 1024 * 1024):
    """Read one captured source beside the config, rejecting escapes/symlinks."""
    path = Path(root) / relative
    if path.is_symlink():
        raise ValueError('Captured source path cannot be a symlink')
    flags = os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0)
    with os.fdopen(os.open(path, flags), 'rb') as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or not 1 <= info.st_size <= limit:
            raise ValueError('Captured source must be a bounded regular file')
        payload = stream.read(info.st_size + 1)
    if len(payload) != info.st_size:
        raise ValueError('Captured source changed while reading')
    if hashlib.sha256(payload).hexdigest() != expected_sha256:
        raise ValueError('Captured source differs from its recorded digest')
    return payload


def derive(config, config_sha256, *, source_root, output):
    """Return a new schema-4 label configuration carrying breathiness."""
    value = load_config(config, config_sha256)
    if (value.get('formatId') != 'com.project-seam.voice-training-label-config'
            or value.get('schemaVersion') not in (2, 3)
            or not isinstance(value.get('labels'), list) or not value['labels']):
        raise ValueError('Expected a schema 2/3 captured label configuration without conditioning')
    root = Path(source_root)
    # Each config ships its own source list and relative paths; resolve from that,
    # never from a guessed per-source directory layout.
    inventory = {row['sourceId']: row for row in value.get('sources', [])}
    if len(inventory) != len(value['labels']):
        raise ValueError('Config must declare exactly one source per label')
    derived = []
    for item in value['labels']:
        if 'conditioning' in item:
            raise ValueError('Refuse to overwrite existing conditioning supervision')
        label = item['label']
        source_id = label['sourceId']
        row = inventory.get(source_id)
        if row is None or row.get('path') in (None, '', '.', '..') or Path(row['path']).is_absolute():
            raise ValueError('Config source inventory must be relative and complete')
        payload = _captured(root, row['path'], row['sourceSha256'])
        if hashlib.sha256(payload).hexdigest() != item['sourceSha256']:
            raise ValueError('Label source digest disagrees with the inventory')
        _, audio = decode_pcm_source(payload, expected_sha256=row['sourceSha256'],
                                     sample_rate=value['sampleRate'])
        frames = len(label['f0Hz'])
        breathiness = estimate(audio, frame_count=frames, hop_size=label['hopSize'])
        if len(breathiness) != frames:
            raise ValueError('Derived conditioning length differs from the label clock')
        derived.append(dict(item, conditioning=dict(revision=CONDITIONING_REVISION,
                                                    breathiness=breathiness)))
    # The label-config schema is closed: exactly these seven top-level fields.
    # Provenance therefore goes to a separate receipt rather than into the config,
    # which would otherwise be rejected by the admission validator.
    report = dict(value, schemaVersion=4, labels=derived)
    publish_new(output, report)
    return report


def provenance(config_sha256, output):
    """Describe where the derived channel came from, for label review."""
    publish_new(output.with_suffix('.provenance.json'), dict(
        formatId='com.project-seam.conditioning-supervision-provenance', schemaVersion=1,
        derivedFromConfigSha256=config_sha256, revision=CONDITIONING_REVISION,
        controls=['breathiness'], estimator='zero-crossing-and-spectral-flatness-v1',
        estimatorSource='captured-reference-audio', reviewRequired=True,
        supervisionAdmitted=False, singerQualified=False, releaseEligible=False))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--config-sha256', required=True)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = derive(args.config, args.config_sha256, source_root=args.source_root,
                    output=args.output)
    provenance(args.config_sha256, args.output)
    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    print(json.dumps(dict(output=str(args.output), sha256=digest,
                          labels=len(report['labels']))))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
