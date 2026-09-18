"""Orchestration regressions with explicit test doubles, no acoustic acceptance."""
import copy
import io
import struct
import tempfile
import unittest
import wave
from pathlib import Path

from tools.singing_quality.asr_triage import (
    DEFAULT_DECODING_SETTINGS, PINNED_NEGATIVE_CONTROLS, character_error_rate,
    control_wav, normalize_text, screen_packet, sha256, validate_wave,
)


class FakeRecognizer:
    identity = {"backend": "TEST_DOUBLE_NOT_ACOUSTIC_EVIDENCE"}
    settings = DEFAULT_DECODING_SETTINGS

    def __init__(self, outputs):
        self.outputs = iter(outputs)
        self.inputs = []

    def transcribe(self, payload):
        self.inputs.append(payload)
        return {"text": next(self.outputs), "segments": []}


class AsrTriageTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.audio = control_wav("impulse-v1")
        (self.root / "audio.wav").write_bytes(self.audio)
        # Deliberately false metadata: the implementation must use the bytes.
        self.manifest = {"cases": [{"id": "song", "outputs": [{
            "path": "audio.wav", "sha256": sha256(self.audio), "peak": 0.8,
            "rms": 0.9, "clippedSamples": 0, "durationSeconds": 900,
        }]}]}

    def test_controls_and_packet_are_really_passed_to_the_same_backend(self):
        backend = FakeRecognizer(["", "", "", "朝"])
        result = screen_packet(self.manifest, self.root, backend, {"song": "あさ"})
        self.assertEqual(len(backend.inputs), 4)
        self.assertEqual(backend.inputs[:3], [control_wav(n) for n in PINNED_NEGATIVE_CONTROLS])
        self.assertEqual(backend.inputs[-1], self.audio)
        self.assertEqual(result["schemaVersion"], 2)
        self.assertEqual(result["items"][0]["transcription"]["text"], "朝")
        self.assertEqual(result["items"][0]["characterErrorRate"], 1.0)
        self.assertNotIn("confidence", result["items"][0])
        self.assertNotIn("readyForListening", result["summary"])
        self.assertFalse(result["releaseEligible"])
        self.assertEqual(result["perceptualStatus"], "UNREVIEWED")

    def test_hallucinated_control_breaches_even_when_the_song_text_matches(self):
        backend = FakeRecognizer(["", "ご視聴ありがとうございました", "", "あさ"])
        result = screen_packet(self.manifest, self.root, backend, {"song": "あさ"})
        self.assertEqual(result["verdict"], "triage_control_breach")
        self.assertTrue(result["summary"]["controlsBreached"])
        self.assertEqual(result["negativeControls"][1]["status"], "BREACH")
        self.assertEqual(result["items"][0]["characterErrorRate"], 0)

    def test_absent_text_does_not_invent_a_confidence_or_acceptance(self):
        result = screen_packet(self.manifest, self.root, FakeRecognizer([""] * 4))
        self.assertTrue(result["items"][0]["flagged"])
        self.assertEqual(result["items"][0]["reasons"], ["no_text_recognized"])
        self.assertIsNone(result["items"][0]["characterErrorRate"])
        self.assertEqual(result["summary"]["textComparedItems"], 0)

    def test_changed_missing_escaping_duplicate_and_empty_audio_refuse_before_inference(self):
        variants = []
        changed = copy.deepcopy(self.manifest)
        changed["cases"][0]["outputs"][0]["sha256"] = "f" * 64
        variants.append(changed)
        for path in ("missing.wav", str(self.root / "audio.wav"), "../outside.wav"):
            variant = copy.deepcopy(self.manifest)
            variant["cases"][0]["outputs"][0]["path"] = path
            variants.append(variant)
        duplicate = copy.deepcopy(self.manifest)
        duplicate["cases"][0]["outputs"] *= 2
        variants.extend([duplicate, {"cases": []}])
        for variant in variants:
            with self.subTest(variant=variant):
                backend = FakeRecognizer([])
                with self.assertRaises((OSError, ValueError)):
                    screen_packet(variant, self.root, backend)
                self.assertEqual(backend.inputs, [])

    def test_controls_have_actual_distinct_repeatable_pcm(self):
        digests = set()
        for name in PINNED_NEGATIVE_CONTROLS:
            payload = control_wav(name)
            self.assertEqual(payload, control_wav(name))
            digests.add(sha256(payload))
            with wave.open(io.BytesIO(payload)) as audio:
                self.assertEqual(audio.getnframes(), 32000)
                self.assertEqual(audio.getframerate(), 16000)
                pcm = audio.readframes(32000)
            if name == "silence-v1":
                self.assertEqual(pcm, bytes(64000))
            elif name == "impulse-v1":
                self.assertEqual(pcm[:32000] + pcm[32002:], bytes(63998))
                self.assertNotEqual(pcm[32000:32002], bytes(2))
            else:
                self.assertGreater(len(set(pcm)), 200)
        self.assertEqual(len(digests), 3)

    def test_text_comparison_is_orthographic_and_does_not_guess_kanji_readings(self):
        self.assertEqual(normalize_text(" アサ、 Ａ! "), "あさa")
        self.assertEqual(character_error_rate("あさ", "アサ"), 0)
        self.assertEqual(character_error_rate("あさ", ""), 1)
        self.assertEqual(character_error_rate("あさ", "朝"), 1)
        self.assertGreater(character_error_rate("あ", "あさのそら"), 1)
        with self.assertRaises(ValueError):
            character_error_rate("...", "")

    def test_invalid_or_unused_references_cannot_silently_skip_comparison(self):
        for expected in ([], {"missing": "あさ"}, {"song": ""}, {"song": "."}, {"song": 12}):
            with self.assertRaises(ValueError):
                screen_packet(self.manifest, self.root, FakeRecognizer([]), expected)

    def test_bad_wave_geometry_rejects_before_audio_decode(self):
        validate_wave(self.audio)
        variants = [b"not audio", self.audio[:-1]]
        compressed = bytearray(self.audio)
        struct.pack_into("<H", compressed, 20, 7)  # compressed mu-law is outside this corpus
        variants.append(compressed)
        invalid_alignment = bytearray(self.audio)
        struct.pack_into("<H", invalid_alignment, 32, 0)
        variants.append(invalid_alignment)
        invalid_rate = bytearray(self.audio)
        struct.pack_into("<I", invalid_rate, 24, 0)
        variants.append(invalid_rate)
        for payload in variants:
            with self.assertRaises(ValueError):
                validate_wave(payload)


if __name__ == "__main__":
    unittest.main()
