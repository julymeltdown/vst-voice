import hashlib
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.voice_model_training.evaluation_provenance import capture_evaluation_provenance


class EvaluationProvenanceTests(unittest.TestCase):
    def test_receipt_binds_evaluator_pitch_executable_and_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "pitch"
            payload = b"frozen pitch tool bytes\n"
            executable.write_bytes(payload)

            provenance = capture_evaluation_provenance(Path(__file__), executable)

        self.assertEqual(
            provenance["evaluator"]["sha256"],
            hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        )
        self.assertEqual(
            provenance["provenanceHelper"]["sha256"],
            hashlib.sha256(Path(capture_evaluation_provenance.__code__.co_filename).read_bytes()).hexdigest(),
        )
        self.assertEqual(provenance["pitchExecutable"]["sha256"], hashlib.sha256(payload).hexdigest())
        self.assertEqual(provenance["pitchExecutable"]["sizeBytes"], len(payload))
        self.assertTrue(provenance["runtime"]["pythonVersion"])
        self.assertTrue(provenance["runtime"]["pythonImplementation"])
        self.assertTrue(provenance["runtime"]["platform"])
        self.assertIn("numpy", provenance["runtime"])
        self.assertIn("scipy", provenance["runtime"])
        self.assertIn("onnxruntime", provenance["runtime"])

    def test_non_file_pitch_path_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises((OSError, ValueError)):
                capture_evaluation_provenance(Path(__file__), Path(directory))

    def test_oversized_pitch_executable_is_rejected_before_reading(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "pitch"
            executable.write_bytes(b"too large")

            with patch(
                "tools.voice_model_training.evaluation_provenance.MAX_PITCH_EXECUTABLE_BYTES",
                4,
            ):
                with self.assertRaisesRegex(ValueError, "bounded regular file"):
                    capture_evaluation_provenance(Path(__file__), executable)

    def test_pitch_executable_mutation_during_hash_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "pitch"
            executable.write_bytes(b"stable")
            read = os.read
            appended = False

            def read_and_mutate(descriptor, size):
                nonlocal appended
                result = read(descriptor, size)
                if result and not appended:
                    with executable.open("ab") as output:
                        output.write(b"changed")
                    appended = True
                return result

            with patch(
                "tools.voice_model_training.evaluation_provenance.os.read",
                side_effect=read_and_mutate,
            ):
                with self.assertRaisesRegex(ValueError, "changed during provenance capture"):
                    capture_evaluation_provenance(Path(__file__), executable)


if __name__ == "__main__":
    unittest.main()
