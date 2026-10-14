"""Closed-schema and held-output tests; the native process is mocked here.

Real vibrato/legato/nasal replay and coordinated output forgeries run inside
seam_sample_review_cli_tests. This fixture is not singing-quality evidence.
"""
import copy
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

from tools.external_beta import installed_continuity_record as audit
from tests.external_beta.test_installed_project_binding_record import fixture as binding_fixture


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def wav(values, rate=48000):
    pcm = struct.pack('<' + 'f' * len(values), *values)
    return (b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt ' +
        struct.pack('<IHHIIHH', 16, 3, 1, rate, rate * 4, 4, 32) +
        b'data' + struct.pack('<I', len(pcm)) + pcm)


class Fixture:
    def __init__(self, root):
        self.root = root; self.output = root / 'continuity'; self.output.mkdir()
        linked = binding_fixture(True); linked['project']['noteCount'] = 1
        self.record = audit.FIXED | dict(installed=linked['installed'], project=linked['project'],
            platform='macos', sourceCommit='a' * 40, buildId='mocked-native', sampleRate=48000,
            startFrame=5, endFrame=11, chunkFrames=3, compiledContextSha256='c' * 64,
            chunks=[], artifacts=[], **{k: 0 for k in audit.DIFFERENCES})
        marker = dict(key='0000000000000001:0', phone='a', startFrame=5, endFrame=11,
            startClipped=False, endClipped=False, gestureKindNativeEnum=0, palatalized=False)
        self.partition = dict(schemaVersion=1,
            timingSource='SHARED_COMPILED_PLAN_PROJECTION_NOT_ACOUSTIC_MEASUREMENT',
            scoreNotes=[dict(noteId='0000000000000001', startFrame=5, endFrame=11,
                midiKey=60, articulationNativeEnum=0, vibratoEnabled=False, vibratoPhaseTurns=0)],
            wholeMarkers=[marker], chunkMarkers=[], boundaries=[dict(frame=8, insidePlannedPhones=[marker])])
        # Finite values above unity and signed zero must survive raw-word checks.
        values = [-0.0, 1.25, -1.25, 0.5, 0.0, -0.5]
        for name in ('whole.wav', 'restarted-joined.wav', 'resumed-joined.wav'):
            (self.output / name).write_bytes(wav(values))
        for i in range(2):
            begin = 5 + i * 3; end = begin + 3
            self.record['chunks'].append(dict(index=i, startFrame=begin, endFrame=end,
                checkpointBefore=begin, checkpointAfter=end, chunkContentSha256='d' * 64,
                sharedCompiledContextSha256='c' * 64, **{k: 0 for k in audit.DIFFERENCES}))
            part = marker | dict(startFrame=begin, endFrame=end, startClipped=i != 0, endClipped=i != 1)
            self.partition['chunkMarkers'].append(dict(index=i, restarted=[part], resumed=[part]))
            for mode in ('restarted', 'resumed'):
                (self.output / f'{mode}-{i}.wav').write_bytes(wav(values[i * 3:(i + 1) * 3]))
        self.write_partition()
        for path in sorted(self.output.iterdir()):
            partition = path.name == 'partition.json'
            self.record['artifacts'].append(dict(path=path.name, sha256=sha(path),
                frames=0 if partition else 6 if 'joined' in path.name or path.name == 'whole.wav' else 3,
                channels=0 if partition else 1,
                encoding='JSON_DERIVED_PLAN_PROJECTION' if partition else 'IEEE_FLOAT32_UNCLAMPED'))
        self.path = root / 'record.json'
        self.cli = root / 'caller-cli'; self.cli.write_bytes(b'mocked CLI')
        self.key = root / 'caller-key'; self.key.write_bytes(b'mocked public key')
        self.args = dict(record_path=self.path, project_path=root / 'project', package_path=root / 'package',
            installed_directory=root / 'installed', continuity_directory=self.output,
            cli_path=self.cli, cli_sha256=sha(self.cli), public_key_path=self.key, public_key_sha256=sha(self.key))
        self.update(); self.outputs = []

    def write_partition(self):
        (self.output / 'partition.json').write_text(json.dumps(self.partition))

    def update(self):
        self.path.write_text(json.dumps(self.record))
        (self.output / 'observation.json').write_bytes(self.path.read_bytes())
        self.args['record_sha256'] = sha(self.path)

    def repin(self, name):
        for x in self.record['artifacts']:
            if x['path'] == name: x['sha256'] = sha(self.output / name)
        self.update()

    def native(self, command, timeout):
        assert command == [str(self.cli), 'observe-installed-procedural-continuity',
            str(self.args['project_path']), self.record['project']['projectSha256'],
            self.record['project']['trackId'], self.record['project']['regionId'], 'ja',
            str(self.args['package_path']), self.record['installed']['packageDigest'],
            self.record['installed']['resourceCandidateSha256'], str(self.args['installed_directory']),
            str(self.key), '3', command[-1]]
        output = Path(command[-1]); output.mkdir(); self.outputs.append(output)
        for p in self.output.iterdir(): (output / p.name).write_bytes(p.read_bytes())
        return copy.deepcopy(self.record)


