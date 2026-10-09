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
    def test_packet_cli_uses_artifact_root_relative_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory)
            root = workspace / 'out/phase13a'
            inputs = self.fixture(root)
            (root / 'Linux').mkdir()
            inputs.result.parent.rename(root / 'Linux/vst3-validator')
            inputs.validator.rename(root / 'Linux/vst3-validator/validator')
            inputs.runner_metadata.rename(root / 'Linux/runner.json')
            packet = root / 'Linux/vst3-validator/packet.json'
            # Run from outside the artifact root: inputs are rooted at --root,
            # not the process working directory, with no CI shell involved.
            command = [sys.executable, str(ROOT / 'scripts/verify_phase13a_vst3_packet.py'),
                '--root', 'out/phase13a', '--packet', str(packet), '--create',
                '--result', 'Linux/vst3-validator/result.json',
                '--stdout-log', 'Linux/vst3-validator/validator.log',
                '--stderr-log', 'Linux/vst3-validator/validator.stderr.log',
                '--plugin', str(inputs.plugin.relative_to(root)),
                '--clap', str(inputs.clap.relative_to(root)),
                '--validator', 'Linux/vst3-validator/validator',
                '--runner-metadata', 'Linux/runner.json',
                '--build-result', str(inputs.build_result.relative_to(root))]
            result = subprocess.run(command, cwd=workspace, capture_output=True, text=True, timeout=30)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertEqual([], verify_packet(packet, root))
            # The real CLI must also refuse a changed input without publishing
            # another packet; no mock validator or acceptance promotion is used.
            (root / 'Linux/vst3-validator/validator').write_bytes(b'changed validator')
            command[command.index('--packet') + 1] = str(packet.with_name('rejected.json'))
            refused = subprocess.run(command, cwd=workspace, capture_output=True, text=True, timeout=30)
            self.assertNotEqual(0, refused.returncode)
            self.assertFalse(packet.with_name('rejected.json').exists())

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
