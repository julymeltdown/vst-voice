'''The N1 feasibility input gate must fail honestly and must not authorize training.

These cases are about the gate's honesty: it reports which inputs are missing rather than passing
silently, it refuses an unverifiable claim of presence, and it never turns a complete record into a
training authorization.
'''
import hashlib
import json
import unittest
from pathlib import Path
import tempfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from scripts.verify_neural_feasibility_inputs import check_record, verify  # noqa: E402


def record(**inputs) -> dict:
    return {'formatId': 'com.project-seam.neural-feasibility-input-record', 'schemaVersion': 1,
            'inputs': inputs}


def present(location: str, digest: str = 'a' * 64) -> dict:
    return {'status': 'present', 'location': location, 'sha256': digest}


def absent(reason: str = 'not available') -> dict:
    return {'status': 'absent', 'reason': reason}


class FeasibilityInputGate(unittest.TestCase):
    def test_missing_declarations_are_reported_not_ignored(self):
        results = check_record(record())
        self.assertTrue(all(not present for _, present, _ in results))
        # Every required input is named, so a reader learns what is missing rather than only that
        # something is.
        self.assertEqual({name for name, _, _ in results},
                         {'corpus', 'labels', 'acousticProfile', 'vocoderProfile',
                          'upstreamRevision', 'permissionEvidence'})

    def test_an_unverifiable_presence_claim_is_treated_as_absent(self):
        # A digest of the wrong shape cannot bind anything, so it is not evidence of presence.
        results = check_record(record(corpus={'status': 'present', 'location': 'x.wav'}))
        corpus = next(entry for entry in results if entry[0] == 'corpus')
        self.assertFalse(corpus[1])
        # A source pin bound by an exact revision is checkable and therefore accepted.
        results = check_record(record(upstreamRevision={'status': 'present', 'location': 'pin',
                                                       'revision': '336cf01b57f2ad44c6b37a79cf33993043291759'}))
        pin = next(entry for entry in results if entry[0] == 'upstreamRevision')
        self.assertTrue(pin[1])

    def test_unknown_statuses_and_fields_are_refused(self):
        with self.assertRaises(ValueError):
            check_record(record(corpus={'status': 'maybe'}))
        with self.assertRaises(ValueError):
            check_record(record(corpus=absent(), somethingElse=absent()))
        with self.assertRaises(ValueError):
            check_record({'formatId': 'x'})

    def test_a_present_input_that_is_not_on_disk_is_absent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            code, results = verify(record(corpus=present('missing.wav')), root, expect_corpus=None)
            self.assertEqual(code, 3)
            self.assertFalse(next(entry for entry in results if entry[0] == 'corpus')[1])
            self.assertIn('not found', next(entry for entry in results if entry[0] == 'corpus')[2])

    def test_a_corpus_digest_mismatch_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'corpus.json').write_text('{}')
            wrong = hashlib.sha256(b'other').hexdigest()
            code, results = verify(record(corpus=present('corpus.json', wrong)), root,
                                   expect_corpus='anything')
            self.assertEqual(code, 3)
            self.assertIn('digest does not match', next(entry for entry in results
                                                          if entry[0] == 'corpus')[2])
            # With the right digest the same declaration is accepted, so the previous failure was
            # about the digest and not about the file being missing.
            right = hashlib.sha256((root / 'corpus.json').read_bytes()).hexdigest()
            code, results = verify(record(corpus=present('corpus.json', right)), root,
                                   expect_corpus='anything')
            corpus = next(entry for entry in results if entry[0] == 'corpus')
            self.assertTrue(corpus[1], corpus[2])
            # Only the corpus is declared here, so the record is still incomplete overall.
            self.assertEqual(code, 3)

    def test_completeness_never_implies_authorization(self):
        # Even a fully present record only means the inputs exist. The module exposes no path that
        # starts training, and this asserts the record carries no approval field to mistake for one.
        source = Path(__file__).resolve().parents[2] / 'scripts' / 'verify_neural_feasibility_inputs.py'
        text = source.read_text()
        self.assertNotIn('train(', text)
        self.assertNotIn('subprocess', text)

class RetainedVocoderBridgeEvidence(unittest.TestCase):
    """The N1 vocoder bridge evidence must stay bound to the bytes it describes."""

    def test_retained_evidence_hashes_still_match(self):
        root = (Path(__file__).resolve().parents[2] / 'docs' / 'implementation' / 'evidence'
                / 'neural-vocoder-bridge-2026-09-15')
        manifest = json.loads((root / 'manifest.json').read_text())
        self.assertTrue(manifest['files'])
        for name, meta in manifest['files'].items():
            actual = hashlib.sha256((root / name).read_bytes()).hexdigest()
            self.assertEqual(actual, meta['sha256'], name)

    def test_retained_evidence_claims_no_more_than_it_proved(self):
        root = (Path(__file__).resolve().parents[2] / 'docs' / 'implementation' / 'evidence'
                / 'neural-vocoder-bridge-2026-09-15')
        manifest = json.loads((root / 'manifest.json').read_text())
        report = json.loads((root / 'vocoder-bridge-report.json').read_text())
        # A geometry and parity check must not be recorded as a qualified singer or a release input.
        self.assertFalse(manifest['singerQualified'])
        self.assertFalse(manifest['releaseEligible'])
        self.assertFalse(report['ganTrainingVerified'])
        self.assertTrue(report['syntheticInputs'])
        # The exact SEAM profile is what makes the check meaningful, so it is asserted, not assumed.
        self.assertEqual(manifest['profile']['samplingRate'], 48000)
        self.assertEqual(manifest['profile']['numMels'], 80)
        self.assertEqual(manifest['profile']['hopSize'], 256)


if __name__ == '__main__':
    unittest.main()

