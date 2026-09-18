'''The listening packet comparison must report what changed and never rank two voices.

Section 6.3 of the revised plan requires comparing new output alongside a retained reference, never
regenerating the reference in place, and treating a difference as a request for investigation. A hash
cannot say a voice got worse, so these cases check that the tool never tries to.
'''
import hashlib
import contextlib
import io
import json
import subprocess
import sys
import unittest
from unittest import mock
from pathlib import Path
import tempfile

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO))
from scripts.compare_listening_packets import (  # noqa: E402
    compare, compare_artifacts, bind_reference_item, bind_reference_manifest,
    promote_reference, run_asr_triage, DEFAULT_ASR_MODEL, DEFAULT_DECODING_SETTINGS,
    PINNED_NEGATIVE_CONTROLS, REFERENCE_SET_FORMAT_ID
)


def manifest(packet_id: str, commit: str, outputs: list) -> dict:
    return {'packetId': packet_id, 'sourceCommit': commit, 'cases': [{'id': 'song', 'outputs': outputs}],
            'artifacts': []}


def output(path: str, digest: str, **identity) -> dict:
    entry = {'path': path, 'variant': 'baseline', 'sha256': digest}
    entry.update(identity)
    return entry


class ListeningPacketComparison(unittest.TestCase):
    def test_identical_packets_report_identical(self):
        one = manifest('a', 'c1', [output('song/baseline/master.wav', 'x' * 64)])
        findings = compare(one, one)
        self.assertEqual([f['status'] for f in findings], ['identical'])

    def test_a_changed_output_is_reported_with_its_identity_change(self):
        reference = manifest('a', 'c1', [output('song/baseline/master.wav', 'x' * 64, recipeHash='r1',
                                           sampleRate=48000, channels=2, frames=100)])
        candidate = manifest('b', 'c2', [output('song/baseline/master.wav', 'y' * 64, recipeHash='r2',
                                          sampleRate=48000, channels=2, frames=100)])
        findings = compare(reference, candidate)
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0]['status'], 'changed')
        # The recipe changed, so the difference is attributed rather than left as an unexplained hash.
        self.assertEqual(findings[0]['identityChanged'], ['recipeHash'])
        self.assertEqual(findings[0]['referenceIdentity']['recipeHash'], 'r1')
        self.assertEqual(findings[0]['candidateIdentity']['recipeHash'], 'r2')

    def test_missing_and_added_outputs_are_distinguished(self):
        reference = manifest('a', 'c1', [output('song/baseline/master.wav', 'x' * 64)])
        candidate = manifest('b', 'c2', [output('song/baseline/other.wav', 'y' * 64)])
        statuses = sorted(f['status'] for f in compare(reference, candidate))
        self.assertEqual(statuses, ['added', 'missing'])

    def test_rerender_tree_is_compared_at_the_reference_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'song' / 'baseline').mkdir(parents=True)
            payload = b'audio-bytes'
            (root / 'song' / 'baseline' / 'master.wav').write_bytes(payload)
            digest = hashlib.sha256(payload).hexdigest()
            reference = manifest('a', 'c1', [output('song/baseline/master.wav', digest)])
            findings = compare_artifacts(reference, root)
            self.assertEqual([f['status'] for f in findings], ['identical'])
            # A different rendering is reported as changed without being called worse.
            reference = manifest('a', 'c1', [output('song/baseline/master.wav', 'x' * 64)])
            findings = compare_artifacts(reference, root)
            self.assertEqual(findings[0]['status'], 'changed')
            self.assertNotIn('verdict', findings[0])

    def test_a_partial_rerender_is_scoped_to_one_case(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'baseline').mkdir(parents=True)
            payload = b'master'
            (root / 'baseline' / 'master.wav').write_bytes(payload)
            reference = manifest('a', 'c1', [output('song/baseline/master.wav',
                                                hashlib.sha256(payload).hexdigest())])
            findings = compare_artifacts(reference, root, only_case='song')
            self.assertEqual([f['status'] for f in findings], ['identical'])

    def test_the_tool_never_records_a_ranking_and_never_writes_the_reference(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reference_path = root / 'reference.json'
            reference_path.write_text(json.dumps(
                manifest('a', 'c1', [output('song/baseline/master.wav', 'x' * 64)])))
            before = hashlib.sha256(reference_path.read_bytes()).hexdigest()
            candidate_path = root / 'candidate.json'
            candidate_path.write_text(json.dumps(
                manifest('b', 'c2', [output('song/baseline/master.wav', 'y' * 64)])))
            report = root / 'report.json'
            completed = subprocess.run(
                [sys.executable, str(REPO / 'scripts' / 'compare_listening_packets.py'),
                 str(reference_path), str(candidate_path), '--report', str(report)],
                capture_output=True, text=True)
            # A difference is exit 3, and the reference file is byte-identical afterwards.
            self.assertEqual(completed.returncode, 3, completed.stderr)
            self.assertEqual(hashlib.sha256(reference_path.read_bytes()).hexdigest(), before)
            self.assertTrue(report.exists())
            written = json.loads(report.read_text())
            self.assertEqual(written['verdict'], 'UNRANKED')
            self.assertEqual(written['counts'].get('changed'), 1)
            # An existing report is never overwritten.
            again = subprocess.run(
                [sys.executable, str(REPO / 'scripts' / 'compare_listening_packets.py'),
                 str(reference_path), str(candidate_path), '--report', str(report)],
                capture_output=True, text=True)
            self.assertEqual(again.returncode, 2)

    def test_the_retained_d1_packet_compares_to_its_own_rerender(self):
        reference = REPO / 'docs' / 'implementation' / 'listening' / '2026-09-15-d1-02' / 'manifest.json'
        repeat = Path('/Users/lhs/Downloads/seam-listening-artifacts/2026-09-15-d1-repeat')
        if not repeat.is_dir():
            self.skipTest('the retained rerender tree is not present on this host')
        # A partial rerender writes the case at its own root, so it is compared case-scoped.
        findings = compare_artifacts(json.loads(reference.read_text()), repeat,
                                    only_case='unfamiliar-song')
        self.assertTrue(findings)
        statuses = {f['status'] for f in findings}
        self.assertEqual(statuses, {'identical'}, str(findings))

    def test_reference_set_manifest_binding_binds_all_contract_fields(self):
        sample_output = output(
            'song/baseline/master.wav', 'a' * 64, recipeHash='r1',
            scoreIdentity='song', recipeIdentity='baseline', resourceIdentity='character-01',
            engineRevision='c1', renderRevision='p1',
            renderSettings={'sampleRate': 48000, 'channels': 2, 'hopSize': 256},
            sampleRate=48000, channels=2, frames=96000, durationSeconds=2.0,
            peak=0.08, rms=0.02, clippedSamples=0,
            spectralDistance=0.12, f0Rmse=1.5, levelDb=-14.2
        )
        bound_item = bind_reference_item(sample_output, 'song', {'packetId': 'p1', 'sourceCommit': 'c1'})
        self.assertEqual(bound_item['scoreIdentity'], 'song')
        self.assertEqual(bound_item['recipeIdentity'], 'baseline')
        self.assertEqual(bound_item['recipeHash'], 'r1')
        self.assertEqual(bound_item['resourceIdentity'], 'character-01')
        self.assertEqual(bound_item['engineRevision'], 'c1')
        self.assertEqual(bound_item['renderRevision'], 'p1')
        self.assertEqual(bound_item['wavSha256'], 'a' * 64)
        self.assertEqual(bound_item['measurements']['spectralDistance'], 0.12)
        self.assertEqual(bound_item['measurements']['f0Rmse'], 1.5)
        self.assertEqual(bound_item['measurements']['levelDb'], -14.2)

        packet = manifest('p1', 'c1', [sample_output])
        ref_set = bind_reference_manifest(packet, reference_set_id='ref-01', reason='Initial baseline')
        self.assertEqual(ref_set['formatId'], REFERENCE_SET_FORMAT_ID)
        self.assertEqual(ref_set['referenceSetId'], 'ref-01')
        self.assertEqual(ref_set['reason'], 'Initial baseline')
        self.assertEqual(len(ref_set['items']), 1)
        self.assertEqual(ref_set['verdict'], 'UNRANKED')

    def test_comparison_reports_named_reference_and_measurements_change(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ref_item = output('song/baseline/master.wav', 'a' * 64, recipeHash='r1',
                              sampleRate=48000, channels=2, frames=96000, durationSeconds=2.0,
                              peak=0.08, rms=0.02, clippedSamples=0, spectralDistance=0.10)
            cand_item = output('song/baseline/master.wav', 'b' * 64, recipeHash='r1',
                               sampleRate=48000, channels=2, frames=96000, durationSeconds=2.0,
                               peak=0.08, rms=0.02, clippedSamples=0, spectralDistance=0.25)
            ref_manifest = bind_reference_manifest(manifest('ref-packet', 'c1', [ref_item]), reference_set_id='retained-ref-01')
            cand_manifest = manifest('cand-packet', 'c2', [cand_item])
            ref_path = root / 'reference_set.json'
            cand_path = root / 'candidate_packet.json'
            ref_path.write_text(json.dumps(ref_manifest))
            cand_path.write_text(json.dumps(cand_manifest))
            completed = subprocess.run(
                [sys.executable, str(REPO / 'scripts' / 'compare_listening_packets.py'),
                 str(ref_path), str(cand_path)],
                capture_output=True, text=True)
            self.assertEqual(completed.returncode, 3)
            self.assertIn('REFERENCE=retained-ref-01', completed.stdout)
            self.assertIn('CANDIDATE=cand-packet', completed.stdout)
            self.assertIn('spectralDistance', completed.stdout)
            self.assertIn('reference=retained-ref-01', completed.stdout)

    def test_promotion_requires_reason_and_rejects_in_place_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ref_path = root / 'reference.json'
            cand_path = root / 'candidate.json'
            ref_path.write_text(json.dumps(manifest('p1', 'c1', [output('song/baseline/master.wav', 'a' * 64)])))
            cand_path.write_text(json.dumps(manifest('p2', 'c2', [output('song/baseline/master.wav', 'b' * 64)])))

            # Promotion without --reason fails
            failed_reason = subprocess.run(
                [sys.executable, str(REPO / 'scripts' / 'compare_listening_packets.py'),
                 str(ref_path), str(cand_path), '--promote-reference', str(root / 'new_ref.json')],
                capture_output=True, text=True)
            self.assertEqual(failed_reason.returncode, 2)
            self.assertIn('--reason is required', failed_reason.stdout)

            # In-place overwrite (promoting onto existing file) fails
            existing_file = root / 'already_exists.json'
            existing_file.write_text('{}')
            failed_overwrite = subprocess.run(
                [sys.executable, str(REPO / 'scripts' / 'compare_listening_packets.py'),
                 str(ref_path), str(cand_path), '--promote-reference', str(existing_file),
                 '--reason', 'Valid reason'],
                capture_output=True, text=True)
            self.assertEqual(failed_overwrite.returncode, 2)
            self.assertIn('already exists', failed_overwrite.stdout)

            # Promoting beside the old one with explicit reason succeeds
            promoted_dest = root / 'promoted_ref_01.json'
            success = subprocess.run(
                [sys.executable, str(REPO / 'scripts' / 'compare_listening_packets.py'),
                 str(ref_path), str(cand_path), '--promote-reference', str(promoted_dest),
                 '--reason', 'Auditioned vowel balance upgrade', '--allow-changes'],
                capture_output=True, text=True)
            self.assertEqual(success.returncode, 0, success.stderr)
            self.assertTrue(promoted_dest.exists())
            promoted_content = json.loads(promoted_dest.read_text())
            self.assertEqual(promoted_content['formatId'], REFERENCE_SET_FORMAT_ID)
            self.assertEqual(promoted_content['reason'], 'Auditioned vowel balance upgrade')
            self.assertEqual(promoted_content['verdict'], 'UNRANKED')

    def test_asr_triage_refuses_metadata_only_confidence_without_a_real_model(self):
        sample_manifest = manifest('p1', 'c1', [
            output('song/baseline/master.wav', 'a' * 64, peak=0.08, rms=0.02, clippedSamples=0, durationSeconds=2.0),
            output('song/baseline/silent.wav', 'b' * 64, peak=0.0, rms=0.00001, clippedSamples=0, durationSeconds=2.0),
        ])
        with self.assertRaisesRegex(ValueError, '--asr-model is required'):
            run_asr_triage(sample_manifest)

    def test_asr_triage_cli_emits_no_evidence_when_no_model_is_configured(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            packet_path = root / 'packet.json'
            report_path = root / 'triage_report.json'
            packet_path.write_text(json.dumps(manifest('p1', 'c1', [
                output('song/baseline/master.wav', 'a' * 64, peak=0.08, rms=0.02, clippedSamples=0, durationSeconds=2.0),
            ])))
            completed = subprocess.run(
                [sys.executable, str(REPO / 'scripts' / 'compare_listening_packets.py'),
                 str(packet_path), '--asr-triage', '--report', str(report_path)],
                capture_output=True, text=True)
            self.assertEqual(completed.returncode, 2, completed.stderr)
            self.assertIn('ASR_TRIAGE=NOT_RUN', completed.stdout)
            self.assertNotIn('ASR_TRIAGE=PASS', completed.stdout)
            self.assertFalse(report_path.exists())

    def test_control_breach_is_retained_and_cannot_be_accepted_with_allow_changes(self):
        from scripts import compare_listening_packets as cli
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            packet = root / 'packet.json'
            report = root / 'report.json'
            packet.write_text(json.dumps(manifest('p', 'c', [])))
            # Explicit CLI orchestration double; real control execution is tested separately.
            breach = {'model': {'backend': 'TEST_DOUBLE'}, 'label': 'triage',
                      'verdict': 'triage_control_breach',
                      'summary': {'totalItems': 1, 'flaggedItems': 0, 'controlsBreached': True}}
            with mock.patch.object(sys, 'argv', [str(cli.__file__), str(packet), '--asr-triage',
                                               '--report', str(report), '--allow-changes']), \
                 mock.patch.object(cli, 'run_asr_triage', return_value=breach), \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(cli.main(), 3)
            self.assertEqual(json.loads(report.read_text())['asrTriage']['verdict'],
                             'triage_control_breach')

    def test_cli_help_documents_reference_set_and_promotion_and_asr_triage(self):
        completed = subprocess.run(
            [sys.executable, str(REPO / 'scripts' / 'compare_listening_packets.py'), '--help'],
            capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0)
        self.assertIn('--promote-reference', completed.stdout)
        self.assertIn('--reason', completed.stdout)
        self.assertIn('--asr-triage', completed.stdout)
        self.assertIn('reference set', completed.stdout.lower())




class ListeningPacketSpectralMeasurement(unittest.TestCase):
    """V06 needs a formant change to be visible, not only peak and rms."""

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def write_tone(self, name: str, frequency: float, seconds: float = 0.5,
                   rate: int = 48000) -> Path:
        import math as _math
        import struct as _struct
        frames = int(rate * seconds)
        payload = b"".join(
            _struct.pack("<f", 0.25 * _math.sin(2.0 * _math.pi * frequency * i / rate))
            for i in range(frames))
        header = _struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len(payload), b"WAVE",
                              b"fmt ", 16, 3, 1, rate, rate * 4, 4, 32, b"data", len(payload))
        path = self.root / name
        path.write_bytes(header + payload)
        return path

    def test_measurements_include_spectral_shape_beyond_peak_and_rms(self) -> None:
        from tools.singing_quality.listening_packet import wav_measurements
        measured = wav_measurements(self.write_tone("tone.wav", 440.0))
        for key in ("levelDb", "spectralDistance", "spectralCentroidHz", "spectralRolloffHz"):
            self.assertIn(key, measured)
        self.assertLess(measured["levelDb"], 0.0)
        self.assertGreater(measured["spectralCentroidHz"], 0.0)
        self.assertGreater(measured["spectralDistance"], 0.0)

    def test_a_higher_frequency_tone_has_a_higher_centroid(self) -> None:
        from tools.singing_quality.listening_packet import wav_measurements
        low = wav_measurements(self.write_tone("low.wav", 220.0))
        high = wav_measurements(self.write_tone("high.wav", 880.0))
        self.assertLess(low["spectralCentroidHz"], high["spectralCentroidHz"])
        self.assertLess(low["spectralRolloffHz"], high["spectralRolloffHz"])

    def test_measurement_never_claims_a_qualification(self) -> None:
        from tools.singing_quality.listening_packet import wav_measurements
        measured = wav_measurements(self.write_tone("plain.wav", 440.0))
        text = json.dumps(measured).lower()
        self.assertNotIn("qualified", text)
        self.assertNotIn("\"pass\"", text)


if __name__ == '__main__':
    unittest.main()
