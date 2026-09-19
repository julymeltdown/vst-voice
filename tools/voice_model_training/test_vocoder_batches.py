import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import wave
from tools.voice_model_training.vocoder_batches import segment_frame_ranges


class SegmentGeometryTests(unittest.TestCase):
    def test_balanced_complete_nonoverlapping_ownership(self):
        for frames in (2, 15, 16, 17, 127, 128, 129, 1080, 4096):
            for maximum in (16, 128, 4096):
                parts = list(segment_frame_ranges(frames, maximum))
                self.assertEqual(parts[0][0], 0)
                self.assertEqual(parts[-1][1], frames)
                self.assertTrue(all(2 <= end - begin <= maximum for begin, end in parts))
                self.assertTrue(all(a[1] == b[0] for a, b in zip(parts, parts[1:])))
                self.assertEqual(sum(end - begin for begin, end in parts), frames)
        self.assertEqual(list(segment_frame_ranges(129, 128)), [(0, 65), (65, 129)])
        for frames, maximum in ((1, 128), (4097, 128), (True, 128), (10, 15), (10, True)):
            with self.assertRaises(ValueError): list(segment_frame_ranges(frames, maximum))


@unittest.skipUnless(importlib.util.find_spec("torch"), "Optional Torch environment required")
class VocoderBatchTailTests(unittest.TestCase):
    def test_segmented_tensors_preserve_all_samples_and_partial_tail(self):
        import numpy as np
        from tools.voice_model_training.audio_source import inspect_pcm_source
        from tools.voice_model_training.vocoder_batches import iter_vocoder_batches
        count = 129 * 256 - 13
        buffer = io.BytesIO()
        with wave.open(buffer, "wb") as writer:
            writer.setnchannels(1); writer.setsampwidth(2); writer.setframerate(48000)
            writer.writeframes(np.full(count, 8192, dtype="<i2").tobytes())
        payload = buffer.getvalue()
        source = inspect_pcm_source(payload, expected_sha256=hashlib.sha256(payload).hexdigest(), sample_rate=48000)
        snapshot = dict(sources=[dict(source, sourceId="s")])
        batch = dict(sourceId="s", datasetSha256="d", targetSha256="t", frameOffset=0, hopSize=256,
                     phraseAnalysisFrames=129, columns=dict(f0Hz=[220.] * 129, validSamples=[256] * 128 + [243]),
                     melTargets=np.arange(129 * 80, dtype=np.float32).reshape(129, 80))
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "s.wav"; path.write_bytes(payload)
            targets = {"s": (dict(profile=dict(tailPadding="zero-to-whole-hop", amplitudeScale="ln-amplitude")), None)}
            with patch("tools.voice_model_training.vocoder_batches.iter_supervised_batches", return_value=iter([batch])):
                parts = list(iter_vocoder_batches(snapshot, Path(root), targets, {"s": path},
                    expected_profile_sha256="p", partition="train", batch_frames=4096, training_segment_frames=128))
        self.assertEqual([p["frameOffset"] for p in parts], [0, 65])
        self.assertEqual(sum(p["validSamples"] for p in parts), count)
        self.assertEqual([p["paddedSamples"] for p in parts], [0, 13])
        merged = np.concatenate([p["pcm"].numpy().ravel() for p in parts])
        np.testing.assert_array_equal(merged[:count], np.full(count, .25))
        np.testing.assert_array_equal(merged[count:], np.zeros(13))
        np.testing.assert_array_equal(np.concatenate([p["mel"].numpy()[0].T for p in parts]), batch["melTargets"])
        parts[0]["mel"].zero_()
        self.assertNotEqual(float(parts[1]["mel"].sum()), 0.)

    def test_partial_hop_is_explicit_and_owned(self):
        import numpy as np
        from tools.voice_model_training.audio_source import inspect_pcm_source
        from tools.voice_model_training.vocoder_batches import iter_vocoder_batches
        buffer = io.BytesIO()
        with wave.open(buffer, "wb") as writer:
            writer.setnchannels(1)
            writer.setsampwidth(2)
            writer.setframerate(48000)
            writer.writeframes(np.full(500, 16384, dtype="<i2").tobytes())
        payload = buffer.getvalue()
        digest = hashlib.sha256(payload).hexdigest()
        source = inspect_pcm_source(payload, expected_sha256=digest, sample_rate=48000)
        snapshot = dict(sources=[dict(source, sourceId="fixture")])
        batch = dict(sourceId="fixture", datasetSha256="fixture", targetSha256="fixture",
                     frameOffset=0, hopSize=256, phraseAnalysisFrames=2,
                     columns=dict(f0Hz=[220, 0], validSamples=[256, 244]),
                     melTargets=np.zeros((2, 80), dtype=np.float32))
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "source.wav"
            path.write_bytes(payload)
            targets = {"fixture": (dict(profile=dict(tailPadding="zero-to-whole-hop", amplitudeScale="ln-amplitude")), None)}
            # Upstream admission/join is covered by the real acoustic integration
            # test; isolate only this reader's partial-hop and ownership behavior.
            with patch("tools.voice_model_training.vocoder_batches.iter_supervised_batches", return_value=iter([batch])):
                result = list(iter_vocoder_batches(snapshot, Path(root), targets, {"fixture": path},
                    expected_profile_sha256="fixture", partition="test"))[0]
            self.assertEqual(result["validSamples"], 500)
            self.assertEqual(result["paddedSamples"], 12)
            np.testing.assert_array_equal(result["pcm"].numpy().ravel()[:500], np.full(500, .5))
            np.testing.assert_array_equal(result["pcm"].numpy().ravel()[500:], np.zeros(12))
            batch["melTargets"][:] = 4
            self.assertEqual(float(result["mel"].sum()), 0)
            self.assertFalse(result["trainingAdmitted"])


if __name__ == "__main__":
    unittest.main()