class InstalledContinuityRecordTests(unittest.TestCase):
    def test_closed_success_schema_refuses_mismatch_and_release_claims(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(Path(tmp)); audit.validate_record(f.record)
            for change in ({'command': 'untrusted'}, {'authorizesRelease': True}, {'schemaVersion': True},
                {'sampleRate': True}, {'canonicalTolerance': 'FROZEN'}, {'exactFloat32WordsEqual': False},
                {'resumedMergedMarkerProjectionMatches': False}, {'restartedDifferentFloat32Words': 1},
                {'resumedMaxAbsDifference': float('nan')}, {'chunkFrames': 1}, {'platform': 'windows'}):
                with self.subTest(change=change), self.assertRaises(ValueError):
                    audit.validate_record(f.record | change)
            for key, value in (('checkpointBefore', 6), ('index', True), ('sharedCompiledContextSha256', 'e' * 64)):
                changed = copy.deepcopy(f.record); changed['chunks'][0][key] = value
                with self.subTest(key=key), self.assertRaises(ValueError): audit.validate_record(changed)
            for name in ('../whole.wav', '/whole.wav', 'whole.wav/other', 'observation.json'):
                changed = copy.deepcopy(f.record); changed['artifacts'][0]['path'] = name
                with self.subTest(name=name), self.assertRaises(ValueError): audit.validate_record(changed)
            changed = copy.deepcopy(f.record); changed['artifacts'][1] = changed['artifacts'][0]
            with self.assertRaises(ValueError): audit.validate_record(changed)

    def test_fixed_command_raw_words_and_private_output_cleanup(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(Path(tmp)); before = {p.name: sha(p) for p in f.output.iterdir()}
            with mock.patch.object(audit.installed, '_run', side_effect=f.native):
                result = audit.audit_installed_continuity_record(**f.args)
            self.assertTrue(result['passed'], result); self.assertFalse(result['authorizesRelease'])
            self.assertEqual([], result['ignoredRecordFields'])
            self.assertEqual('NOT_AUTHENTICATED', result['historicalExecution'])
            self.assertEqual('UNRESOLVED', result['canonicalTolerance'])
            self.assertTrue(all(not p.parent.exists() for p in f.outputs))
            self.assertEqual(before, {p.name: sha(p) for p in f.output.iterdir()})

    def test_word_rate_and_nonfinite_changes_refuse_with_consistent_hashes(self):
        for case in ('signed-zero', 'sample-rate', 'nonfinite'):
            with tempfile.TemporaryDirectory() as tmp:
                f = Fixture(Path(tmp)); p = f.output / 'resumed-0.wav'; raw = bytearray(p.read_bytes())
                if case == 'signed-zero': struct.pack_into('<f', raw, 44, 0.0)
                elif case == 'sample-rate': struct.pack_into('<I', raw, 24, 44100)
                else: struct.pack_into('<f', raw, 44, float('nan'))
                p.write_bytes(raw); f.repin(p.name)
                with mock.patch.object(audit.installed, '_run', side_effect=AssertionError('must not execute')):
                    result = audit.audit_installed_continuity_record(**f.args)
                self.assertFalse(result['passed'], (case, result))

    def test_projection_geometry_and_clipping_are_derived_not_trusted(self):
        for case in ('cut', 'clip', 'enum', 'note', 'extra'):
            with tempfile.TemporaryDirectory() as tmp:
                f = Fixture(Path(tmp))
                if case == 'cut': f.partition['boundaries'][0]['frame'] = 9
                elif case == 'clip': f.partition['chunkMarkers'][1]['resumed'][0]['startClipped'] = False
                elif case == 'enum': f.partition['wholeMarkers'][0]['gestureKindNativeEnum'] = True
                elif case == 'note': f.partition['scoreNotes'][0]['noteId'] = '0' * 16
                else: f.partition['unused'] = True
                f.write_partition(); f.repin('partition.json')
                with mock.patch.object(audit.installed, '_run', side_effect=AssertionError('must not execute')):
                    result = audit.audit_installed_continuity_record(**f.args)
                self.assertFalse(result['passed'], (case, result))

    def test_missing_symlinked_fifo_and_overbudget_inputs_refuse_before_execution(self):
        import os
        for case in ('missing', 'leaf-link', 'root-link', 'observation-link', 'fifo', 'budget'):
            with tempfile.TemporaryDirectory() as tmp:
                f = Fixture(Path(tmp)); p = f.output / 'whole.wav'
                if case == 'missing': p.unlink()
                elif case == 'leaf-link': p.rename(f.root / 'foreign'); p.symlink_to(f.root / 'foreign')
                elif case == 'root-link':
                    f.output.rename(f.root / 'foreign-dir'); f.output.symlink_to(f.root / 'foreign-dir', target_is_directory=True)
                elif case == 'observation-link':
                    p = f.output / 'observation.json'; p.unlink(); p.symlink_to(f.path)
                elif case == 'fifo': p.unlink(); os.mkfifo(p)
                with mock.patch.object(audit.installed, '_run', side_effect=AssertionError('must not execute')), \
                        mock.patch.object(audit, 'AGGREGATE_LIMIT', 1 if case == 'budget' else audit.AGGREGATE_LIMIT):
                    result = audit.audit_installed_continuity_record(**f.args)
                self.assertFalse(result['passed'], (case, result))

    def test_external_pins_and_invalid_timeout_refuse_before_execution(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(Path(tmp))
            with mock.patch.object(audit.installed, '_run', side_effect=AssertionError('must not execute')):
                for key in ('record_sha256', 'cli_sha256', 'public_key_sha256'):
                    self.assertFalse(audit.audit_installed_continuity_record(**(f.args | {key: 'f' * 64}))['passed'])
                for timeout in (0, -1, True, float('nan'), float('inf'), 301):
                    self.assertFalse(audit.audit_installed_continuity_record(**(f.args | {'timeout_seconds': timeout}))['passed'])

    def test_retained_or_fresh_substitution_and_key_drift_are_refused(self):
        for case in ('retained-audio', 'retained-record', 'key', 'cli', 'fresh-audio', 'fresh-record', 'fresh-symlink'):
            with tempfile.TemporaryDirectory() as tmp:
                f = Fixture(Path(tmp))
                def substitute(command, timeout):
                    result = f.native(command, timeout); fresh = Path(command[-1])
                    paths = {'retained-audio': f.output / 'whole.wav', 'retained-record': f.path,
                        'key': f.key, 'cli': f.cli, 'fresh-audio': fresh / 'whole.wav', 'fresh-record': fresh / 'observation.json'}
                    if case == 'fresh-symlink':
                        p = fresh / 'whole.wav'; p.unlink(); p.symlink_to(f.output / 'whole.wav')
                    else: paths[case].write_bytes(b'substituted')
                    return result
                with mock.patch.object(audit.installed, '_run', side_effect=substitute):
                    result = audit.audit_installed_continuity_record(**f.args)
                self.assertFalse(result['passed'], (case, result))
                self.assertTrue(all(not p.parent.exists() for p in f.outputs))

    def test_no_ignored_fields_in_fresh_comparison(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(Path(tmp))
            def altered(command, timeout):
                result = f.native(command, timeout); result['compiledContextSha256'] = 'e' * 64
                for chunk in result['chunks']: chunk['sharedCompiledContextSha256'] = 'e' * 64
                return result
            with mock.patch.object(audit.installed, '_run', side_effect=altered):
                result = audit.audit_installed_continuity_record(**f.args)
            self.assertFalse(result['passed']); self.assertIn('fresh native', result['errors'][0])

    def test_native_timeout_cleans_scratch_and_preserves_retained_outputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(Path(tmp)); before = {p.name: sha(p) for p in f.output.iterdir()}; scratch = []
            def refuse(command, timeout):
                scratch.append(Path(command[-1]).parent); raise subprocess.TimeoutExpired(command, timeout)
            with mock.patch.object(audit.installed, '_run', side_effect=refuse):
                result = audit.audit_installed_continuity_record(**f.args)
            self.assertFalse(result['passed']); self.assertTrue(all(not p.exists() for p in scratch))
            self.assertEqual(before, {p.name: sha(p) for p in f.output.iterdir()})


if __name__ == '__main__': unittest.main()
