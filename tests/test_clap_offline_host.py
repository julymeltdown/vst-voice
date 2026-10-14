"""Actual loaded-plugin cold bounce checks; never linked-runtime substitutes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
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
            overflow_project = root / "probe-placement-overflow.seam"
            overflow_data = json.loads(project.read_text())
            overflow_region = overflow_data["vocalTracks"][0]["regions"][0]
            # The original region end is domain-valid; the probe's added rest is not.
            overflow_region["startTick"] = (1 << 63) - 1 - overflow_region["durationTick"]
            overflow_project.write_text(json.dumps(overflow_data))
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
                (1, "Pinned project exceeds offline probe budget", ["--project", overflow_project,
                    "--project-sha256", hashlib.sha256(overflow_project.read_bytes()).hexdigest()]),
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

    def test_ambiguous_arguments_are_refused_before_plugin_loading(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trials = [
                ["--project", "", "--project-sha256", ""],
                ["--project", "first.seam", "--project", "second.seam",
                 "--project-sha256", "0" * 64],
                ["--project-sha256", "0" * 64, "--project-sha256", "1" * 64,
                 "--project", "missing.seam"],
                ["--offline-only"],
                ["--audio", ""],
                ["--summary", ""],
                ["--audio", "--summary", str(root / "capture.json")],
                ["--project", "--offline-only", "--project-sha256", "0" * 64],
            ]
            for extra in trials:
                with self.subTest(arguments=extra):
                    result = subprocess.run(
                        [str(ARGS.runner), "--plugin", str(root / "must-not-load.clap"),
                         "--offline-only", *extra], capture_output=True, text=True,
                        timeout=10, check=False,
                    )
                    self.assertEqual(2, result.returncode, result.stdout + result.stderr)
                    self.assertIn("Usage:", result.stderr)
                    self.assertNotIn("Unable to load", result.stderr)
            self.assertFalse((root / "capture.json").exists())

    def test_follow_host_budget_and_unmuted_notes_are_admitted_before_loading(self):
        project = self.installed_project()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for beats, muted, accepted in [(124, False, True), (125, False, False),
                                            (4, True, False)]:
                with self.subTest(beats=beats, muted=muted):
                    data = json.loads(project.read_text())
                    data["tempoMap"] = [{"tick": 0, "bpm": 120}]
                    data["settings"]["hostStartOffsetTick"] = 0
                    track = data["vocalTracks"][0]
                    track["muted"] = muted
                    region = track["regions"][0]
                    region["startTick"] = 0
                    region["durationTick"] = beats * data["ppq"]
                    candidate = root / "candidate.seam"
                    candidate.write_text(json.dumps(data))
                    result = subprocess.run(
                        [str(ARGS.runner), "--plugin", str(root / "must-not-load.clap"),
                         "--offline-only", "--project", str(candidate), "--project-sha256",
                         hashlib.sha256(candidate.read_bytes()).hexdigest(),
                         "--audio", str(root / "must-not-exist.wav")],
                        capture_output=True, text=True, timeout=10, check=False,
                    )
                    self.assertEqual(1, result.returncode, result.stdout + result.stderr)
                    # Exact boundary reaches module admission; beyond it does not.
                    self.assertIn("Unable to load" if accepted else
                                  "Pinned project exceeds offline probe budget", result.stderr)
                    self.assertFalse((root / "must-not-exist.wav").exists())

    def test_pinned_capture_outputs_preserve_existing_files_and_are_distinct(self):
        project = self.installed_project()
        digest = hashlib.sha256(project.read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trials = [("audio", "direct"), ("summary", "direct"),
                      ("audio", "hardlink")]
            if os.name != "nt":
                trials += [("summary", "symlink"), ("audio", "dangling")]
            for index, (option, kind) in enumerate(trials):
                with self.subTest(option=option, kind=kind):
                    sentinel = root / f"preserved-{index}.bin"
                    sentinel.write_bytes(b"Private owned input; preserve exact bytes.\n")
                    before = sentinel.read_bytes()
                    output = root / f"alias-{index}.bin"
                    missing = root / f"missing-{index}.bin"
                    if kind == "hardlink":
                        os.link(sentinel, output)
                    elif kind == "symlink":
                        output.symlink_to(sentinel)
                    elif kind == "dangling":
                        output.symlink_to(missing)
                    else:
                        output = sentinel
                    other = root / f"fresh-{index}.bin"
                    arguments = ["--audio", str(output if option == "audio" else other),
                                 "--summary", str(output if option == "summary" else other)]
                    result = subprocess.run(
                        [str(ARGS.runner), "--plugin", str(ARGS.plugin), "--offline-only",
                         "--project", str(project), "--project-sha256", digest, *arguments],
                        capture_output=True, text=True, timeout=120, check=False,
                    )
                    self.assertEqual(1, result.returncode, result.stdout + result.stderr)
                    self.assertIn("Pinned project capture outputs must be new and distinct", result.stderr)
                    self.assertEqual(before, sentinel.read_bytes())
                    self.assertFalse(other.exists())
                    self.assertFalse(missing.exists())
            same = root / "same-output.bin"
            result = subprocess.run(
                [str(ARGS.runner), "--plugin", str(ARGS.plugin), "--offline-only",
                 "--project", str(project), "--project-sha256", digest,
                 "--audio", str(same), "--summary", str(same)],
                capture_output=True, text=True, timeout=120, check=False,
            )
            self.assertEqual(1, result.returncode, result.stdout + result.stderr)
            self.assertIn("Pinned project capture outputs must be new and distinct", result.stderr)
            self.assertFalse(same.exists())
            self.assertEqual(digest, hashlib.sha256(project.read_bytes()).hexdigest())

    def test_tuned_installed_lyric_song_bounces_after_producer_source_is_removed(self):
        result = subprocess.run(
            [str(ARGS.song_journey_runner),
             "Tuning an installed singer survives undo, save, reopen and export"],
            capture_output=True, text=True, timeout=120, check=False,
        )
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        paths = [line.removeprefix("INSTALLED_HOST_SONG_PROJECT ")
                 for line in result.stdout.splitlines()
                 if line.startswith("INSTALLED_HOST_SONG_PROJECT ")]
        self.assertEqual(1, len(paths))
        project = Path(paths[0])
        data = json.loads(project.read_text())
        regions = [region for track in data["vocalTracks"] for region in track["regions"]]
        self.assertEqual(48, sum(len(region["notes"]) for region in regions))
        self.assertEqual(78720, regions[0]["durationTick"])  # 41 seconds at 120 BPM.
        self.assertEqual(2, len(regions[0]["formantAutomation"]))
        self.assertGreater(len({lyric["surface"] for lyric in regions[0]["lyrics"]}), 20)
        # Real saved exports carry provenance; it must not change the sound identity.
        self.assertGreater(data["settings"]["renderedCompilerRevision"], 0)
        self.assertTrue(data["settings"]["renderedRenderAbi"])
        self.assertIsNotNone(data["vocalTracks"][0]["proceduralRecipe"]["installation"])
        self.assertTrue(Path(data["vocalTracks"][0]["proceduralRecipe"]["path"]).is_file())
        # These are this native invocation's temporary producer files, after its
        # tuning/undo/redo/cancellation/save/reopen/export checks have passed.
        shutil.rmtree(project.parent / "producer-source")
        (project.parent / "song-01.seamsinger").unlink()
        self.assertFalse((project.parent / "producer-source").exists())
        self.assertFalse((project.parent / "song-01.seamsinger").exists())
        digest = hashlib.sha256(project.read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = self.run_host(root, "--project", project, "--project-sha256", digest,
                                   "--audio", root / "song.wav")
            self.assertEqual("PASS", report["result"])
            self.assertEqual(digest, report["inputProjectSha256"])
            self.assertEqual(96, report["noteWindows"])  # All 48 notes at both rates.
            for key in ("inputProjectLoaded", "inputProjectStateRoundTripMatched",
                        "completeScoreBounce", "rateChangeReprepared", "failedFinalRejected",
                        "missingFinalRejected", "beatsOnlyRejected", "eventOverflowFailClosed",
                        "realtimeOverflowKeepsAdvancing", "followHostOffsetBounce",
                        "followHostStaleRejected", "followHostTransportEventRejected"):
                self.assertIs(True, report[key], key)
            self.assertEqual(0, report["noteEventsSent"])
            self.assertFalse(report["releaseEligible"])
            audio = (root / "song.wav").read_bytes()
            self.assertEqual(3, struct.unpack_from("<H", audio, 20)[0])
            self.assertEqual(48000, struct.unpack_from("<I", audio, 24)[0])
            channels = struct.unpack_from("<H", audio, 22)[0]
            self.assertEqual(2004000 * channels * 4, len(audio) - 44)

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
    parser.add_argument("--song-journey-runner", type=Path, required=True)
    ARGS = parser.parse_args()
    unittest.main(argv=[__file__])
