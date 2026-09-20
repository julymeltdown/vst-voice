"""Evaluate every frozen reference without assigning a training partition."""
import argparse
import json
from pathlib import Path

from .__main__ import load_config
from .phrase_fingerprint import fingerprint, project_events
from .pitch_comparison import _capture
from .render_frozen_evaluation import validate_plan
from .validation_campaign import execute_campaign


def prepare(plan, plan_sha256, history, capture, capture_sha256, preparation, preparation_sha256):
    frozen = load_config(plan, plan_sha256)
    historical = load_config(history, frozen['historicalIndexSha256'])
    items = validate_plan(frozen, historical)
    reference = load_config(capture, capture_sha256)
    prepared = load_config(preparation, preparation_sha256)
    if (reference.get('formatId') != 'com.project-seam.frozen-evaluation-capture'
            or reference.get('schemaVersion') != 1 or reference.get('state') != 'CAPTURED'
            or reference.get('planSha256') != plan_sha256 or reference.get('pilotUnchanged') is not True
            or prepared.get('formatId') != 'com.project-seam.frozen-evaluation-preparation'
            or prepared.get('schemaVersion') != 1 or prepared.get('captureSha256') != capture_sha256
            or reference.get('trainingAdmitted') is not False
            or prepared.get('trainingAdmitted') is not False):
        raise ValueError('Frozen reference/preparation binding mismatch')
    ids = [item['sourceId'] for item in items]
    if ([row['sourceId'] for row in reference['items']] != ids
            or [row['sourceId'] for row in prepared['items']] != ids):
        raise ValueError('Evaluation must retain the complete frozen cohort in order')
    captured = []
    for item, ref, pre in zip(items, reference['items'], prepared['items']):
        if ref['state'] != 'CAPTURED' or pre['state'] != 'PREPARED_UNAPPROVED':
            raise ValueError('Incomplete frozen reference preparation')
        root = preparation.parent / item['sourceId']
        if root.is_symlink():
            raise ValueError('Prepared source directory must not be a symlink')
        receipt = load_config(root / 'preparation.json', pre['preparationSha256'])
        if (receipt['sourceId'] != item['sourceId'] or receipt['trainingAdmitted'] is not False
                or any(receipt[key] != ref[key] for key in
                       ('projectSha256', 'recipeSha256', 'sourceSha256', 'exportReceiptSha256'))
                or receipt['recipeSha256'] != frozen['voiceRecipeSha256']):
            raise ValueError('Prepared source differs from frozen capture')
        source_bytes, source_hash = _capture(root / 'source.wav', 8 * 1024 * 1024)
        project_bytes, project_hash = _capture(Path(ref['exportRoot']) / 'project.seam', 4 * 1024 * 1024)
        if source_hash != receipt['sourceSha256'] or project_hash != ref['projectSha256']:
            raise ValueError('Frozen source/project changed')
        project = json.loads(project_bytes)
        if fingerprint(project_events(project), ppq=project['ppq']) != item['fingerprints']:
            raise ValueError('Captured score differs from frozen phrase')
        captured.append((dict(sourceId=item['sourceId'], sourceSha256=source_hash,
                              projectSha256=project_hash), source_bytes, project_bytes))
    return dict(selectionSha256=plan_sha256, corpusSha256=preparation_sha256,
                frozenCaptureSha256=capture_sha256), captured, frozen


def run(*, plan, plan_sha256, history, capture, capture_sha256, preparation,
        preparation_sha256, bundle, renderer, pitch_executable, output,
        vocoder_export=None, vocoder_checkpoint=None):
    binding, captured, frozen = prepare(plan, plan_sha256, history, capture, capture_sha256,
                                        preparation, preparation_sha256)
    # This first campaign is the predeclared baseline, not a post-selection model.
    if _capture(bundle / 'manifest.json', 1048576)[1] != frozen['baselineManifestSha256']:
        raise ValueError('Candidate differs from the frozen baseline manifest')
    return execute_campaign(binding, captured, bundle, renderer, pitch_executable, output,
        vocoder_export=vocoder_export, vocoder_checkpoint=vocoder_checkpoint,
        scope='frozen-same-voice-unseen-phrase-engineering-only')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('plan', 'history', 'capture', 'preparation', 'bundle', 'renderer',
                 'pitch-executable', 'output', 'vocoder-export', 'vocoder-checkpoint'):
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ('plan-sha256', 'capture-sha256', 'preparation-sha256'):
        parser.add_argument('--' + name, required=True)
    report = run(**vars(parser.parse_args()))
    print(json.dumps(dict(selectedCount=report['selectedCount'], executionPassed=report['executionPassed'])))
    return 0 if report['executionPassed'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
