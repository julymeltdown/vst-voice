"""Executable admission regressions. Never substitute these for the process matrix."""
import argparse
import json
from pathlib import Path
import subprocess
import shutil
import tempfile
import unittest
import wave

ARGS = None


class MatrixHostAdmissionTests(unittest.TestCase):
    def run_matrix(self, output, *arguments):
        return subprocess.run([str(ARGS.runner), str(output), *map(str, arguments)],
                              capture_output=True, text=True, timeout=120, check=False)

    def test_leading_silence_keeps_host_processing_until_audible_attack(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bank = root / "leading-silence-bank"
            shutil.copytree(ARGS.bank, bank)
            # Modify only an isolated engineering resource, never the source bank.
            for audio in (bank / "audio").glob("*.wav"):
                with wave.open(str(audio), "rb") as stream:
                    parameters = stream.getparams()
                    pcm = stream.readframes(parameters.nframes)
                silent_bytes = (parameters.framerate // 50) * parameters.nchannels * parameters.sampwidth
                with wave.open(str(audio), "wb") as stream:
                    stream.setparams(parameters)
                    stream.writeframes(bytes(min(silent_bytes, len(pcm))) + pcm[silent_bytes:])
            output = root / "matrix.json"
            result = self.run_matrix(output, "--plugin", ARGS.plugin, "--bank", bank, "--development-fixture")
            self.assertEqual(0, result.returncode, result.stderr + result.stdout)
            report = json.loads(output.read_text())
            self.assertEqual(336, len(report["rowResults"]))
            self.assertTrue(all(row["processingStatusVerified"] for row in report["rowResults"]))

    def test_text_plugin_is_rejected_and_replaces_stale_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plugin = root / "not-a-plugin.clap"
            plugin.write_text("not a binary", encoding="utf-8")
            output = root / "matrix.json"
            output.write_text('{"result":"PASS"}', encoding="utf-8")
            result = self.run_matrix(output, "--plugin", plugin, "--bank", ARGS.bank, "--development-fixture")
            self.assertNotEqual(0, result.returncode)
            self.assertIn("Cannot load supplied CLAP binary", result.stderr)
            self.assertEqual("FAIL", json.loads(output.read_text())["result"])

    def test_fixture_requires_explicit_admission(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_matrix(Path(directory) / "report.json", "--plugin", ARGS.plugin, "--bank", ARGS.bank)
            self.assertNotEqual(0, result.returncode)
            self.assertIn("trusted installed", result.stderr)

    def test_bad_arguments_and_input_overwrite_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "report.json"
            for tail in (["--plugin"], ["--unknown", "1"], ["--bank", ARGS.bank],
                         ["--plugin", ARGS.plugin, "--plugin", ARGS.plugin]):
                with self.subTest(arguments=tail):
                    result = self.run_matrix(output, *tail)
                    self.assertNotEqual(0, result.returncode)
                    self.assertFalse(output.exists())
            result = self.run_matrix(ARGS.bank / "do-not-write.json",
                                     "--plugin", ARGS.plugin, "--bank", ARGS.bank, "--development-fixture")
            self.assertNotEqual(0, result.returncode)
            self.assertFalse((ARGS.bank / "do-not-write.json").exists())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--bank", type=Path, required=True)
    ARGS = parser.parse_args()
    unittest.main(argv=[__file__])
