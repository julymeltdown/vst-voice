import hashlib
from pathlib import Path
import tempfile
import unittest
from tools.voice_model_training.prepare import prepare_sources
from tools.voice_model_training.test_audio_source import wav


class PrepareTests(unittest.TestCase):
    def test_inspects_bytes_and_reports_failures_without_partial_split(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payload = wav()
            (root / "source.wav").write_bytes(payload)
            row = dict(sourceId="s1", songId="song", sessionId="session", lineageId="lineage",
                       path="source.wav", sourceSha256=hashlib.sha256(payload).hexdigest())
            result = prepare_sources(root, [row], sample_rate=48000)
            self.assertEqual(result["rejectedCount"], 0)
            self.assertEqual(len(result["splitSources"]), 1)
            for path in ("../source.wav", "/source.wav", "missing.wav"):
                bad = dict(row, sourceId="s2", path=path)
                result = prepare_sources(root, [row, bad], sample_rate=48000)
                self.assertEqual(result["rejectedCount"], 1)
                self.assertEqual(result["splitSources"], [])
                self.assertEqual(len(result["sources"]), 2)
            (root / "link.wav").symlink_to(root / "source.wav")
            self.assertEqual(prepare_sources(root, [dict(row, path="link.wav")], sample_rate=48000)["rejectedCount"], 1)
            self.assertEqual(prepare_sources(root, [dict(row, sourceSha256="0" * 64)], sample_rate=48000)["rejectedCount"], 1)
            self.assertEqual((root / "source.wav").read_bytes(), payload)


if __name__ == "__main__":
    unittest.main()
