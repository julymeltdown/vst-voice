from __future__ import annotations

import copy
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import wave
from unittest.mock import patch

from tools.voicebank_source_intake.prepare_sustain_cuts import prepare


class SustainIntakeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.audio = self.root / "take.wav"
        self.rate, self.frames = 8000, 32768
        pcm = b"".join(struct.pack("<h", round(8000 * math.sin(2 * math.pi * 440 * i / self.rate)))
                       for i in range(self.frames))
        with wave.open(str(self.audio), "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(self.rate)
            wav.writeframes(pcm)
        self.original_pcm = pcm
        self.analysis = {"source": str(self.audio), "sampleRate": self.rate, "channels": 1,
                         "frames": self.frames,
                         "pitchFrames": [{"sourceFrame": i, "f0Hz": 440.0, "confidence": 0.99,
                                          "voiced": True}
                                         for i in range(0, self.frames - 2048 + 1, 256)]}
        self.analysis_path = self.root / "analysis.json"
        self.legal = self.root / "license.txt"
        self.legal.write_text("Synthetic test license; this is no provider approval.\n")
        self.plan = {"schemaVersion": 1,
                     "analysisContract": {"command": "seam_voicebank_cli analyze", "windowFrames": 2048,
                                          "hopFrames": 256, "coverage": "CompleteWindows"},
                     "license": {**self.binding(self.legal), "sourceUrl": "https://example.invalid/license",
                                 "attribution": "Synthetic test fixture"},
                     "sources": [{"id": "fixture-a", "sourceVowel": "a", "sourceTechnique": "straight",
                                  "audio": self.binding(self.audio)}]}
        self.plan_path = self.root / "plan.json"
        self.save()

    def binding(self, path: Path) -> dict:
        return {"path": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}

    def save(self) -> None:
        self.analysis_path.write_text(json.dumps(self.analysis))
        self.plan["sources"][0]["analysis"] = self.binding(self.analysis_path)
        self.plan_path.write_text(json.dumps(self.plan))

    def test_exact_pcm_intervals_and_deterministic_unapproved_outputs(self) -> None:
        initial = self.audio.read_bytes()
        first = prepare(self.plan_path, self.root / "one")
        prepare(self.plan_path, self.root / "two")
        self.assertEqual(initial, self.audio.read_bytes())
        self.assertEqual("UNAPPROVED", first["status"])
        self.assertEqual("NOT_APPROVED", first["bankAdmission"])
        self.assertNotIn("inputRoot", first)
        self.assertNotIn(str(self.root), json.dumps(first))
        self.assertEqual(1, len(first["cuts"]))
        for cut in first["cuts"]:
            self.assertEqual("NOT_ASSIGNED", cut["assignment"])
            self.assertEqual("NOT_REVIEWED", cut["pitchReview"])
            with wave.open(str(self.root / "one" / cut["derivedPath"]), "rb") as wav:
                self.assertEqual(self.rate, wav.getframerate())
                self.assertEqual(self.original_pcm[cut["startFrame"] * 2:cut["endFrameExclusive"] * 2],
                                 wav.readframes(wav.getnframes()))
        for path in (self.root / "one").iterdir():
            self.assertEqual(path.read_bytes(), (self.root / "two" / path.name).read_bytes())
        self.assertFalse((self.root / "one" / "INCOMPLETE").exists())

    def test_low_confidence_gap_splits_same_pitch_without_bridging(self) -> None:
        for frame in self.analysis["pitchFrames"]:
            if 14000 <= frame["sourceFrame"] <= 17000:
                frame["confidence"] = 0.1
        self.save()
        result = prepare(self.plan_path, self.root / "cuts")
        self.assertEqual(1, len(result["cuts"]))
        cut = result["cuts"][0]
        self.assertTrue(cut["endFrameExclusive"] < 14000 or cut["startFrame"] > 17000)

    def test_observed_pitches_are_kept_separate(self) -> None:
        for frame in self.analysis["pitchFrames"]:
            if frame["sourceFrame"] >= 16000:
                frame["f0Hz"] = 523.2511306
        self.save()
        result = prepare(self.plan_path, self.root / "cuts")
        self.assertEqual([69, 72], [c["observedNearestMidi"] for c in result["cuts"]])

    def test_too_short_low_rate_run_does_not_discard_a_later_valid_run(self) -> None:
        for index, frame in enumerate(self.analysis["pitchFrames"]):
            if index == 16:
                frame["confidence"] = 0.1
        self.save()
        result = prepare(self.plan_path, self.root / "cuts")
        self.assertEqual(1, len(result["cuts"]))
        self.assertGreater(result["cuts"][0]["startFrame"], 16 * 256)

    def test_refuses_stale_analysis_and_bad_frame_grid_before_writing(self) -> None:
        for change in ("source", "extent", "order", "nonfinite", "contract"):
            with self.subTest(change=change):
                original_analysis, original_plan = copy.deepcopy(self.analysis), copy.deepcopy(self.plan)
                if change == "source":
                    self.analysis["source"] = str(self.root / "other.wav")
                elif change == "extent":
                    self.analysis["frames"] += 1
                elif change == "order":
                    self.analysis["pitchFrames"][2]["sourceFrame"] = 0
                elif change == "nonfinite":
                    self.analysis["pitchFrames"][2]["f0Hz"] = float("nan")
                else:
                    self.plan["analysisContract"]["windowFrames"] = 1024
                self.save()
                target = self.root / change
                expected = {"source": "Analysis source identity mismatch",
                            "extent": "Analysis/audio extent or format mismatch",
                            "order": "Invalid analysis frame order or extent",
                            "nonfinite": "Non-finite JSON constant",
                            "contract": "Unsupported analysis contract"}[change]
                with self.assertRaisesRegex(ValueError, expected):
                    prepare(self.plan_path, target)
                self.assertFalse(target.exists())
                self.analysis, self.plan = original_analysis, original_plan

    def test_refuses_hash_changes_and_does_not_overwrite_foreign_output(self) -> None:
        self.legal.write_text("Changed license bytes\n")
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            prepare(self.plan_path, self.root / "cuts")
        self.assertFalse((self.root / "cuts").exists())
        foreign = self.root / "foreign"
        foreign.mkdir()
        sentinel = foreign / "keep"
        sentinel.write_bytes(b"owner bytes")
        with self.assertRaisesRegex(ValueError, "Output already exists"):
            prepare(self.plan_path, foreign)
        self.assertEqual(b"owner bytes", sentinel.read_bytes())

    def test_refuses_traversal_symlinks_duplicates_and_unstable_material(self) -> None:
        for change in ("traversal", "symlink", "duplicate", "unstable"):
            with self.subTest(change=change):
                original_analysis, original_plan = copy.deepcopy(self.analysis), copy.deepcopy(self.plan)
                if change == "traversal":
                    self.plan["sources"][0]["audio"]["path"] = "../take.wav"
                elif change == "symlink":
                    link = self.root / "linked.wav"
                    link.symlink_to(self.audio)
                    self.plan["sources"][0]["audio"]["path"] = link.name
                elif change == "duplicate":
                    self.plan["sources"].append(copy.deepcopy(self.plan["sources"][0]))
                else:
                    for index, frame in enumerate(self.analysis["pitchFrames"]):
                        if index % 2:
                            frame["confidence"] = 0.1
                self.save()
                target = self.root / change
                expected = {"traversal": "Bound paths must remain below",
                            "symlink": "Symlink input is not supported",
                            "duplicate": "Invalid or duplicate source id",
                            "unstable": "No intervals satisfy"}[change]
                with self.assertRaisesRegex(ValueError, expected):
                    prepare(self.plan_path, target)
                self.assertFalse(target.exists())
                self.analysis, self.plan = original_analysis, original_plan

    def test_refuses_repeated_audio_duplicate_json_keys_and_empty_source_url(self) -> None:
        baseline_plan = copy.deepcopy(self.plan)
        for change in ("audio", "keys", "url"):
            with self.subTest(change=change):
                self.plan = copy.deepcopy(baseline_plan)
                self.save()
                original_plan = copy.deepcopy(self.plan)
                if change == "audio":
                    second = copy.deepcopy(self.plan["sources"][0])
                    second.update(id="fixture-e", sourceVowel="e")
                    self.plan["sources"].append(second)
                    expected = "Duplicate source audio hash"
                elif change == "url":
                    self.plan["license"]["sourceUrl"] = ""
                    expected = "License sourceUrl must not be empty"
                else:
                    data = json.dumps(self.analysis).replace('"sampleRate": 8000',
                                                            '"sampleRate": 8000, "sampleRate": 8000')
                    self.analysis_path.write_text(data)
                    self.plan["sources"][0]["analysis"] = self.binding(self.analysis_path)
                    expected = "Duplicate JSON key"
                self.plan_path.write_text(json.dumps(self.plan))
                target = self.root / change
                with self.assertRaisesRegex(ValueError, expected):
                    prepare(self.plan_path, target)
                self.assertFalse(target.exists())
                self.plan = original_plan
                self.save()

    def test_malformed_cli_input_has_controlled_stderr_refusal(self) -> None:
        tool = Path(__file__).resolve().parents[1] / "tools/voicebank_source_intake/prepare_sustain_cuts.py"
        for change in ("large-number", "deep-json"):
            with self.subTest(change=change):
                if change == "large-number":
                    data = copy.deepcopy(self.analysis)
                    data["pitchFrames"][0]["f0Hz"] = 10 ** 399
                    text = json.dumps(data)
                else:
                    text = "[" * 1500 + "0" + "]" * 1500
                self.analysis_path.write_text(text)
                self.plan["sources"][0]["analysis"] = self.binding(self.analysis_path)
                self.plan_path.write_text(json.dumps(self.plan))
                target = self.root / change
                result = subprocess.run([sys.executable, str(tool), "--plan", str(self.plan_path),
                                         "--output", str(target)], capture_output=True, text=True)
                self.assertEqual(2, result.returncode)
                self.assertEqual("", result.stdout)
                self.assertIn("Preparation refused:", result.stderr)
                self.assertNotIn("Traceback", result.stderr)
                self.assertFalse(target.exists())

    def test_io_failure_keeps_incomplete_marker_and_no_complete_manifest(self) -> None:
        output = self.root / "partial"
        original = Path.write_bytes

        def failing_write(path: Path, data: bytes) -> int:
            if path.name == "license-source.txt":
                raise OSError("Forced publication failure")
            return original(path, data)

        with patch.object(Path, "write_bytes", failing_write):
            with self.assertRaisesRegex(OSError, "Forced publication failure"):
                prepare(self.plan_path, output)
        self.assertTrue((output / "INCOMPLETE").is_file())
        self.assertFalse((output / "cuts.json").exists())

    def test_format_and_rate_refusals_name_the_failed_boundary(self) -> None:
        for change in ("stereo", "pcm24", "rate"):
            with self.subTest(change=change):
                if change == "rate":
                    self.analysis["sampleRate"] = 16000
                    expected = "Analysis/audio extent or format mismatch"
                else:
                    width = 3 if change == "pcm24" else 2
                    channels = 2 if change == "stereo" else 1
                    with wave.open(str(self.audio), "wb") as wav:
                        wav.setnchannels(channels)
                        wav.setsampwidth(width)
                        wav.setframerate(self.rate)
                        wav.writeframes((b"\x01" + b"\x00" * (width - 1)) * self.frames * channels)
                    expected = "supports mono PCM16 only"
                self.plan["sources"][0]["audio"] = self.binding(self.audio)
                self.save()
                target = self.root / change
                with self.assertRaisesRegex(ValueError, expected):
                    prepare(self.plan_path, target)
                self.assertFalse(target.exists())
                with wave.open(str(self.audio), "wb") as wav:
                    wav.setnchannels(1)
                    wav.setsampwidth(2)
                    wav.setframerate(self.rate)
                    wav.writeframes(self.original_pcm)


if __name__ == "__main__":
    unittest.main()
