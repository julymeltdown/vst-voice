"""Rerun an explicitly frozen validation selection through the real application.

Execution success is separate from pitch quality. Every selected song remains
in the result, including failures; this command cannot qualify a singer.
"""
import argparse
import hashlib
import json
from pathlib import Path

from tools.neural_runtime.check_production_render import run_render
from .__main__ import publish_new
from .compare_application_export import measure
from .pitch_comparison import _capture


def capture_json(path, limit=8 * 1024 * 1024):
    payload, digest = _capture(path, limit)
    return json.loads(payload), digest


def prepare_selection(selection, corpus, expected_sha256):
    config, digest = capture_json(selection)
    if digest != expected_sha256:
        raise ValueError('Selection SHA-256 mismatch')
    inventory, corpus_digest = capture_json(corpus)
    if (config.get('formatId') != 'com.project-seam.validation-selection'
            or config.get('schemaVersion') != 1
            or config.get('corpusSha256') != corpus_digest
            or inventory.get('formatId') != 'com.project-seam.captured-teacher-corpus'
            or inventory.get('schemaVersion') != 1):
        raise ValueError('Selection/corpus identity mismatch')
    items = config.get('items', [])
    if not isinstance(items, list) or not 1 <= len(items) <= 16:
        raise ValueError('Select 1..16 explicit validation songs')
    songs = inventory['songs']
    captured, seen = [], set()
    for item in items:
        identity = item['sourceId']
        matches = [song for song in songs if song['sourceId'] == identity]
        partitions = [group['partition'] for group in inventory['split']['groups']
                      if identity in group['sourceIds']]
        if (identity in seen or len(matches) != 1 or partitions != ['validation']
                or identity in inventory.get('heldOutSongIds', [])):
            raise ValueError('Duplicate, unknown or non-validation song: ' + identity)
        seen.add(identity)
        song = matches[0]
        if song['partition'] != 'validation':
            raise ValueError('Song partition disagrees with split')
        relative = Path(song['directory'])
        if relative.is_absolute() or len(relative.parts) != 1 or relative.name in ('.', '..'):
            raise ValueError('Invalid captured song directory')
        source = corpus.parent / relative / 'source.wav'
        if source.parent.is_symlink():
            raise ValueError('Source directory cannot be a symlink')
        source_bytes, source_digest = _capture(source, 8 * 1024 * 1024)
        project_bytes, project_digest = _capture(Path(item['projectPath']), 4 * 1024 * 1024)
        if source_digest != song['sourceSha256'] or project_digest != song['projectSha256']:
            raise ValueError('Captured source/project SHA-256 mismatch: ' + identity)
        captured.append((dict(sourceId=identity, sourceSha256=source_digest,
                              projectSha256=project_digest), source_bytes, project_bytes))
    return dict(selectionSha256=digest, corpusSha256=corpus_digest), captured


def run_campaign(selection, selection_sha256, corpus, bundle, renderer, pitch_executable,
                 output, *, silence_phone='pau'):
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError('Output must be new with an existing parent')
    binding, captured = prepare_selection(selection, corpus, selection_sha256)
    resource, resource_digest = capture_json(bundle / 'resource.json', 16384)
    _, manifest_digest = capture_json(bundle / 'manifest.json', 1048576)
    if (resource.get('formatId') != 'com.project-seam.neural-resource'
            or resource.get('schemaVersion') != 1 or resource.get('contentHash') != manifest_digest):
        raise ValueError('Candidate resource identity mismatch')
    binary_hashes = {str(path): _capture(path, 256 * 1024 * 1024)[1]
                     for path in (renderer, pitch_executable)}
    output.mkdir()
    publish_new(output / 'selection.json', dict(binding, items=[row[0] for row in captured],
        manifestSha256=manifest_digest, resourceSha256=resource_digest,
        binarySha256=binary_hashes, silencePhone=silence_phone))
    selection_receipt_hash = _capture(output / 'selection.json', 1048576)[1]
    results = []
    for index, (identity, source_bytes, project_bytes) in enumerate(captured):
        directory = output / ('song-%03d' % index)
        directory.mkdir()
        source, project = directory / 'source.wav', directory / 'input.seam'
        with source.open('xb') as stream:
            stream.write(source_bytes)
        with project.open('xb') as stream:
            stream.write(project_bytes)
        row = dict(identity, directory=directory.name, execution='FAILED')
        try:
            for path, expected in binary_hashes.items():
                if _capture(Path(path), 256 * 1024 * 1024)[1] != expected:
                    raise ValueError('Executable changed during campaign')
            if capture_json(bundle / 'resource.json', 16384)[1] != resource_digest:
                raise ValueError('Candidate resource changed during campaign')
            run_render(renderer, bundle, manifest_digest, 256 * 1024 * 1024,
                project=project, output=directory / 'export', model_id=resource['id'],
                version=resource['version'], silence_phone=silence_phone)
            comparison = measure(source, directory / 'export' / 'master.wav', executable=pitch_executable)
            if comparison['referenceSha256'] != identity['sourceSha256']:
                raise ValueError('Captured reference changed during measurement')
            for path, expected in binary_hashes.items():
                if _capture(Path(path), 256 * 1024 * 1024)[1] != expected:
                    raise ValueError('Executable changed during measurement')
            publish_new(directory / 'comparison.json', comparison)
            row.update(execution='PASSED', pitchStatus=comparison['pitch']['comparison']['status'],
                comparisonSha256=_capture(directory / 'comparison.json', 8 * 1024 * 1024)[1])
        except Exception as error:
            row.update(errorType=type(error).__name__, error=str(error)[:8192])
        publish_new(directory / 'result.json', row)
        results.append(row)
    report = dict(formatId='com.project-seam.validation-campaign', schemaVersion=1,
        **binding, items=results, selectedCount=len(results),
        selectionReceiptSha256=selection_receipt_hash,
        executionPassed=all(row['execution'] == 'PASSED' for row in results),
        singerQualified=False, releaseEligible=False,
        qualityPolicy='Per-song strict diagnostics; no failed-item exclusion or qualification')
    publish_new(output / 'campaign.json', report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('selection', 'corpus', 'bundle', 'renderer', 'pitch-executable', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--selection-sha256', required=True)
    parser.add_argument('--silence-phone', choices=('pau', 'SP', 'sil'), default='pau')
    args = parser.parse_args()
    report = run_campaign(**vars(args))
    print(json.dumps({key: report[key] for key in
                     ('selectedCount', 'executionPassed', 'singerQualified', 'releaseEligible')}))
    raise SystemExit(0 if report['executionPassed'] else 2)


if __name__ == '__main__':
    main()
