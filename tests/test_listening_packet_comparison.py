'''The listening packet comparison must report what changed and never rank two voices.

Section 6.3 of the revised plan requires comparing new output alongside a retained reference, never
regenerating the reference in place, and treating a difference as a request for investigation. A hash
cannot say a voice got worse, so these cases check that the tool never tries to.
'''
import hashlib
import json
import subprocess
import sys
import unittest
from pathlib import Path
import tempfile

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO))
from scripts.compare_listening_packets import compare, compare_artifacts  # noqa: E402


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


if __name__ == '__main__':
    unittest.main()

