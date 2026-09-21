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


_MODEL_LEAF_SPEC = {
    'vocoder': dict(format='com.project-seam.vocoder-export', graph_field='vocoderPath',
                    sha_field='vocoderSha256', bytes_field='vocoderBytes',
                    coverage_field='sourceUpdates'),
    'acoustic': dict(format='com.project-seam.acoustic-export', graph_field='acousticPath',
                     sha_field='acousticSha256', bytes_field='acousticBytes',
                     coverage_field='coveredSourceFrames'),
}


def _bind_model_leaf(manifest, export, checkpoint, role):
    """Bind one deployed graph asset to its claimed complete checkpoint epoch."""
    spec = _MODEL_LEAF_SPEC[role]
    from .prepare_bundle import read_report
    exported, _ = read_report(export, spec['format'], spec['graph_field'],
                              spec['sha_field'], spec['bytes_field'])
    assets = [asset for asset in manifest['assets'] if asset['role'] == role]
    receipt, digest = capture_json(checkpoint)
    if (len(assets) != 1 or assets[0]['sha256'] != exported[spec['sha_field']]
            or digest != exported['checkpointReceiptSha256']
            or receipt['epoch']['epochComplete'] is not True
            or receipt['epoch']['coverageVerified'] is not True):
        raise ValueError(role + ' training receipt does not bind the complete candidate epoch')
    # The vocoder export records its dataset digest; the acoustic export does not,
    # so the acoustic leaf binds on receipt identity alone.
    if exported.get('datasetSha256') is not None and \
            receipt['epoch'].get('datasetSha256') != exported['datasetSha256']:
        raise ValueError(role + ' training receipt does not bind the complete candidate epoch')
    coverage = receipt['epoch'].get(spec['coverage_field'])
    if not isinstance(coverage, dict) or not coverage or any(
            type(count) is not int or count <= 0 for count in coverage.values()):
        raise ValueError('Invalid ' + role + ' training coverage')
    return exported, receipt, digest, coverage


def audit_model_training(items, manifest, export, checkpoint, role):
    """Positive training overlap is decisive; absence does not prove holdout.

Receipts describe one epoch, not necessarily all pretraining or resume ancestry.
Source-ID matching is conservative and cannot prove independent source audio.
"""
    if export is None and checkpoint is None:
        return dict(status='NOT_AUDITED', combinedModelHoldoutVerified=False)
    if export is None or checkpoint is None:
        raise ValueError(role.capitalize() + ' export and checkpoint receipt must be supplied together')
    exported, receipt, digest, coverage = _bind_model_leaf(manifest, export, checkpoint, role)
    overlap = [item['sourceId'] for item in items if item['sourceId'] in coverage]
    result = dict(status='TRAINING_SOURCE_ID_OVERLAP' if overlap else 'NO_SOURCE_ID_OVERLAP_IN_THIS_EPOCH',
        checkpointReceiptSha256=digest, trainingSourceIdOverlap=overlap,
        combinedModelHoldoutVerified=False,
        limitation='No absence or independent-audio claim across pretraining/resume ancestry')
    if exported.get('datasetSha256') is not None:
        result['datasetSha256'] = exported['datasetSha256']
    result[_MODEL_LEAF_SPEC[role]['sha_field']] = exported[_MODEL_LEAF_SPEC[role]['sha_field']]
    return result


def audit_vocoder_training(items, manifest, export, checkpoint):
    return audit_model_training(items, manifest, export, checkpoint, 'vocoder')


