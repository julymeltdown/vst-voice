import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from tools.voice_model_training.__main__ import segment_batch_command
from tools.voice_model_training.test_audio_source import wav
from tools.voice_model_training.split import split_sources


class BatchTests(unittest.TestCase):
    def test_partial_failure_resume_and_report_preservation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            audio = wav()
            (root / "source.wav").write_bytes(audio)
            config = dict(formatId="com.project-seam.voice-training-segment-config", schemaVersion=1,
                source=dict(sourceId="parent", songId="song", sessionId="session", lineageId="lineage",
                            sourceSha256=hashlib.sha256(audio).hexdigest()),
                sampleRate=48000, segmentId="child", startFrame=0, endFrame=16)
            payload = json.dumps(config).encode()
            (root / "first.json").write_bytes(payload)
            entry = dict(configuration="first.json", configurationSha256=hashlib.sha256(payload).hexdigest(),
                         source="source.wav", outputName="first")
            config["segmentId"] = "second"
            second = json.dumps(config).encode()
            rows = [entry, dict(entry, configuration="second.json", outputName="second",
                                configurationSha256=hashlib.sha256(second).hexdigest())]
            batch = json.dumps(dict(formatId="com.project-seam.voice-training-segment-batch", schemaVersion=1, entries=rows)).encode()
            (root / "batch.json").write_bytes(batch)
            def run(report, resume=False):
                return segment_batch_command(root / "batch.json", hashlib.sha256(batch).hexdigest(), root,
                                             root / "clips", root / report, resume=resume)
            self.assertEqual(run("attempt1.json"), 3)
            first = (root / "clips/first/segment.json").read_bytes()
            report = (root / "attempt1.json").read_bytes()
            self.assertEqual(json.loads(report)["rejectedCount"], 1)
            self.assertEqual(json.loads(report)["splitSources"], [])
            (root / "second.json").write_bytes(second)
            self.assertEqual(run("attempt2.json", True), 0)
            self.assertEqual((root / "clips/first/segment.json").read_bytes(), first)
            self.assertEqual((root / "attempt1.json").read_bytes(), report)
            with self.assertRaises(ValueError): run("attempt1.json", True)
            self.assertFalse(json.loads((root / "attempt2.json").read_bytes())["trainingAdmitted"])
            ready = json.loads((root / "attempt2.json").read_bytes())
            self.assertEqual(ready["schemaVersion"], 2)
            split = split_sources(ready["splitSources"], seed="pilot", held_out_songs=["song"])
            self.assertEqual(split["counts"]["test"], 2)
            self.assertEqual(split["redundantSourceCount"], 1)
            rows[1] = dict(entry, outputName="duplicate")
            batch = json.dumps(dict(formatId="com.project-seam.voice-training-segment-batch", schemaVersion=1, entries=rows)).encode()
            (root / "batch.json").write_bytes(batch)
            self.assertEqual(run("duplicate.json", True), 3)
            duplicate = json.loads((root / "duplicate.json").read_bytes())
            self.assertEqual(duplicate["splitSources"], [])
            self.assertEqual(duplicate["duplicateSourceIds"], [dict(sourceId="child", outputs=["duplicate", "first"])])


if __name__ == "__main__":
    unittest.main()
