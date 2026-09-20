import unittest
from pathlib import Path
import tempfile
import numpy as np

from tools.voice_model_training.reconstruct_source_vocoder import checked_waveform, reconstruct


class SourceVocoderTests(unittest.TestCase):
    def test_only_declared_padding_is_trimmed(self):
        original = np.linspace(-.5, .5, 2304, dtype=np.float32)[None]
        result = checked_waveform(original, source_frames=2050)
        np.testing.assert_array_equal(result, original[0, :2050])
        result[0] = 0
        self.assertEqual(original[0, 0], -.5)

    def test_invalid_padding_is_not_hidden_by_trimming(self):
        for bad in (float('nan'), float('inf'), 1.01, -1.01):
            original = np.zeros((1, 2304), dtype=np.float32)
            original[0, -1] = bad
            with self.assertRaises(ValueError):
                checked_waveform(original, source_frames=2050)

    def test_wrong_channels_or_length_are_refused(self):
        for shape in ((2304,), (2, 2304), (1, 2050), (1, 2560)):
            with self.assertRaises(ValueError):
                checked_waveform(np.zeros(shape), source_frames=2050)

    def test_changed_source_is_refused_before_loading_export_or_creating_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source.wav'
            source.write_bytes(b'changed source')
            output = root / 'result'
            with self.assertRaisesRegex(ValueError, 'Source differs'):
                reconstruct(source, root / 'missing-export', source_sha256='a' * 64,
                            executable=root / 'missing-extractor', output=output)
            self.assertFalse(output.exists())

    def test_existing_output_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            marker = root / 'retained.json'
            marker.write_text('retained')
            with self.assertRaisesRegex(ValueError, 'new directory'):
                reconstruct(root / 'missing-source', root, source_sha256='a' * 64,
                            executable=root, output=root)
            self.assertEqual(marker.read_text(), 'retained')


if __name__ == '__main__':
    unittest.main()
