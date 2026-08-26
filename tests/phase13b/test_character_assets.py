import hashlib
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
import sys

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'phase13b'))

from character_assets import generate_character_assets  # noqa: E402


class CharacterAssetTests(unittest.TestCase):
    def _source(self, root: Path) -> Path:
        source = root / 'source.png'
        image = Image.new('RGB', (320, 480), (26, 22, 31))
        for y in range(80, 440):
            for x in range(90, 230):
                image.putpixel((x, y), (112 + (x % 19), 65 + (y % 23), 93 + ((x + y) % 17)))
        image.save(source)
        return source

    def test_generation_is_deterministic_and_hashes_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = self._source(root)
            first = root / 'first'
            second = root / 'second'
            a = generate_character_assets(source, first)
            b = generate_character_assets(source, second)
            self.assertEqual(a['sourceSha256'], hashlib.sha256(source.read_bytes()).hexdigest())
            self.assertEqual(a['assetSha256'], b['assetSha256'])
            self.assertTrue(a['developmentOnly'])
            for name in ('key-art-1024.png', 'portrait-512.png', 'thumbnail-256.png', 'silhouette-256.png', 'palette.json', 'asset-manifest.json'):
                self.assertTrue((first / name).is_file(), name)

    def test_manifest_labels_assets_as_nonproduction(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / 'out'
            generate_character_assets(self._source(root), output)
            manifest = json.loads((output / 'asset-manifest.json').read_text(encoding='utf-8'))
            self.assertEqual('official.character.01', manifest['characterId'])
            self.assertTrue(manifest['developmentOnly'])
            self.assertEqual('NOT_A_PRODUCTION_TURNAROUND', manifest['productionStatus'])

    def test_evidence_generator_does_not_rewrite_tracked_character_assets(self):
        assets = ROOT / 'assets/character-01/production-development'
        before = {
            path.name: (hashlib.sha256(path.read_bytes()).hexdigest(), path.stat().st_mtime_ns)
            for path in assets.iterdir()
            if path.is_file()
        }
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / 'scripts/generate_phase13b_evidence.py'),
                    '--root',
                    str(ROOT),
                    '--output',
                    directory,
                ],
                check=False,
                capture_output=True,
                text=True,
            )
        self.assertEqual(0, result.returncode, result.stderr)
        after = {
            path.name: (hashlib.sha256(path.read_bytes()).hexdigest(), path.stat().st_mtime_ns)
            for path in assets.iterdir()
            if path.is_file()
        }
        self.assertEqual(before, after)


if __name__ == '__main__':
    unittest.main()
