"""Real native packet workflow; the fixture and binary are explicitly supplied."""
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from unittest import mock
import tempfile
import unittest
import numpy as np

from tools.singing_quality.listener_packet_003 import (
    build, verify, inventory, NAMES, harmonic_balance, onset_leads, require_question_properties,
)
from tools.singing_quality.listener_packet_contract import make_q3_control


def prepare_request(root, seed, cli):
    base = json.loads(seed.read_text())
    recipe = base['vocalTracks'][0]['proceduralRecipe']
    source = seed.parent / recipe['path']
    projects = {}
    for index, name in enumerate(NAMES):
        value = deepcopy(base)
        region = value['vocalTracks'][0]['regions'][0]
        if index < 4:
            midis = [67,64,62,60,62,64,67] if index < 2 else [55,60,67,72,79]
            notes, lyrics = region['notes'][:len(midis)], region['lyrics'][:len(midis)]
            for i, (note, lyric, midi) in enumerate(zip(notes, lyrics, midis)):
                note.update(midiKey=midi, startTick=i*(2880 if index==1 else 1920), durationTick=1920, phoneticHint=None)
                lyric.update(surface=(['あ','い','う','え','お','か','き'][i] if index < 2 else 'あ'), language='ja')
            region['notes'], region['lyrics'] = notes, lyrics
            region['durationTick'] = notes[-1]['startTick'] + notes[-1]['durationTick']
            if index == 2:
                selected = notes[2]; selected['startTick'] = 0
                region['notes'] = [selected]
                region['lyrics'] = [lyric for lyric in lyrics if lyric['id'] == selected['lyricId']]
                region['durationTick'] = 1920
        else:
            note = region['notes'][0]
            target = note['id']
            note['phoneticHint'] = 'pau'
            next(l for l in region['lyrics'] if l['id']==note['lyricId'])['surface'] = 'pau'
            if index == 5:
                value = make_q3_control(value, target)
        folder = root / Path(name).stem
        folder.mkdir()
        (folder / 'project.seam').write_text(json.dumps(value, ensure_ascii=False))
        dest = folder / recipe['path']; dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(source.read_bytes())
        projects[name] = str(folder / 'project.seam')
    build_evidence = root / 'build-evidence.txt'
    build_evidence.write_text('Regression uses supplied native binary; this is not a fresh-build attestation.\n')
    evidence = root / 'source-evidence.json'
    evidence.write_text(json.dumps({'sourceCommit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
        'rendererSha256':hashlib.sha256(cli.read_bytes()).hexdigest(),
        'buildEvidenceSha256':hashlib.sha256(build_evidence.read_bytes()).hexdigest(),
        'evidenceClass':'native-regression-not-release'}))
    request = root / 'request.json'
    request.write_text(json.dumps({'projects':projects, 'renderer':str(cli), 'sourceEvidence':str(evidence),
                                  'buildEvidence':str(build_evidence), 'q3NoteId':target}))
    return request


@unittest.skipUnless(os.environ.get('SEAM_LISTENER_PACKET_PROJECT') and os.environ.get('SEAM_VOICEBANK_CLI'),
                     'explicit packet source project and native CLI required; use the dedicated W01 lane')