def audit_combined_training(items, manifest, vocoder_export, vocoder_checkpoint,
                            acoustic_export, acoustic_checkpoint, ancestry_audit):
    """Audit both deployed models and, when supplied, their declared ancestry.

The leaf checks bind each deployed graph to its claimed complete epoch. The
optional ancestry audit widens the overlap question from the leaf epoch to every
declared training source across both receipt chains. It is a receipt-level audit:
recipe equivalence and undeclared pretraining remain unprovable, so the combined
holdout is never reported verified by this command.
"""
    result = dict(vocoder=audit_model_training(items, manifest, vocoder_export,
                                             vocoder_checkpoint, 'vocoder'),
                  acoustic=audit_model_training(items, manifest, acoustic_export,
                                                acoustic_checkpoint, 'acoustic'),
                  combinedModelHoldoutVerified=False)
    if ancestry_audit is None:
        result['ancestry'] = dict(status='NOT_AUDITED')
        return result
    if vocoder_checkpoint is None or acoustic_checkpoint is None:
        raise ValueError('Ancestry audit requires both deployed checkpoint receipts')
    report, digest = capture_json(ancestry_audit)
    if (report.get('formatId') != 'com.project-seam.training-ancestry-audit'
            or report.get('schemaVersion') != 1):
        raise ValueError('Ancestry audit is not a training-ancestry-audit receipt')
    vocoder_digest = result['vocoder']['checkpointReceiptSha256']
    acoustic_digest = result['acoustic']['checkpointReceiptSha256']
    verdicts = {}
    for role, leaf in (('vocoder', vocoder_digest), ('acoustic', acoustic_digest)):
        section = report.get(role)
        if (not isinstance(section, dict)
                or section.get('declaredReceiptChainComplete') is not True
                or not isinstance(section.get('receiptSha256s'), list)
                or not section['receiptSha256s'] or section['receiptSha256s'][0] != leaf):
            raise ValueError('Ancestry audit does not bind the deployed ' + role + ' leaf')
        candidates = section.get('candidates')
        if not isinstance(candidates, list):
            raise ValueError('Ancestry audit has no ' + role + ' candidate verdicts')
        verdicts[role] = {row['sourceId']: row.get('status') for row in candidates
                          if isinstance(row, dict) and isinstance(row.get('sourceId'), str)}
    missing = [item['sourceId'] for item in items
               if item['sourceId'] not in verdicts['vocoder']
               or item['sourceId'] not in verdicts['acoustic']]
    if missing:
        raise ValueError('Ancestry audit does not cover every selected source: ' + ', '.join(missing))
    overlap = {}
    for role in ('vocoder', 'acoustic'):
        overlap[role] = [item['sourceId'] for item in items
                         if verdicts[role][item['sourceId']] == 'TRAINING_OVERLAP']
    result['ancestry'] = dict(status='DECLARED_ANCESTRY_AUDITED',
        auditSha256=digest,
        vocoderReceipts=len(report['vocoder']['receiptSha256s']),
        acousticReceipts=len(report['acoustic']['receiptSha256s']),
        vocoderTrainingSourceIdOverlap=overlap['vocoder'],
        acousticTrainingSourceIdOverlap=overlap['acoustic'],
        limitation='Declared receipt/split/source identity only; recipe equivalence and undeclared pretraining unprovable')
    return result


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
                 output, *, silence_phone='pau', vocoder_export=None, vocoder_checkpoint=None,
                 acoustic_export=None, acoustic_checkpoint=None, ancestry_audit=None):
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError('Output must be new with an existing parent')
    binding, captured = prepare_selection(selection, corpus, selection_sha256)
    return execute_campaign(binding, captured, bundle, renderer, pitch_executable, output,
        silence_phone=silence_phone, vocoder_export=vocoder_export,
        vocoder_checkpoint=vocoder_checkpoint, acoustic_export=acoustic_export,
        acoustic_checkpoint=acoustic_checkpoint, ancestry_audit=ancestry_audit)


def execute_campaign(binding, captured, bundle, renderer, pitch_executable, output, *,
                     silence_phone='pau', vocoder_export=None, vocoder_checkpoint=None,
                     acoustic_export=None, acoustic_checkpoint=None, ancestry_audit=None,
                     scope='selected-acoustic-corpus-only'):
    """Execute already verified captures; admission belongs to each entry point."""
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError('Output must be new with an existing parent')
    resource, resource_digest = capture_json(bundle / 'resource.json', 16384)
    manifest, manifest_digest = capture_json(bundle / 'manifest.json', 1048576)
    if (resource.get('formatId') != 'com.project-seam.neural-resource'
            or resource.get('schemaVersion') != 1 or resource.get('contentHash') != manifest_digest):
        raise ValueError('Candidate resource identity mismatch')
    training_audit = audit_combined_training([row[0] for row in captured], manifest,
        vocoder_export, vocoder_checkpoint, acoustic_export, acoustic_checkpoint,
        ancestry_audit)
    binary_hashes = {str(path): _capture(path, 256 * 1024 * 1024)[1]
                     for path in (renderer, pitch_executable)}
    output.mkdir()
    publish_new(output / 'selection.json', dict(binding, items=[row[0] for row in captured],
        manifestSha256=manifest_digest, resourceSha256=resource_digest,
        binarySha256=binary_hashes, silencePhone=silence_phone,
        validationScope=scope, vocoderTrainingAudit=training_audit['vocoder'],
        acousticTrainingAudit=training_audit['acoustic'],
        combinedTrainingAncestry=training_audit['ancestry']))
    selection_receipt_hash = _capture(output / 'selection.json', 1048576)[1]
    results = []
    inference_worker_sha = None
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
            worker_sha = run_render(renderer, bundle, manifest_digest, 256 * 1024 * 1024,
                project=project, output=directory / 'export', model_id=resource['id'],
                version=resource['version'], silence_phone=silence_phone)
            if (type(worker_sha) is not str or len(worker_sha) != 64
                    or any(char not in '0123456789abcdef' for char in worker_sha)):
                raise ValueError('Renderer did not report its inference worker digest')
            if inference_worker_sha is None:
                inference_worker_sha = worker_sha
            elif inference_worker_sha != worker_sha:
                raise ValueError('Inference worker changed during campaign')
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
        validationScope=scope, vocoderTrainingAudit=training_audit['vocoder'],
        acousticTrainingAudit=training_audit['acoustic'],
        combinedTrainingAncestry=training_audit['ancestry'],
        inferenceWorkerSha256=inference_worker_sha,
        combinedModelHoldoutVerified=False,
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
    parser.add_argument('--vocoder-export', type=Path)
    parser.add_argument('--vocoder-checkpoint', type=Path)
    parser.add_argument('--acoustic-export', type=Path)
    parser.add_argument('--acoustic-checkpoint', type=Path)
    parser.add_argument('--ancestry-audit', type=Path)
    parser.add_argument('--silence-phone', choices=('pau', 'SP', 'sil'), default='pau')
    args = parser.parse_args()
    report = run_campaign(**vars(args))
    print(json.dumps({key: report[key] for key in
                     ('selectedCount', 'executionPassed', 'singerQualified', 'releaseEligible')}))
    raise SystemExit(0 if report['executionPassed'] else 2)


if __name__ == '__main__':
    main()
