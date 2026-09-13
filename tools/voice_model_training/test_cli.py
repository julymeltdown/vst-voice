import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from tools.voice_model_training.test_split import source
from tools.voice_model_training.test_audio_source import wav


class CliTests(unittest.TestCase):
    def test_segment_artifact_command(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            audio = wav()
            original = root / "original.wav"
            original.write_bytes(audio)
            value = dict(formatId="com.project-seam.voice-training-segment-config", schemaVersion=1,
                source=dict(sourceId="original", songId="song", sessionId="session", lineageId="family",
                            sourceSha256=hashlib.sha256(audio).hexdigest()),
                sampleRate=48000, segmentId="phrase", startFrame=2, endFrame=20)
            def run(output, resume=False):
                payload = json.dumps(value).encode()
                config = root / "config.json"
                config.write_bytes(payload)
                return subprocess.run([sys.executable, "-m", "tools.voice_model_training", "segment",
                    str(config), hashlib.sha256(payload).hexdigest(), str(original), str(root / output)] + (["--resume"] if resume else []),
                    capture_output=True, timeout=10)
            self.assertEqual(run("phrase").returncode, 0)
            record = json.loads((root / "phrase/segment.json").read_bytes())
            clip = (root / "phrase/audio.wav").read_bytes()
            self.assertEqual(record["sourceSha256"], hashlib.sha256(clip).hexdigest())
            self.assertEqual(record["frameCount"], 18)
            self.assertEqual(record["lineageId"], "family")
            self.assertEqual(run("phrase").returncode, 2)
            self.assertEqual((root / "phrase/audio.wav").read_bytes(), clip)
            manifest = (root / "phrase/segment.json").read_bytes()
            self.assertEqual(run("phrase", True).returncode, 0)
            self.assertEqual((root / "phrase/segment.json").read_bytes(), manifest)
            (root / "recoverable").mkdir()
            (root / "recoverable/audio.wav").write_bytes(clip)
            self.assertEqual(run("recoverable", True).returncode, 0)
            self.assertEqual((root / "recoverable/segment.json").read_bytes(), manifest)
            (root / "damaged").mkdir()
            (root / "damaged/audio.wav").write_bytes(clip[:-1])
            self.assertEqual(run("damaged", True).returncode, 2)
            self.assertFalse((root / "damaged/segment.json").exists())
            self.assertEqual((root / "damaged/audio.wav").read_bytes(), clip[:-1])
            (root / "conflict").mkdir()
            (root / "conflict/audio.wav").write_bytes(clip)
            (root / "conflict/segment.json").write_bytes(b"{}")
            self.assertEqual(run("conflict", True).returncode, 2)
            self.assertEqual((root / "conflict/segment.json").read_bytes(), b"{}")
            (root / "linked").symlink_to(root / "phrase", target_is_directory=True)
            self.assertEqual(run("linked", True).returncode, 2)
            (root / "partial").mkdir()
            self.assertEqual(run("partial").returncode, 2)
            self.assertFalse((root / "partial/segment.json").exists())
            value["endFrame"] = 33
            self.assertEqual(run("invalid").returncode, 2)
            self.assertFalse((root / "invalid").exists())
            self.assertEqual(original.read_bytes(), audio)
            from tools.voice_model_training.audio_source import inspect_pcm_source
            parent = inspect_pcm_source(audio, expected_sha256=value["source"]["sourceSha256"], sample_rate=48000)
            value.update(schemaVersion=2, startFrame=8, endFrame=25, vocabulary=["a"], minimumConfidence=0.8,
                parentLabel=dict(sourceSha256=parent["sourceSha256"], audioSha256=parent["audioSha256"],
                    label=dict(sourceId="original", frameCount=32, hopSize=8,
                        phonemes=[dict(symbol="a", startFrame=0, endFrame=32, confidence=1)],
                        f0Hz=[220] * 4, voiced=[True] * 4, reviewRevision="review")))
            self.assertEqual(run("labeled").returncode, 0)
            labeled = json.loads((root / "labeled/segment.json").read_bytes())
            self.assertEqual(labeled["label"]["label"]["frameCount"], 17)
            self.assertIsNone(labeled["label"]["label"]["reviewRevision"])
            value.update(schemaVersion=3, parentScore=dict(language="en", silencePhones=[],
                syllables=[dict(lyric="ah", phoneStart=0, phoneEnd=1)],
                notes=[dict(startFrame=0, endFrame=32, midi=57, syllable=0, slur=False)]))
            self.assertEqual(run("scored").returncode, 0)
            scored = json.loads((root / "scored/segment.json").read_bytes())
            self.assertEqual(scored["label"]["score"]["notes"][0]["endFrame"], 17)
            value["parentLabel"]["audioSha256"] = "0" * 64
            self.assertEqual(run("wrong-label").returncode, 2)
            self.assertFalse((root / "wrong-label").exists())

    def test_source_bound_labels_and_correction_exit(self):
        from tools.voice_model_training.audio_source import inspect_pcm_source
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            audio = wav()
            (root / "source.wav").write_bytes(audio)
            digest = hashlib.sha256(audio).hexdigest()
            inspected = inspect_pcm_source(audio, expected_sha256=digest, sample_rate=48000)
            frames = inspected["frameCount"]
            label = dict(sourceId="s", frameCount=frames, hopSize=256,
                         phonemes=[dict(symbol="a", startFrame=0, endFrame=frames, confidence=1)],
                         f0Hz=[220] * ((frames + 255) // 256), voiced=[True] * ((frames + 255) // 256),
                         reviewRevision="supplied")
            bound = dict(sourceSha256=digest, audioSha256=inspected["audioSha256"], label=label)
            value = dict(formatId="com.project-seam.voice-training-label-config", schemaVersion=1,
                         sampleRate=48000, sources=[dict(sourceId="s", songId="song", sessionId="session",
                         lineageId="lineage", path="source.wav", sourceSha256=digest)],
                         labels=[bound], vocabulary=["a"], minimumConfidence=0.8)
            def run(name):
                payload = json.dumps(value).encode()
                (root / "config.json").write_bytes(payload)
                return subprocess.run([sys.executable, "-m", "tools.voice_model_training", "labels",
                    str(root / "config.json"), hashlib.sha256(payload).hexdigest(), str(root), str(root / name)],
                    capture_output=True, timeout=10)
            self.assertEqual(run("ok.json").returncode, 0)
            result = json.loads((root / "ok.json").read_bytes())
            self.assertFalse(result["trainingAdmitted"])
            self.assertEqual(result["sources"][0]["audioSha256"], inspected["audioSha256"])
            before = (root / "ok.json").read_bytes()
            self.assertEqual(run("ok.json").returncode, 2)
            self.assertEqual((root / "ok.json").read_bytes(), before)
            label["reviewRevision"] = None
            self.assertEqual(run("queue.json").returncode, 3)
            bound["audioSha256"] = "0" * 64
            self.assertEqual(run("wrong.json").returncode, 2)
            self.assertFalse((root / "wrong.json").exists())
            self.assertEqual((root / "source.wav").read_bytes(), audio)
            bound["audioSha256"] = inspected["audioSha256"]
            label["reviewRevision"] = "supplied"
            value["schemaVersion"] = 2
            bound["score"] = dict(language="en", syllables=[dict(lyric="ah", phoneStart=0, phoneEnd=1)],
                notes=[dict(startFrame=0, endFrame=frames, midi=57, syllable=0, slur=False)])
            self.assertEqual(run("score.json").returncode, 0)
            scored = json.loads((root / "score.json").read_bytes())
            self.assertEqual(scored["schemaVersion"], 2)
            self.assertEqual(scored["sources"][0]["scoreSupervision"]["noteCount"], 1)
            bound["score"]["notes"][0]["slur"] = True
            self.assertEqual(run("bad-score.json").returncode, 2)
            self.assertFalse((root / "bad-score.json").exists())
            bound["score"]["notes"][0]["slur"] = False
            value["schemaVersion"] = 3
            bound["score"]["silencePhones"] = []
            self.assertEqual(run("silence.json").returncode, 0)
            silence = json.loads((root / "silence.json").read_bytes())
            self.assertEqual(silence["schemaVersion"], 3)
            self.assertEqual(silence["sources"][0]["scoreSupervision"]["silencePhoneCount"], 0)

    def test_prepare_command_publishes_diagnostics_and_verified_inventory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            audio = wav()
            (root / "source.wav").write_bytes(audio)
            row = dict(sourceId="s", songId="song", sessionId="session", lineageId="lineage",
                       path="source.wav", sourceSha256=hashlib.sha256(audio).hexdigest())
            config = root / "prepare.json"
            def run(records, name):
                payload = json.dumps(dict(formatId="com.project-seam.voice-training-prepare-config",
                    schemaVersion=1, sampleRate=48000, sources=records)).encode()
                config.write_bytes(payload)
                command = [sys.executable, "-m", "tools.voice_model_training", "prepare", str(config),
                           hashlib.sha256(payload).hexdigest(), str(root), str(root / name)]
                return subprocess.run(command, capture_output=True, timeout=10)
            self.assertEqual(run([row], "ready.json").returncode, 0)
            ready = json.loads((root / "ready.json").read_bytes())
            self.assertEqual(len(ready["splitSources"]), 1)
            self.assertFalse(ready["sourceRightsAdmitted"])
            self.assertEqual(run([dict(row, path="missing.wav")], "rejected.json").returncode, 3)
            rejected = json.loads((root / "rejected.json").read_bytes())
            self.assertEqual(rejected["rejectedCount"], 1)
            self.assertEqual(rejected["splitSources"], [])
            before = (root / "ready.json").read_bytes()
            self.assertEqual(run([row], "ready.json").returncode, 2)
            self.assertEqual((root / "ready.json").read_bytes(), before)

    def test_captured_config_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payload = json.dumps(dict(formatId="com.project-seam.voice-training-split-config", schemaVersion=1,
                                      seed="pilot", heldOutSongIds=["song-0"], sources=[source(i) for i in range(20)])).encode()
            config, output = root / "config.json", root / "split.json"
            config.write_bytes(payload)
            digest = hashlib.sha256(payload).hexdigest()
            command = [sys.executable, "-m", "tools.voice_model_training", "split", str(config), digest, str(output)]
            def run(args):
                return subprocess.run(args, capture_output=True, timeout=10)
            wrong = command.copy(); wrong[-2] = "0" * 64
            self.assertEqual(run(wrong).returncode, 2)
            self.assertFalse(output.exists())
            self.assertEqual(run(command).returncode, 0)
            original = output.read_bytes()
            self.assertEqual(json.loads(original)["configurationSha256"], digest)
            self.assertEqual(run(command).returncode, 2)
            self.assertEqual(output.read_bytes(), original)
            self.assertEqual(config.read_bytes(), payload)
            self.assertEqual(list(root.glob(".seam-split-*")), [])
            command[-1] = str(root / "second.json")
            self.assertEqual(run(command).returncode, 0)
            self.assertEqual((root / "second.json").read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
