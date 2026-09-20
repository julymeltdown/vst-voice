import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.voice_model_training.frozen_evaluation_campaign import prepare, run
from tools.voice_model_training import test_render_frozen_evaluation as fixtures


class FrozenCampaignTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.plan, self.history = fixtures.FrozenEvaluationTests().fixture()
        self.source = self.plan['items'][0]['sourceId']
        song = self.root / self.source
        song.mkdir()
        (song / 'source.wav').write_bytes(b'captured-source')
        project = dict(ppq=960, vocalTracks=[dict(regions=[dict(
            lyrics=[dict(id='a', language='ja', surface='あ')],
            notes=[dict(lyricId='a', midiKey=60, startTick=0, durationTick=480)])])])
        project_hash = self.write('project.seam', project)
        self.plan['voiceRecipeSha256'] = 'a' * 64
        self.plan['baselineManifestSha256'] = 'b' * 64
        self.plan['historicalIndexSha256'] = self.write('history.json', self.history)
        self.provenance = dict(projectSha256=project_hash, recipeSha256='a' * 64,
            sourceSha256=hashlib.sha256(b'captured-source').hexdigest(), exportReceiptSha256='c' * 64)
        self.receipt = dict(self.provenance, sourceId=self.source, trainingAdmitted=False)
        self.reference = dict(formatId='com.project-seam.frozen-evaluation-capture', schemaVersion=1,
            state='CAPTURED', pilotUnchanged=True, trainingAdmitted=False,
            items=[dict(self.provenance, sourceId=self.source, state='CAPTURED', exportRoot=str(self.root))])
        self.prepared = dict(formatId='com.project-seam.frozen-evaluation-preparation', schemaVersion=1,
            trainingAdmitted=False, items=[dict(sourceId=self.source, state='PREPARED_UNAPPROVED')])

    def write(self, name, value):
        path = self.root / name
        path.write_text(json.dumps(value))
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def args(self):
        plan_hash = self.write('plan.json', self.plan)
        self.reference['planSha256'] = plan_hash
        capture_hash = self.write('capture.json', self.reference)
        self.prepared['captureSha256'] = capture_hash
        if self.prepared['items']:
            self.prepared['items'][0]['preparationSha256'] = self.write(
                self.source + '/preparation.json', self.receipt)
        prepared_hash = self.write('preparation.json', self.prepared)
        return dict(plan=self.root/'plan.json', plan_sha256=plan_hash, history=self.root/'history.json',
                    capture=self.root/'capture.json', capture_sha256=capture_hash,
                    preparation=self.root/'preparation.json', preparation_sha256=prepared_hash)

    def test_exact_capture_without_training_partition(self):
        binding, captured, _ = prepare(**self.args())
        self.assertEqual(captured[0][1], b'captured-source')
        self.assertEqual(captured[0][0]['sourceId'], self.source)
        self.assertIn('frozenCaptureSha256', binding)

    def test_incomplete_selection_rejected(self):
        self.prepared['items'] = []
        with self.assertRaisesRegex(ValueError, 'complete frozen cohort'):
            prepare(**self.args())

    def test_changed_source_and_wrong_recipe_rejected(self):
        args = self.args()
        (self.root/self.source/'source.wav').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'source/project changed'):
            prepare(**args)
        self.receipt['recipeSha256'] = 'd' * 64
        with self.assertRaisesRegex(ValueError, 'differs from frozen capture'):
            prepare(**self.args())

    def test_baseline_identity_and_explicit_scope(self):
        args = dict(self.args(), bundle=self.root, renderer=self.root/'renderer',
                    pitch_executable=self.root/'pitch', output=self.root/'output')
        with patch('tools.voice_model_training.frozen_evaluation_campaign.prepare',
                   return_value=({}, [], self.plan)), patch(
                   'tools.voice_model_training.frozen_evaluation_campaign._capture',
                   return_value=(b'', 'wrong')):
            with self.assertRaisesRegex(ValueError, 'baseline manifest'):
                run(**args)
        with patch('tools.voice_model_training.frozen_evaluation_campaign.prepare',
                   return_value=({}, [], self.plan)), patch(
                   'tools.voice_model_training.frozen_evaluation_campaign._capture',
                   return_value=(b'', 'b'*64)), patch(
                   'tools.voice_model_training.frozen_evaluation_campaign.execute_campaign') as execute:
            run(**args)
            self.assertEqual(execute.call_args.kwargs['scope'],
                             'frozen-same-voice-unseen-phrase-engineering-only')


if __name__ == '__main__':
    unittest.main()
