"""Actual loaded-plugin cold bounce checks; never linked-runtime substitutes."""
import argparse
import json
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
    ARGS = parser.parse_args()
    unittest.main(argv=[__file__])
