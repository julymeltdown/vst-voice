import os
from pathlib import Path
import sys
import tempfile
import unittest
import signal
from unittest.mock import patch
from tools.voice_model_training.native_features import extract_pitch


@unittest.skipUnless(os.name == "posix", "POSIX supervisor")
class NativeFeatureTests(unittest.TestCase):
    def test_exited_leader_does_not_skip_descendant_group_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.wav"; source.write_bytes(b"fixture")
            pid_file = root / "leader.pid"
            executable = root / "forking-fixture"
            executable.write_text(f"#!{sys.executable}\nimport os,time\n"
                f"open({str(pid_file)!r}, 'w').write(str(os.getpid()))\n"
                "if os.fork() == 0:\n time.sleep(10)\n os._exit(0)\nos._exit(0)\n")
            executable.chmod(0o700)
            original_killpg = os.killpg
            with patch("tools.voice_model_training.native_features.os.killpg", wraps=original_killpg) as kill:
                with self.assertRaisesRegex(ValueError, "timed out"):
                    extract_pitch(executable, source, timeout_seconds=3)
                leader = int(pid_file.read_text())
                kill.assert_called_once_with(leader, signal.SIGKILL)

    def test_timeout_duplicate_output_and_stderr_budget(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.wav"; source.write_bytes(b"fixture")
            executable = root / "fixture-extractor"
            for code, timeout in (("import time; time.sleep(10)", 0.05),
                                  ('print(\'{"x":1,"x":2}\')', 5),
                                  ('import sys; sys.stderr.write("x" * 70000)', 5)):
                executable.write_text(f"#!{sys.executable}\n{code}\n")
                executable.chmod(0o700)
                with self.assertRaises(ValueError):
                    extract_pitch(executable, source, timeout_seconds=timeout)
            executable.write_text(f'#!{sys.executable}\nprint(\'{{"ok":true}}\')\n')
            self.assertEqual(extract_pitch(executable, source), {"ok": True})


if __name__ == "__main__":
    unittest.main()
