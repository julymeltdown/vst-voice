import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.voice_model_training.validation_campaign import audit_vocoder_training, prepare_selection, run_campaign


def digest(payload):
    return hashlib.sha256(payload).hexdigest()


class ValidationCampaignTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.selection = self.root / 'selection.json'
        self.corpus = self.root / 'corpus.json'
        self.project = self.root / 'project.seam'
        self.project.write_bytes(b'project')
        (self.root / 'song-001').mkdir()
        (self.root / 'song-001' / 'source.wav').write_bytes(b'source')
        self.inventory = dict(formatId='com.project-seam.captured-teacher-corpus', schemaVersion=1,
            songs=[dict(sourceId='one', directory='song-001', partition='validation',
                        sourceSha256=digest(b'source'), projectSha256=digest(b'project'))],
            split=dict(groups=[dict(partition='validation', sourceIds=['one'])]), heldOutSongIds=[])
        self.items = [dict(sourceId='one', projectPath=str(self.project))]
        self.write_selection()

    def write_selection(self):
        self.corpus.write_text(json.dumps(self.inventory))
        self.selection.write_text(json.dumps(dict(formatId='com.project-seam.validation-selection',
            schemaVersion=1, corpusSha256=digest(self.corpus.read_bytes()), items=self.items)))
        self.sha = digest(self.selection.read_bytes())

    def prepare(self):
        return prepare_selection(self.selection, self.corpus, self.sha)

    def test_captures_exact_inputs(self):
        binding, captured = self.prepare()
        self.assertEqual(binding['selectionSha256'], self.sha)
        self.assertEqual(captured[0][1:], (b'source', b'project'))

    def test_rejects_train_test_and_disagreeing_partitions(self):
        for partition in ('train', 'test'):
            self.inventory['split']['groups'][0]['partition'] = partition
            self.write_selection()
            with self.assertRaisesRegex(ValueError, 'non-validation'):
                self.prepare()

    def test_rejects_duplicates_and_held_out(self):
        self.items *= 2
        self.write_selection()
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            self.prepare()
        self.items = self.items[:1]
        self.inventory['heldOutSongIds'] = ['one']
        self.write_selection()
        with self.assertRaisesRegex(ValueError, 'non-validation'):
            self.prepare()

    def test_rejects_changed_project_and_selection(self):
        self.project.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
            self.prepare()
        self.selection.write_text('{}')
        with self.assertRaisesRegex(ValueError, 'Selection SHA-256 mismatch'):
            self.prepare()

    def test_failed_item_retained_and_later_item_attempted(self):
        bundle = self.root / 'bundle'
        bundle.mkdir()
        (bundle / 'manifest.json').write_text('{}')
        (bundle / 'resource.json').write_text(json.dumps(dict(
            formatId='com.project-seam.neural-resource', schemaVersion=1,
            contentHash=digest(b'{}'), id='test', version='1')))
        binary = self.root / 'binary'
        binary.write_bytes(b'binary')
        captured = self.prepare()[1]
        second = (dict(captured[0][0], sourceId='two'), b'source', b'project')
        comparison = dict(referenceSha256=digest(b'source'), pitch=dict(comparison=dict(status='MISMATCH')))
        with patch('tools.voice_model_training.validation_campaign.prepare_selection',
                   return_value=({}, captured + [second])), patch(
                   'tools.voice_model_training.validation_campaign.run_render',
                   side_effect=[RuntimeError('render failed'), None]) as render, patch(
                   'tools.voice_model_training.validation_campaign.measure', return_value=comparison):
            report = run_campaign(self.selection, self.sha, self.corpus, bundle, binary, binary,
                                  self.root / 'output')
        self.assertEqual(render.call_count, 2)
        self.assertEqual(report['selectedCount'], 2)
        self.assertFalse(report['executionPassed'])
        self.assertEqual(report['items'][0]['error'], 'render failed')
        self.assertEqual(report['items'][1]['pitchStatus'], 'MISMATCH')
        self.assertFalse(report['singerQualified'])
        self.assertTrue((self.root / 'output' / 'song-000' / 'result.json').is_file())
        with patch('tools.voice_model_training.validation_campaign.run_render'), patch(
                'tools.voice_model_training.validation_campaign.measure', return_value=comparison):
            mismatch = run_campaign(self.selection, self.sha, self.corpus, bundle, binary, binary,
                                    self.root / 'mismatch-output')
        self.assertTrue(mismatch['executionPassed'])
        self.assertFalse(mismatch['releaseEligible'])
        self.assertEqual(mismatch['items'][0]['pitchStatus'], 'MISMATCH')
        self.assertEqual(mismatch['selectionReceiptSha256'], digest(
            (self.root / 'mismatch-output' / 'selection.json').read_bytes()))

    def test_existing_output_preserved(self):
        with self.assertRaisesRegex(ValueError, 'Output must be new'):
            run_campaign(self.selection, self.sha, self.corpus, self.root, self.project,
                         self.project, self.root)
        self.assertEqual(self.project.read_bytes(), b'project')

    def test_candidate_bound_vocoder_overlap_and_absence_are_not_holdout(self):
        receipt = self.root / 'checkpoint.json'
        receipt.write_text(json.dumps(dict(epoch=dict(datasetSha256='d' * 64,
            epochComplete=True, coverageVerified=True, sourceUpdates={'one': 4}))))
        exported = dict(vocoderSha256='v' * 64, datasetSha256='d' * 64,
                        checkpointReceiptSha256=digest(receipt.read_bytes()))
        manifest = dict(assets=[dict(role='vocoder', sha256='v' * 64)])
        with patch('tools.voice_model_training.prepare_bundle.read_report', return_value=(exported, b'graph')):
            overlap = audit_vocoder_training([dict(sourceId='one')], manifest, self.root, receipt)
            self.assertEqual(overlap['trainingSourceIdOverlap'], ['one'])
            self.assertEqual(overlap['status'], 'TRAINING_SOURCE_ID_OVERLAP')
            absent = audit_vocoder_training([dict(sourceId='two')], manifest, self.root, receipt)
            self.assertFalse(absent['combinedModelHoldoutVerified'])
            self.assertEqual(absent['status'], 'NO_SOURCE_ID_OVERLAP_IN_THIS_EPOCH')
            manifest['assets'][0]['sha256'] = 'wrong'
            with self.assertRaisesRegex(ValueError, 'does not bind'):
                audit_vocoder_training([], manifest, self.root, receipt)
            manifest['assets'][0]['sha256'] = 'v' * 64
            receipt.write_text('{}')
            with self.assertRaisesRegex(ValueError, 'does not bind'):
                audit_vocoder_training([], manifest, self.root, receipt)
        self.assertEqual(audit_vocoder_training([], {}, None, None)['status'], 'NOT_AUDITED')
        with self.assertRaisesRegex(ValueError, 'together'):
            audit_vocoder_training([], {}, self.root, None)


if __name__ == '__main__':
    unittest.main()