class ListenerPacket003Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root = Path(cls.temporary.name)
        cls.request = prepare_request(cls.root, Path(os.environ['SEAM_LISTENER_PACKET_PROJECT']).resolve(),
                                      Path(os.environ['SEAM_VOICEBANK_CLI']).resolve())
        cls.original = cls.root / 'original'
        # The actual dry bake does NOT contain the lead assumed by Q1. Retain
        # this as a known native refusal, never turn it into a positive fixture.
        try:
            build(cls.request, cls.original)
        except ValueError as error:
            if 'question properties absent' not in str(error):
                raise
        else:
            raise AssertionError('native fixture unexpectedly has Q1 leads; investigate before rebasing')

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.packet = Path(self.temp.name) / 'copy'
        shutil.copytree(self.original, self.packet)

    def refresh_inventory(self):
        path = self.packet / 'manifest.json'
        manifest = json.loads(path.read_text()); manifest['fileHashes'] = inventory(self.packet)
        path.write_text(json.dumps(manifest))

    def test_native_absent_leads_are_still_rejected_after_moving(self):
        with self.assertRaisesRegex(ValueError, 'question properties absent'):
            verify(self.packet)

    def test_expected_manifest_identity_is_checked_before_other_properties(self):
        expected=hashlib.sha256((self.packet/'manifest.json').read_bytes()).hexdigest()
        with self.assertRaisesRegex(ValueError, 'question properties absent'):
            verify(self.packet, expected)
        with self.assertRaisesRegex(ValueError, 'expected manifest SHA-256 differs'):
            verify(self.packet, '0'*64)

    def test_audio_tamper_fails_for_hash_reason(self):
        path = self.packet / NAMES[0]; path.write_bytes(path.read_bytes()+b'x')
        with self.assertRaisesRegex(ValueError, 'inventory/hash'):
            verify(self.packet)

    def test_missing_recipe_fails_for_inventory_reason(self):
        next((self.packet/'inputs').glob('*/recipes/*.json')).unlink()
        with self.assertRaisesRegex(ValueError, 'inventory/hash'):
            verify(self.packet)

    def test_swapped_guide_answers_fail_even_with_updated_inventory(self):
        path = self.packet/'GUIDE.md'
        path.write_text(path.read_text().replace('**INTENTIONAL_REST**','**UNINTENDED_HOLE**'))
        self.refresh_inventory()
        with self.assertRaisesRegex(ValueError, 'guide differs'):
            verify(self.packet)

    def test_renderer_binding_mismatch_fails_even_with_updated_inventory(self):
        path = self.packet/'inputs/source-evidence.json'
        data = json.loads(path.read_text()); data['rendererSha256']='0'*64; path.write_text(json.dumps(data))
        self.refresh_inventory()
        with self.assertRaisesRegex(ValueError, 'renderer identity'):
            verify(self.packet)

    def test_non_target_resolved_phone_drift_is_rejected(self):
        path = next((self.packet/'renders'/Path(NAMES[-1]).stem/'candidates').glob('*.json'))
        data=json.loads(path.read_text());data['markers'][-1]['phone']='changed';path.write_text(json.dumps(data))
        self.refresh_inventory()
        with self.assertRaisesRegex(ValueError, 'non-target resolved markers'):
            verify(self.packet)

    def test_unrelated_project_change_is_rejected_after_hash_rebinding(self):
        stem=Path(NAMES[-1]).stem; path=self.packet/'inputs'/stem/'project.seam'
        data=json.loads(path.read_text());data['vocalTracks'][0]['regions'][0]['notes'][1]['midiKey']+=1
        path.write_text(json.dumps(data))
        manifest_path=self.packet/'manifest.json';manifest=json.loads(manifest_path.read_text())
        manifest['artifacts'][-1]['projectSha256']=hashlib.sha256(path.read_bytes()).hexdigest()
        manifest_path.write_text(json.dumps(manifest));self.refresh_inventory()
        with self.assertRaisesRegex(ValueError, 'more than the target'):
            verify(self.packet)

    def test_q1_unrelated_lyric_change_is_rejected(self):
        name=NAMES[1];path=self.packet/'inputs'/Path(name).stem/'project.seam'
        data=json.loads(path.read_text());data['vocalTracks'][0]['regions'][0]['lyrics'][0]['surface']='お'
        path.write_text(json.dumps(data))
        manifest_path=self.packet/'manifest.json';manifest=json.loads(manifest_path.read_text())
        manifest['artifacts'][1]['projectSha256']=hashlib.sha256(path.read_bytes()).hexdigest()
        manifest_path.write_text(json.dumps(manifest));self.refresh_inventory()
        with self.assertRaisesRegex(ValueError, 'more than the written gaps'):
            verify(self.packet)

    def test_missing_input_refuses_before_render_or_output_creation(self):
        value=json.loads(self.request.read_text());value['projects'][NAMES[0]]='absent.seam'
        request=Path(self.temp.name)/'bad-request.json';request.write_text(json.dumps(value))
        output=Path(self.temp.name)/'not-created'
        with mock.patch('tools.singing_quality.listener_packet_003.capture_command') as launch:
            with self.assertRaises(FileNotFoundError):
                build(request,output)
            launch.assert_not_called()
        self.assertFalse(output.exists())

    def test_cli_returns_nonzero_for_the_intended_hash_failure(self):
        path=self.packet/NAMES[0];path.write_bytes(path.read_bytes()+b'x')
        result=subprocess.run([sys.executable,'-m','tools.singing_quality.listener_packet_003',
                               'verify',str(self.packet)],capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,1)
        self.assertIn('inventory/hash mismatch',result.stderr)
        self.assertNotIn('Traceback',result.stderr)

    def test_existing_output_is_never_overwritten(self):
        before=(self.original/'manifest.json').read_bytes()
        with self.assertRaises(FileExistsError):
            build(self.request,self.original)
        self.assertEqual((self.original/'manifest.json').read_bytes(),before)


class ListenerQuestionMeasurementTests(unittest.TestCase):
    def test_known_harmonic_ratios_and_dc_offset(self):
        rate=48000; t=np.arange(rate)/rate; f=440*2**((67-69)/12)
        for ratio in (0.5, 2.0):
            signal=0.1*np.sin(2*np.pi*f*t)+ratio*0.1*np.sin(4*np.pi*f*t)+0.2
            measured=harmonic_balance(signal,rate,67)
            self.assertAlmostEqual(measured['h2OverH1'],ratio,delta=0.03)

    def test_no_partial_and_short_signal_are_not_a_pass(self):
        for value in (np.zeros(48000),np.ones(100)):
            with self.assertRaises(ValueError):
                harmonic_balance(value,48000,67)

    def test_known_origin_relative_marker_leads(self):
        project={'vocalTracks':[{'regions':[{'notes':[
            {'id':'1','startTick':0},{'id':'2','startTick':1920}]}]}]}
        metadata={'frameCount':96000,'scoreOriginFrame':1200,'markers':[
            {'key':'1:0','startFrame':0,'endFrame':48000,'phone':'a'},
            {'key':'2:0','startFrame':47520,'endFrame':96000,'phone':'a'}]}
        self.assertEqual(onset_leads(project,metadata),[0,480])

    def test_question_properties_fail_independently(self):
        require_question_properties({'legato':[0,480],'detached':[0,32]}, {'h2OverH1':2})
        for leads,ratio in (({'legato':[0,0],'detached':[0,32]},2),
                            ({'legato':[0,480],'detached':[0,0]},2),
                            ({'legato':[0,480],'detached':[0,32]},0.5)):
            with self.assertRaisesRegex(ValueError,'question properties absent'):
                require_question_properties(leads,{'h2OverH1':ratio})


if __name__ == '__main__':
    unittest.main()
