import hashlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import wave

import numpy as np

from tools.voice_model_training.compare_application_export import decode_master, measure


def pcm_wav(samples, width=2, rate=48000):
    samples = np.asarray(samples).reshape(len(samples), -1)
    integers = (samples * (1 << (8 * width - 1))).astype(np.int64).ravel()
    raw = b''.join(int(value).to_bytes(width, 'little', signed=True) for value in integers)
    stream = io.BytesIO()
    with wave.open(stream, 'wb') as writer:
        writer.setnchannels(samples.shape[1])
        writer.setsampwidth(width)
        writer.setframerate(rate)
        writer.writeframes(raw)
    return stream.getvalue()


class ApplicationComparisonTests(unittest.TestCase):
    def test_signed_pcm_widths_and_channel_order(self):
        samples = np.tile([[-.5, .25], [.125, -.75]], (1024, 1))
        for width in (2, 3, 4):
            identity, decoded = decode_master(pcm_wav(samples, width))
            self.assertEqual(identity['sampleWidthBytes'], width)
            np.testing.assert_array_equal(samples, decoded)

    def test_refuses_wrong_rate_and_truncation(self):
        samples = np.zeros(2048)
        for payload in (pcm_wav(samples, rate=44100), pcm_wav(samples)[:-2]):
            with self.assertRaises(ValueError):
                decode_master(payload)

    def compare(self, source, master):
        with tempfile.TemporaryDirectory() as directory:
            reference, candidate = (Path(directory) / name for name in ('source.wav', 'master.wav'))
            reference.write_bytes(source)
            candidate.write_bytes(master)
            with patch('tools.voice_model_training.compare_application_export.compare_wavs',
                       return_value={'comparison': {'status': 'UNRESOLVED'}}) as pitch:
                result = measure(reference, candidate, executable=Path('unused'))
                pitch.assert_called_once()
            self.assertEqual(result['referenceSha256'], hashlib.sha256(source).hexdigest())
            self.assertEqual(result['candidateSha256'], hashlib.sha256(master).hexdigest())
            return result

    def test_identical_dual_mono_has_zero_spectral_distance(self):
        samples = np.tile([.25, -.25], 1024)
        result = self.compare(pcm_wav(samples), pcm_wav(np.column_stack([samples, samples])))
        self.assertEqual(result['spectralDistance'], 0)
        self.assertEqual(result['channelRms'], [.25, .25])
        self.assertFalse(result['singerQualified'])

    def test_stereo_cancellation_keeps_channel_energy_visible(self):
        samples = np.tile([.25, -.25], 1024)
        result = self.compare(pcm_wav(samples), pcm_wav(np.column_stack([samples, -samples])))
        self.assertEqual(result['downmixedRms'], 0)
        self.assertEqual(result['channelRms'], [.25, .25])
        self.assertGreater(result['spectralDistance'], 0)

    def test_different_lengths_are_not_trimmed(self):
        with self.assertRaisesRegex(ValueError, 'lengths differ'):
            self.compare(pcm_wav(np.zeros(2048)), pcm_wav(np.zeros(4096)))


if __name__ == '__main__':
    unittest.main()
