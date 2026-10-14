"""Actual loaded-plugin cold bounce checks; never linked-runtime substitutes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ARGS = None


class ClapOfflineHostTests(unittest.TestCase):
    def run_host(self, root, *arguments):
        result = subprocess.run(
            [str(ARGS.runner), "--plugin", str(ARGS.plugin), "--offline-only",
             "--summary", str(root / "summary.json"), *map(str, arguments)],
            capture_output=True, text=True, timeout=120, check=False,
        )
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        return json.loads((root / "summary.json").read_text())

    def installed_project(self):
        result = subprocess.run(
            [str(ARGS.journey_runner),
             "An installed procedural singer sings a tuned phrase after the producer source is gone"],
            capture_output=True, text=True, timeout=60, check=False,
        )
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        paths = [line.removeprefix("INSTALLED_HOST_PROJECT ")
                 for line in result.stdout.splitlines() if line.startswith("INSTALLED_HOST_PROJECT ")]
        self.assertEqual(1, len(paths))
        project = Path(paths[0])
        self.assertTrue(project.is_file())
        self.assertFalse((project.parent / "producer-source").exists())
        self.assertFalse((project.parent / "authored.seamsinger").exists())
        recipe = json.loads(project.read_text())["vocalTracks"][0]["proceduralRecipe"]
        self.assertIsNotNone(recipe["installation"])
        self.assertTrue(Path(recipe["path"]).is_file())
        return project

    def test_pinned_installed_project_loads_and_bounces_in_cold_plugin(self):
        project = self.installed_project()
        digest = hashlib.sha256(project.read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = self.run_host(root, "--project", project, "--project-sha256", digest,
                                   "--audio", root / "installed.wav")
            self.assertEqual("PASS", report["result"])
            self.assertIs(True, report["inputProjectLoaded"])
            self.assertIs(True, report["inputProjectStateRoundTripMatched"])
            self.assertEqual(digest, report["inputProjectSha256"])
            # Project JSON uses short hex; the typed ID view uses zero-padded hex.
            self.assertEqual(int(json.loads(project.read_text())["projectId"], 16),
                             int(report["inputProjectId"], 16))
            self.assertEqual(2, report["noteWindows"])  # One installed note at each rate.
            for key in ("completeScoreBounce", "rateChangeReprepared", "failedFinalRejected",
                        "missingFinalRejected", "beatsOnlyRejected", "eventOverflowFailClosed",
                        "realtimeOverflowKeepsAdvancing", "followHostOffsetBounce",
                        "followHostStaleRejected", "followHostTransportEventRejected"):
                self.assertIs(True, report[key], key)
            self.assertFalse(report["releaseEligible"])
            self.assertGreater((root / "installed.wav").stat().st_size, 44)

    def test_pinned_project_admission_refuses_before_plugin_or_output(self):
        project = self.installed_project()
        digest = hashlib.sha256(project.read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            invalid = root / "invalid.seam"
            invalid.write_text("not JSON")
            oversized = root / "oversized.seam"
            with oversized.open("wb") as stream:
                stream.truncate(16 * 1024 * 1024 + 1)
            long_project = root / "too-long.seam"
            long_data = json.loads(project.read_text())
            long_data["settings"]["hostStartOffsetTick"] = 100000000
            long_project.write_text(json.dumps(long_data))
            crowded_project = root / "too-many-notes.seam"
            crowded_data = json.loads(project.read_text())
            region = crowded_data["vocalTracks"][0]["regions"][0]
            original_note = region["notes"][0]
            region["notes"] = [dict(original_note, id=format(10000 + index, "x"),
                                    startTick=index, durationTick=1) for index in range(257)]
            crowded_project.write_text(json.dumps(crowded_data))
            trials = [
                (1, "Pinned project digest does not match", ["--project", project, "--project-sha256", "0" * 64]),
                (1, "Pinned project decode failed", ["--project", invalid, "--project-sha256",
                    hashlib.sha256(invalid.read_bytes()).hexdigest()]),
                (1, "Pinned project read failed", ["--project", oversized, "--project-sha256", "0" * 64]),
                (1, "Pinned project read failed", ["--project", root / "missing.seam", "--project-sha256", "0" * 64]),
                (2, "Usage:", ["--project", project]),
                (2, "Usage:", ["--project-sha256", digest]),
                (2, "Usage:", ["--project", project, "--project-sha256", "X" * 64]),
                (2, "Usage:", ["--unknown-option"]),
                (1, "Pinned project exceeds offline probe budget", ["--project", long_project,
                    "--project-sha256", hashlib.sha256(long_project.read_bytes()).hexdigest()]),
                (1, "Pinned project exceeds offline probe budget", ["--project", crowded_project,
                    "--project-sha256", hashlib.sha256(crowded_project.read_bytes()).hexdigest()]),
            ]
            if os.name != "nt":
                link = root / "linked.seam"
                link.symlink_to(project)
                trials.append((1, "Pinned project read failed", ["--project", link, "--project-sha256", digest]))
            for code, diagnostic, extra in trials:
                with self.subTest(arguments=list(map(str, extra))):
                    output = root / "must-not-exist.wav"
                    summary = root / "must-not-exist.json"
                    result = subprocess.run(
                        [str(ARGS.runner), "--plugin", str(root / "must-not-load.clap"),
                         "--offline-only", "--audio", str(output), "--summary", str(summary),
                         *map(str, extra)], capture_output=True, text=True, timeout=10, check=False,
                    )
                    self.assertEqual(code, result.returncode, result.stdout + result.stderr)
                    self.assertIn(diagnostic, result.stderr)
                    self.assertNotIn("Unable to load", result.stderr)
                    self.assertFalse(output.exists())
                    self.assertFalse(summary.exists())
            # Both direct and hard-linked output names must preserve the held input.
            alias = root / "project-alias.seam"
            os.link(project, alias)
            for option, path in [("--audio", project), ("--summary", alias)]:
                result = subprocess.run(
                    [str(ARGS.runner), "--plugin", str(root / "must-not-load.clap"),
                     "--offline-only", "--project", str(project), "--project-sha256", digest,
                     option, str(path)], capture_output=True, text=True, timeout=10, check=False,
                )
                self.assertEqual(1, result.returncode, result.stdout + result.stderr)
                self.assertIn("Pinned project output would replace input", result.stderr)
                self.assertEqual(digest, hashlib.sha256(project.read_bytes()).hexdigest())

    def test_cold_complete_score_bounce_and_negative_final_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            audio = root / "score-bounce.wav"
            report = self.run_host(root, "--target-runtime-fixture-root", ARGS.bank, "--audio", audio)
            self.assertEqual("PASS", report["result"])
            self.assertEqual("loaded-clap-cold-score-bounce-v1", report["executionPath"])
            self.assertEqual(0, report["noteEventsSent"])
            self.assertEqual(16, report["noteWindows"])  # Eight actual notes at each of two rates.
            self.assertGreater(report["capturedFrames"], 48000)
            self.assertGreater(report["scoreEnergy"], 0.01)
            for key in ("completeScoreBounce", "rateChangeReprepared", "missingFinalRejected",
                        "failedFinalRejected", "beatsOnlyRejected", "audioWritten",
                        "eventOverflowFailClosed", "realtimeOverflowKeepsAdvancing",
                        "followHostOffsetBounce", "followHostStaleRejected",
                        "followHostTransportEventRejected"):
                self.assertIs(True, report[key], key)
            self.assertGreater(report["followHostExpectedOnsetSeconds"], 2.0)
            self.assertLess(report["followHostEarlyEnergy"], 0.001)
            self.assertGreater(report["followHostOnsetEnergy"], 0.01)
            self.assertEqual("complete", report["followHostStage"])
            self.assertEqual("engineering", report["evidenceScope"])
            self.assertIs(False, report["releaseEligible"])
            self.assertIs(False, report["followHostQualified"])
            data = audio.read_bytes()
            self.assertEqual(b"RIFF", data[:4])
            self.assertEqual(b"WAVE", data[8:12])
            self.assertEqual(3, struct.unpack_from("<H", data, 20)[0])  # Float32 output.
            self.assertEqual(48000, struct.unpack_from("<I", data, 24)[0])
            self.assertEqual(len(data) - 44, struct.unpack_from("<I", data, 40)[0])
            self.assertGreater(len(data), 48000 * 4)

    def test_missing_final_cold_state_is_rejected_without_fake_positive_audio(self):
        with tempfile.TemporaryDirectory() as directory:
            report = self.run_host(Path(directory), "--expect-missing-bank")
            self.assertEqual("PASS", report["result"])
            self.assertIs(True, report["missingFinalRejected"])
            self.assertIs(True, report["expectedMissingBank"])
            self.assertIs(False, report["completeScoreBounce"])
            self.assertEqual(0, report["capturedFrames"])
            self.assertEqual(0, report["noteEventsSent"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--bank", type=Path, required=True)
    parser.add_argument("--journey-runner", type=Path, required=True)
    ARGS = parser.parse_args()
    unittest.main(argv=[__file__])
