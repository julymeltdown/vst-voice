import importlib.util
import unittest
import hashlib
import io
import wave
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from tools.voice_model_training.acoustics import log_mel_targets, wav_log_mel_targets


@unittest.skipUnless(importlib.util.find_spec("numpy"), "Optional acoustic-target NumPy environment not installed")
class AcousticTargetsTests(unittest.TestCase):
    def test_byte_bound_pcm_widths_and_sign_extension(self):
        import numpy as np
        results = []
        for width in (2, 3, 4):
            # Exact cross-width values exercise negative full-scale and 24-bit sign extension.
            integers = [-(1 << (8 * width - 1)), 0, 1 << (8 * width - 2), 0] * 512
            raw = b"".join(value.to_bytes(width, "little", signed=True) for value in integers)
            output = io.BytesIO()
            with wave.open(output, "wb") as writer:
                writer.setnchannels(1)
                writer.setsampwidth(width)
                writer.setframerate(48000)
                writer.writeframes(raw)
            payload = output.getvalue()
            digest = hashlib.sha256(payload).hexdigest()
            record, targets = wav_log_mel_targets(payload, expected_sha256=digest, sample_rate=48000)
            self.assertEqual(record["sourceSha256"], digest)
            self.assertEqual(record["sourceFrameCount"], 2048)
            self.assertEqual(record["analysisFrameCount"], 8)
            self.assertEqual(record["targetSha256"], hashlib.sha256(targets.astype("<f4").tobytes()).hexdigest())
            self.assertFalse(record["trainingAdmitted"])
            results.append(targets)
            if width == 2:
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    (root / "source.wav").write_bytes(payload)
                    config = dict(formatId="com.project-seam.training-acoustic-config", schemaVersion=1,
                                  sourceSha256=digest, sampleRate=48000, fftSize=1024, hopSize=256,
                                  bins=80, minimumHz=20, maximumHz=24000)
                    config_bytes = json.dumps(config).encode()
                    (root / "config.json").write_bytes(config_bytes)
                    command = [sys.executable, "-m", "tools.voice_model_training", "acoustic-targets",
                               str(root / "config.json"), hashlib.sha256(config_bytes).hexdigest(),
                               str(root / "source.wav"), str(root / "targets")]
                    result = subprocess.run(command, capture_output=True, timeout=15)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    published = json.loads((root / "targets/target.json").read_bytes())
                    target_bytes = (root / "targets/mel.f32le").read_bytes()
                    self.assertEqual(target_bytes, targets.astype("<f4").tobytes())
                    self.assertEqual(published["targetSha256"], hashlib.sha256(target_bytes).hexdigest())
                    self.assertFalse(published["trainingAdmitted"])
                    from tools.voice_model_training.batches import iter_supervised_batches, _encoded
                    from tools.voice_model_training.conditioning import build_conditioning
                    from tools.voice_model_training.split import split_sources
                    label = dict(sourceId="s", frameCount=2048, hopSize=256,
                        phonemes=[dict(symbol="a", startFrame=0, endFrame=2048, confidence=1)],
                        f0Hz=[220] * 8, voiced=[True] * 8, reviewRevision=None)
                    score = dict(language="en", silencePhones=[], syllables=[dict(lyric="ah", phoneStart=0, phoneEnd=1)],
                        notes=[dict(startFrame=0, endFrame=2048, midi=57, syllable=0, slur=False)])
                    features = build_conditioning(label, score, vocabulary=["a"], minimum_confidence=.8)
                    feature_bytes = _encoded(features)
                    (root / "features").mkdir()
                    (root / "features/phrase-000000.json").write_bytes(feature_bytes)
                    ref = dict(sourceId="s", path="phrase-000000.json", sizeBytes=len(feature_bytes),
                               frameCount=8, sha256=hashlib.sha256(feature_bytes).hexdigest())
                    row = dict(sourceId="s", songId="song", sessionId="session", lineageId="lineage", audioSha256=record["audioSha256"])
                    split = split_sources([row], seed="test", held_out_songs=["song"])
                    bindings = dict(split=split, conditioningSha256=hashlib.sha256(_encoded([ref])).hexdigest())
                    snapshot = dict(formatId="com.project-seam.training-dataset-snapshot", schemaVersion=3,
                        bindings=bindings, datasetSha256=hashlib.sha256(_encoded(bindings)).hexdigest(),
                        conditioning=[ref], vocabulary=["a"], labels=[dict(label=label, score=score)],
                        sources=[dict(row, sourceSha256=digest, frameCount=2048, sampleRate=48000)])
                    target_map = {"s": (published, root / "targets/mel.f32le")}
                    def batches(partition="test", context_frames=0):
                        return list(iter_supervised_batches(snapshot, root / "features", target_map,
                            expected_profile_sha256=published["profileSha256"], partition=partition, batch_frames=3,
                            context_frames=context_frames))
                    joined = batches()
                    self.assertEqual([b["melTargets"].shape for b in joined], [(3, 80), (3, 80), (2, 80)])
                    np.testing.assert_array_equal(np.concatenate([b["melTargets"] for b in joined]), targets)
                    contextual = batches(context_frames=1)
                    self.assertEqual([b["frameOffset"] for b in contextual], [0, 2, 5])
                    self.assertEqual([b["coreFrameOffset"] for b in contextual], [0, 3, 6])
                    self.assertEqual([b["melTargets"].shape[0] for b in contextual], [4, 5, 3])
                    np.testing.assert_array_equal(np.concatenate([b["melTargets"][b["lossMask"]] for b in contextual]), targets)
                    self.assertEqual(sum(sum(b["lossMask"]) for b in contextual), 8)
                    self.assertEqual([b["tokens"] for b in contextual], [[1], [1], [1]])
                    self.assertEqual([b["mel2ph"] for b in contextual], [[1] * 4, [1] * 5, [1] * 3])
                    with self.assertRaises(ValueError): batches(context_frames=4096)
                    self.assertEqual(batches("train"), [])
                    published["sourceSha256"] = "0" * 64
                    with self.assertRaises(ValueError): batches()
                    published["sourceSha256"] = digest
                    binary_path = root / "targets/mel.f32le"
                    binary_path.write_bytes(bytes([target_bytes[0] ^ 1]) + target_bytes[1:])
                    with self.assertRaises(ValueError): batches()
                    binary_path.write_bytes(target_bytes)
                    self.assertEqual(subprocess.run(command, capture_output=True, timeout=15).returncode, 2)
                    self.assertEqual((root / "targets/mel.f32le").read_bytes(), target_bytes)
                    command[-1] = str(root / "bad-targets")
                    (root / "source.wav").write_bytes(payload[:-1])
                    self.assertEqual(subprocess.run(command, capture_output=True, timeout=15).returncode, 2)
                    self.assertFalse((root / "bad-targets").exists())
            with self.assertRaises(ValueError):
                wav_log_mel_targets(payload, expected_sha256="0" * 64, sample_rate=48000)
            with self.assertRaises(ValueError):
                wav_log_mel_targets(payload, expected_sha256=digest, sample_rate=44100)
        for targets in results[1:]:
            np.testing.assert_array_equal(targets, results[0])

    def test_silence_partial_hop_and_amplitude_scaling(self):
        import numpy as np
        zeros = log_mel_targets(np.zeros(2049), sample_rate=48000)
        self.assertEqual(zeros.shape, (9, 80))
        np.testing.assert_allclose(zeros, np.log(1e-5), atol=1e-6)
        signal = .2 * np.sin(2 * np.pi * 440 * np.arange(4096) / 48000)
        first = log_mel_targets(signal, sample_rate=48000)
        second = log_mel_targets(signal / 2, sample_rate=48000)
        active = second > np.log(1e-5) + .1
        self.assertTrue(active.any())
        np.testing.assert_allclose((first - second)[active], np.log(2), atol=2e-6)
        self.assertEqual(first.dtype, np.float32)
        self.assertTrue(np.isfinite(first).all())
        for invalid in (np.zeros(1), np.array([np.nan] * 2048), np.ones(2048) * 2, np.zeros((2, 2048))):
            with self.assertRaises(ValueError): log_mel_targets(invalid, sample_rate=48000)


if __name__ == "__main__":
    unittest.main()
