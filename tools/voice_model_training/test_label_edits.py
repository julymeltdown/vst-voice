import copy
import unittest
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from tools.voice_model_training.label_edits import apply_label_edits
from tools.voice_model_training.test_audio_source import wav


class LabelEditTests(unittest.TestCase):
    def test_source_bound_correction_cli(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            audio = wav(); (root / "source.wav").write_bytes(audio)
            label = dict(sourceId="source", frameCount=32, hopSize=256, f0Hz=[220], voiced=[True],
                         phonemes=[dict(symbol="a", startFrame=0, endFrame=32, confidence=1)], reviewRevision="old")
            edit = dict(kind="pitch", index=0, expected=dict(f0Hz=220, voiced=True), replacement=dict(f0Hz=221, voiced=True))
            config = dict(formatId="com.project-seam.training-label-correction-config", schemaVersion=1,
                source=dict(sourceId="source", songId="song", sessionId="session", lineageId="lineage",
                            path="source.wav", sourceSha256=hashlib.sha256(audio).hexdigest()),
                sampleRate=48000, label=label, edits=[edit], vocabulary=["a"], minimumConfidence=0.8)
            def run(name):
                payload = json.dumps(config).encode(); (root / "config.json").write_bytes(payload)
                return subprocess.run([sys.executable, "-m", "tools.voice_model_training", "correct-labels",
                    str(root / "config.json"), hashlib.sha256(payload).hexdigest(), str(root), str(root / name)],
                    capture_output=True, timeout=10)
            self.assertEqual(run("corrected.json").returncode, 0)
            original = (root / "corrected.json").read_bytes()
            report = json.loads(original)
            self.assertEqual(report["label"]["f0Hz"], [221])
            self.assertNotEqual(report["parentLabelSha256"], report["labelSha256"])
            self.assertFalse(report["trainingAdmitted"])
            self.assertEqual(run("corrected.json").returncode, 2)
            self.assertEqual((root / "corrected.json").read_bytes(), original)
            edit["expected"]["f0Hz"] = 222
            self.assertEqual(run("stale.json").returncode, 2)
            self.assertFalse((root / "stale.json").exists())

    def test_pitch_and_shared_boundary_transaction(self):
        label = dict(sourceId="source", frameCount=512, hopSize=256, f0Hz=[220, 0], voiced=[True, False],
            reviewRevision="old", phonemes=[dict(symbol="a", startFrame=0, endFrame=256, confidence=0.7),
                                            dict(symbol="i", startFrame=256, endFrame=512, confidence=0.9)])
        original = copy.deepcopy(label)
        edits = [dict(kind="pitch", index=1, expected=dict(f0Hz=0, voiced=False), replacement=dict(f0Hz=230, voiced=True)),
            dict(kind="phoneme", index=0, expected=label["phonemes"][0], replacement=dict(label["phonemes"][0], endFrame=300)),
            dict(kind="phoneme", index=1, expected=label["phonemes"][1], replacement=dict(label["phonemes"][1], startFrame=300))]
        def apply(operations):
            return apply_label_edits(label, operations, vocabulary={"a", "i"}, minimum_confidence=0.8)
        result = apply(edits)
        self.assertEqual(result["f0Hz"], [220, 230])
        self.assertEqual(result["phonemes"][1]["startFrame"], 300)
        self.assertIsNone(result["reviewRevision"])
        self.assertEqual(label, original)
        for operations in (edits[:2], edits + [edits[0]], [dict(edits[0], expected=dict(f0Hz=10, voiced=False))],
                           [dict(edits[0], replacement=dict(f0Hz=230, voiced=False))]):
            with self.assertRaises(ValueError): apply(operations)
            self.assertEqual(label, original)


if __name__ == "__main__":
    unittest.main()
