import hashlib
import importlib.util
import io
import json
import struct
import unittest
import wave
from tools.voice_model_training.audio_source import inspect_pcm_source, decode_pcm_source
from tools.voice_model_training.split import split_sources
from tools.voice_model_training.test_split import source


def wav(channels=1, width=2, rate=48000):
    output = io.BytesIO()
    with wave.open(output, "wb") as writer:
        writer.setnchannels(channels); writer.setsampwidth(width); writer.setframerate(rate)
        writer.writeframes(bytes(32 * channels * width))
    return output.getvalue()


def float_wav(values, rate=48000, encoding=3):
    pcm = struct.pack('<%df' % len(values), *values)
    fmt = struct.pack('<HHIIHH', encoding, 1, rate, rate * 4, 4, 32)
    body = b'WAVEfmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(pcm)) + pcm
    return b'RIFF' + struct.pack('<I', len(body)) + body


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

    @unittest.skipUnless(importlib.util.find_spec('numpy'), 'Optional NumPy decoder not installed')
    def test_float_renderer_samples_preserve_encoding_bits_and_clock(self):
        payload = float_wav([0.0, -0.0, 0.125, -0.5, 1.0, -1.0])
        report = self.inspect(payload)
        self.assertEqual(report['schemaVersion'], 2)
        self.assertEqual(report['sampleEncoding'], 'ieee-float32-le')
        self.assertEqual(report['frameCount'], 6)
        source, values = decode_pcm_source(payload, expected_sha256=report['sourceSha256'], sample_rate=48000)
        self.assertEqual(source, report)
        self.assertEqual(values.tolist(), [0.0, -0.0, 0.125, -0.5, 1.0, -1.0])
        import numpy as np
        self.assertTrue(np.signbit(values[1]))
        # Identical bytes interpreted as int32 are a different signal.
        integer = bytearray(payload)
        struct.pack_into('<H', integer, 20, 1)
        self.assertNotEqual(report['audioSha256'], self.inspect(bytes(integer))['audioSha256'])

    def test_integer_audio_identity_is_backward_compatible(self):
        payload = wav(width=4)
        geometry = dict(sampleRate=48000, channels=1, sampleWidthBytes=4, frameCount=32)
        previous = hashlib.sha256(json.dumps(geometry, sort_keys=True, separators=(',', ':')).encode()
                                  + b'\0' + payload[44:]).hexdigest()
        report = self.inspect(payload)
        self.assertEqual(report['schemaVersion'], 1)
        self.assertEqual(report['audioSha256'], previous)

    def test_float_refuses_nonfinite_and_unnormalized_samples_without_clipping(self):
        for value in (float('nan'), float('inf'), -float('inf'), 1.01, -1.01):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.inspect(float_wav([0.0, value]))

    def test_container_geometry_and_duplicate_chunks_are_not_guessed(self):
        source = float_wav([0.0, 0.25])
        malformed = []
        for offset, fmt, value in ((4, '<I', len(source)), (28, '<I', 7), (32, '<H', 2), (40, '<I', 7)):
            changed = bytearray(source)
            struct.pack_into(fmt, changed, offset, value)
            malformed.append(bytes(changed))
        for extra in (source[12:36], source[36:]):
            malformed.append(source[:4] + struct.pack('<I', len(source) - 8 + len(extra)) + source[8:] + extra)
        for payload in malformed:
            with self.subTest(payload=payload[:44]), self.assertRaises(ValueError):
                self.inspect(payload)

    def test_extensible_float_has_same_audio_identity_and_validated_guid(self):
        payload = float_wav([0.0, 0.125, -0.5])
        fmt = (struct.pack('<HHIIHHHHI', 0xfffe, 1, 48000, 192000, 4, 32, 22, 32, 4)
               + bytes.fromhex('0300000000001000800000aa00389b71'))
        body = b'WAVEfmt ' + struct.pack('<I', len(fmt)) + fmt + payload[36:]
        extended = b'RIFF' + struct.pack('<I', len(body)) + body
        self.assertEqual(self.inspect(payload)['audioSha256'], self.inspect(extended)['audioSha256'])
        bad = bytearray(extended)
        bad[59] ^= 1
        with self.assertRaises(ValueError):
            self.inspect(bytes(bad))

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
