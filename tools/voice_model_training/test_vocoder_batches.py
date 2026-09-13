import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import wave


@unittest.skipUnless(importlib.util.find_spec("torch"), "Optional Torch environment required")
class VocoderBatchTailTests(unittest.TestCase):
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
