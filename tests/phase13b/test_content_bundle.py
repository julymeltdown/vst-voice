import hashlib
import json
import os
import tempfile
import unittest
import zipfile
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'phase13b'))

from content_bundle import create_development_bundle  # noqa: E402


class ContentBundleTests(unittest.TestCase):
    def test_development_bundle_is_deterministic_and_explicitly_nonrelease(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            voice = root / 'voice'
            character = root / 'character'
            voice.mkdir(); character.mkdir()
            (voice / 'manifest.json').write_text('{"official":false}\n', encoding='utf-8')
            (voice / 'sample.wav').write_bytes(b'voice')
            (character / 'asset-manifest.json').write_text('{"developmentOnly":true}\n', encoding='utf-8')
            (character / 'portrait.png').write_bytes(b'character')
            a = root / 'a.zip'; b = root / 'b.zip'
            first = create_development_bundle(voice, character, a, version='0.13.1')
            second = create_development_bundle(voice, character, b, version='0.13.1')
            self.assertEqual(first['sha256'], second['sha256'])
            with zipfile.ZipFile(a) as archive:
                self.assertIn('ProjectSEAM/DEVELOPMENT_ONLY.txt', archive.namelist())
                manifest = json.loads(archive.read('ProjectSEAM/content-manifest.json'))
                self.assertFalse(manifest['releaseEligible'])
                self.assertTrue(manifest['developmentOnly'])

    @unittest.skipUnless(hasattr(os, 'symlink'), 'symlink unavailable')
    def test_symlink_in_content_tree_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            voice = root / 'voice'; character = root / 'character'
            voice.mkdir(); character.mkdir()
            outside = root / 'outside.txt'; outside.write_text('outside', encoding='utf-8')
            os.symlink(outside, voice / 'link')
            (character / 'asset').write_text('x', encoding='utf-8')
            with self.assertRaises(ValueError):
                create_development_bundle(voice, character, root / 'out.zip', version='0.13.1')


if __name__ == '__main__':
    unittest.main()
