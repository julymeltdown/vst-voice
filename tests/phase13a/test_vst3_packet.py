import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.phase13a.distribution_manifest import tree_sha256
from scripts.verify_phase13a_vst3_packet import (
    Vst3PacketInputs,
    create_packet,
    verify_packet,
)

ROOT = Path(__file__).resolve().parents[2]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Vst3PacketTests(unittest.TestCase):
    def fixture(self, root: Path) -> Vst3PacketInputs:
        plugin = root / "payload/VST3/ProjectSEAMEditor.vst3"
        (plugin / "Contents/aarch64-linux").mkdir(parents=True)
        (plugin / "Contents/aarch64-linux/ProjectSEAMEditor.so").write_bytes(b"plugin")
        clap = root / "payload/CLAP/ProjectSEAMEditor.clap"
        clap.parent.mkdir(parents=True)
        clap.write_bytes(b"clap")
        validator = root / "validator/validator"
        validator.parent.mkdir(parents=True)
        validator.write_bytes(b"validator")
        runner = root / "runner.json"
        runner.write_text(
            '{"runnerOs":"Linux","runnerArchitecture":"aarch64"}\n',
            encoding="utf-8",
        )
        stdout = root / "vst3-validator/validator.log"
        stderr = root / "vst3-validator/validator.stderr.log"
        stdout.parent.mkdir(parents=True)
        stdout.write_text("validator PASS\n", encoding="utf-8")
        stderr.write_text("", encoding="utf-8")
        result = root / "vst3-validator/result.json"
        result.write_text(
            json.dumps(
                {
                    "schemaVersion": 1,
                    "status": "PASS",
                    "pluginSha256": tree_sha256(plugin),
                    "canonicalClapSha256": tree_sha256(clap),
                    "tool": {"sha256": digest(validator)},
                    "platform": "linux",
                }
            ),
            encoding="utf-8",
        )
        build_result = root / "phase13a-build-result.json"
        build_result.write_text('{"version":"0.13.0"}\n', encoding="utf-8")
        return Vst3PacketInputs(
            root=root,
            result=result,
            stdout_log=stdout,
            stderr_log=stderr,
            plugin=plugin,
            clap=clap,
            validator=validator,
            runner_metadata=runner,
            build_result=build_result,
        )

    def test_create_and_verify_binds_all_vst3_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            packet = root / "vst3-validator/packet.json"
            create_packet(self.fixture(root), packet)
            self.assertEqual(verify_packet(packet, root), [])

    def test_verify_rejects_mutated_plugin_tree(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inputs = self.fixture(root)
            packet = root / "vst3-validator/packet.json"
            create_packet(inputs, packet)
            inputs.plugin.joinpath("Contents/aarch64-linux/ProjectSEAMEditor.so").write_bytes(b"mutated")
            errors = verify_packet(packet, root)
            self.assertTrue(any("plugin" in error and "digest" in error for error in errors))

    def test_verify_rejects_mutated_canonical_clap(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inputs = self.fixture(root)
            packet = root / "vst3-validator/packet.json"
            create_packet(inputs, packet)
            inputs.clap.write_bytes(b"mutated")
            errors = verify_packet(packet, root)
            self.assertTrue(any("clap" in error.lower() for error in errors))

    def test_cli_create_and_verify(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inputs = self.fixture(root)
            packet = root / "vst3-validator/cli-packet.json"
            command = [
                sys.executable,
                str(ROOT / "scripts/verify_phase13a_vst3_packet.py"),
                "--create", "--root", str(root), "--packet", str(packet),
                "--result", str(inputs.result), "--stdout-log", str(inputs.stdout_log),
                "--stderr-log", str(inputs.stderr_log), "--plugin", str(inputs.plugin),
                "--clap", str(inputs.clap), "--validator", str(inputs.validator),
                "--runner-metadata", str(inputs.runner_metadata),
                "--build-result", str(inputs.build_result),
            ]
            result = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(verify_packet(packet, root), [])

    def test_cli_help_is_available(self) -> None:
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts/verify_phase13a_vst3_packet.py"), "--help"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
