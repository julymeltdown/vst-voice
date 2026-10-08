"""Exercise the packet verifier as an external process, including false-success probes."""
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / 'tools/singing_quality/verify_listener_packet_002.py'
PAIRS = {
    'q1-lead-timing': ['q1-legato-run.wav', 'q1-detached-run.wav'],
    'q2-harmonic-balance': ['q2-pitch-range.wav', 'q2-single-weak-fundamental.wav'],
    'q3-missing-note': ['q3-phrase-with-missing-note.wav', 'q3-phrase-control.wav'],
}


def wav(samples, floating=False):
    payload = b''.join(struct.pack('<f' if floating else '<h', x) for x in samples)
    width = 4 if floating else 2
    return struct.pack('<4sI4s4sIHHIIHH4sI', b'RIFF', 36 + len(payload), b'WAVE',
                       b'fmt ', 16, 3 if floating else 1, 1, 48000, 48000 * width,
                       width, 8 * width, b'data', len(payload)) + payload


class ListenerPacketIntegrity(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.packet = Path(self.temp.name)
        payload = wav([int(5000 * math.sin(2 * math.pi * 440 * i / 48000)) for i in range(4800)])
        self.manifest = {
            'formatId': 'com.project-seam.listening-packet', 'schemaVersion': 1,
            'cases': [{'id': key, 'files': files} for key, files in PAIRS.items()],
            'artifacts': [],
        }
        for name in sum(PAIRS.values(), []):
            (self.packet / name).write_bytes(payload)
            self.manifest['artifacts'].append({'file': name, 'sha256': hashlib.sha256(payload).hexdigest()})

    def run_cli(self, *args):
        (self.packet / 'manifest.json').write_text(json.dumps(self.manifest))
        return subprocess.run([sys.executable, str(CLI), str(self.packet), *args],
                              cwd=self.packet, capture_output=True, text=True, timeout=30)

    def assert_failure(self, result):
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn('PACKET_INTEGRITY=PASS', result.stdout)

    def replace_audio(self, payload):
        artifact = self.manifest['artifacts'][0]
        (self.packet / artifact['file']).write_bytes(payload)
        artifact['sha256'] = hashlib.sha256(payload).hexdigest()

    def test_valid_packet_is_portable_read_only_and_not_acceptance(self):
        before = {p.name: p.read_bytes() for p in self.packet.iterdir()}
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('PACKET_INTEGRITY=PASS', result.stdout)
        self.assertIn('musical acceptance and provenance are NOT_VERIFIED', result.stdout)
        for name, contents in before.items():
            self.assertEqual((self.packet / name).read_bytes(), contents)

    def test_checksum_mismatch_is_a_failing_process(self):
        self.manifest['artifacts'][0]['sha256'] = '0' * 64
        result = self.run_cli()
        self.assert_failure(result)
        self.assertIn('MISMATCH', result.stdout)

    def test_hash_consistent_silence_is_a_failing_process(self):
        self.replace_audio(wav([0] * 4800))
        self.assert_failure(self.run_cli())

    def test_nonfinite_float_audio_is_rejected_even_with_matching_hash(self):
        self.replace_audio(wav([0.25, float('nan'), 0.25], floating=True))
        result = self.run_cli()
        self.assert_failure(result)
        self.assertIn('non-finite', result.stderr)

    def test_empty_and_truncated_wav_are_rejected(self):
        for payload in [wav([]), wav([2000] * 100)[:-3], b'not a wave file']:
            with self.subTest(payload_size=len(payload)):
                self.replace_audio(payload)
                self.assert_failure(self.run_cli())

    def test_missing_artifact_and_omitted_manifest_entry_are_rejected(self):
        (self.packet / self.manifest['artifacts'][0]['file']).unlink()
        self.assert_failure(self.run_cli())
        self.manifest['artifacts'].pop(0)
        self.assert_failure(self.run_cli())

    def test_duplicate_artifact_cannot_mask_a_missing_file(self):
        self.manifest['artifacts'][0] = self.manifest['artifacts'][1]
        self.assert_failure(self.run_cli())

    def test_empty_or_incomplete_case_inventory_is_rejected(self):
        self.manifest['cases'].pop()
        self.assert_failure(self.run_cli())
        self.manifest['cases'] = []
        self.assert_failure(self.run_cli())

    def test_wrong_question_files_are_rejected(self):
        self.manifest['cases'][0]['files'] = PAIRS['q3-missing-note']
        self.assert_failure(self.run_cli())

    def test_diagnostics_require_an_explicit_project(self):
        result = self.run_cli('--diagnostics')
        self.assert_failure(result)
        self.assertIn('--q3-project', result.stderr)

    def test_missing_explicit_project_fails_without_implicit_fallback(self):
        self.assert_failure(self.run_cli('--diagnostics', '--q3-project', str(self.packet / 'missing.seam')))


if __name__ == '__main__':
    unittest.main()
