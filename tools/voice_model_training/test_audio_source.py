import hashlib
import io
import struct
import unittest
import wave
from tools.voice_model_training.audio_source import inspect_pcm_source
from tools.voice_model_training.split import split_sources
from tools.voice_model_training.test_split import source


def wav(channels=1, width=2, rate=48000):
    output = io.BytesIO()
    with wave.open(output, "wb") as writer:
        writer.setnchannels(channels); writer.setsampwidth(width); writer.setframerate(rate)
        writer.writeframes(bytes(32 * channels * width))
    return output.getvalue()


class AudioSourceTests(unittest.TestCase):
    def inspect(self, payload, rate=48000):
        return inspect_pcm_source(payload, expected_sha256=hashlib.sha256(payload).hexdigest(), sample_rate=rate)

    def test_supported_widths_and_source_preservation(self):
        for width in (2, 3, 4):
            payload = wav(width=width)
            report = self.inspect(payload)
            self.assertEqual(report["frameCount"], 32)
            self.assertEqual(report["sampleWidthBytes"], width)
            self.assertFalse(report["sourceRightsAdmitted"])
            self.assertEqual(report["sourceSha256"], hashlib.sha256(payload).hexdigest())

    def test_container_metadata_does_not_hide_exact_pcm_duplicate(self):
        payload = wav()
        extra = b"JUNK" + struct.pack("<I", 4) + b"test"
        changed = payload[:4] + struct.pack("<I", len(payload) - 8 + len(extra)) + payload[8:] + extra
        first, second = self.inspect(payload), self.inspect(changed)
        self.assertNotEqual(first["sourceSha256"], second["sourceSha256"])
        self.assertEqual(first["audioSha256"], second["audioSha256"])
        split = split_sources([source(0, audioSha256=first["audioSha256"]),
                               source(1, audioSha256=second["audioSha256"])], seed="pilot", held_out_songs=["song-0"])
        self.assertEqual(split["uniqueAudioCounts"]["test"], 1)
        self.assertEqual(split["redundantSourceCount"], 1)

    def test_rejects_wrong_digest_shape_clock_and_truncation(self):
        with self.assertRaises(ValueError):
            inspect_pcm_source(wav(), expected_sha256="0" * 64, sample_rate=48000)
        for payload in (wav(channels=2), wav(width=1), wav(rate=44100), wav()[:-1], b"not wav"):
            with self.assertRaises(ValueError):
                self.inspect(payload)
        self.assertNotEqual(self.inspect(wav())["audioSha256"], self.inspect(wav(rate=44100), 44100)["audioSha256"])


if __name__ == "__main__":
    unittest.main()
