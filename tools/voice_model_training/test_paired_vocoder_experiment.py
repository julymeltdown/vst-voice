import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import subprocess

from tools.voice_model_training.paired_vocoder_experiment import arm_state, export_arms


class PairedExperimentTests(unittest.TestCase):
    def loader(self):
        def load(path, digest):
            if Path(path).name in ('profile.json', 'p.json'):
                return dict(profileId='seam-full-hop-slaney-v1')
            return json.loads(Path(path).read_bytes())
        return load

    def test_unready_arm_is_reported_not_substituted(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            arm=dict(name='control',output=str(root/'control'))
            self.assertFalse(arm_state(arm)['completed'])
            (root/'control').mkdir()
            with patch('tools.voice_model_training.paired_vocoder_experiment.load_config',
                       side_effect=self.loader()):
                report=export_arms([arm],profile=root/'profile.json',profile_sha256='a'*64,
                    checkout=root,output_root=root/'exports')
            self.assertEqual(report['arms'][0]['state'],'NOT_READY')
            self.assertFalse(report['allReady'])
            self.assertFalse(report['releaseEligible'])

    def test_exported_arm_binds_run_receipt(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);arm_root=root/'control';epoch=arm_root/'epoch-000001';epoch.mkdir(parents=True)
            (epoch/'checkpoint.json').write_text('{}')
            receipt=hashlib.sha256(b'{}').hexdigest()
            (arm_root/'run.json').write_text(json.dumps({'checkpoints':[
                dict(receiptSha256=receipt,path='epoch-000001',binariesRetained=True)],
                'formatId':'com.project-seam.vocoder-training-run'}))
            destination=root/'exports'/'control'
            def fake(command,**kwargs):
                Path(command[command.index('--output')+1]).mkdir(parents=True)
                (destination/'export.json').write_text('{}')
                return subprocess.CompletedProcess(command,0,'','')
            with patch('tools.voice_model_training.paired_vocoder_experiment.load_config',
                       side_effect=self.loader()), \
                 patch('tools.voice_model_training.paired_vocoder_experiment.subprocess.run',side_effect=fake):
                report=export_arms([dict(name='control',output=str(arm_root))],profile=root/'p.json',
                    profile_sha256='a'*64,checkout=root,output_root=root/'exports')
            self.assertTrue(report['allReady'])
            self.assertEqual(report['arms'][0]['receiptSha256'],receipt)
            (arm_root/'run.json').write_text(json.dumps({'checkpoints':[
                dict(receiptSha256='b'*64,path='epoch-000001',binariesRetained=True)],
                'formatId':'com.project-seam.vocoder-training-run'}))

    def test_wrong_run_format_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);arm_root=root/'control';epoch=arm_root/'epoch-000001';epoch.mkdir(parents=True)
            (epoch/'checkpoint.json').write_text('{}')
            (arm_root/'run.json').write_text(json.dumps({'checkpoints':[]}))
            with patch('tools.voice_model_training.paired_vocoder_experiment.load_config',side_effect=self.loader()):
                with self.assertRaises(ValueError):
                    export_arms([dict(name='control',output=str(arm_root))],profile=root/'p.json',
                        profile_sha256='a'*64,checkout=root,output_root=root/'exports')
            with patch('tools.voice_model_training.paired_vocoder_experiment.load_config',
                       side_effect=self.loader()):
                with self.assertRaises(ValueError):
                    export_arms([dict(name='control',output=str(arm_root))],profile=root/'p.json',
                        profile_sha256='a'*64,checkout=root,output_root=root/'other')


if __name__=='__main__':unittest.main()
