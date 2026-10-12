from __future__ import annotations

import copy
import hashlib
import json
import math
from pathlib import Path
import struct
import tempfile
import unittest
import wave

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
                with self.assertRaises(ValueError):
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
                with self.assertRaises(ValueError):
                    prepare(self.plan_path, target)
                self.assertFalse(target.exists())
                self.analysis, self.plan = original_analysis, original_plan


if __name__ == "__main__":
    unittest.main()
