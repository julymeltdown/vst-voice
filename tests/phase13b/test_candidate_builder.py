import hashlib
import json
import tempfile
import unittest
import zipfile
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'phase13b'))

from candidate_builder import build_candidate  # noqa: E402


class CandidateBuilderTests(unittest.TestCase):
    def test_candidate_archives_are_byte_identical(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            voice = root / 'voice.json'
            char = root / 'character.json'
            report = root / 'report.json'
            voice.write_text('{"voice":1}\n', encoding='utf-8')
            char.write_text('{"character":1}\n', encoding='utf-8')
            report.write_text('{"passed":false}\n', encoding='utf-8')
            a = root / 'a.zip'
            b = root / 'b.zip'
            kwargs = dict(
                output=a,
                component_files={'voicebank-dossier.json': voice, 'character-dossier.json': char, 'release-report.json': report},
                release_result={'passed': False, 'blockedTargets': ['contract']},
                product_version='0.13.0-rc1',
            )
            build_candidate(**kwargs)
            kwargs['output'] = b
            build_candidate(**kwargs)
            self.assertEqual(hashlib.sha256(a.read_bytes()).hexdigest(), hashlib.sha256(b.read_bytes()).hexdigest())
            with zipfile.ZipFile(a) as archive:
                manifest = json.loads(archive.read('candidate-manifest.json'))
                self.assertFalse(manifest['releaseEligible'])


if __name__ == '__main__':
    unittest.main()
