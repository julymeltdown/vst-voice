from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from scripts.verify_phase12c_soak_packet import (
    SoakPacketInputs,
    create_packet,
    verify_packet,
)

ROOT = Path(__file__).resolve().parents[1]


class Phase12CSoakPacketTests(unittest.TestCase):
    def test_cli_runs_from_repository_root(self) -> None:
        completed = subprocess.run(
            [sys.executable, str(Path(__file__).parents[1] / "scripts/verify_phase12c_soak_packet.py"), "--help"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_create_and_verify_binds_every_uploaded_artifact(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            root = Path(directory)
            live = root / "live"
            live.mkdir()
            summary = live / "summary.json"
            summary.write_text(
                json.dumps(
                    {
                        "voicebankId": "demo.voice",
                        "voicebankVersion": "0.1.0",
                        "voicebankContentHash": "a" * 64,
                        "unitCount": 1,
                        "energy": 2.0,
                        "noteOns": 1,
                        "steals": 1,
                        "transitionFallbacks": 1,
                        "eventOverflows": 0,
                        "renderedFrames": 64,
                        "finite": True,
                        "result": "PASS",
                    }
                ),
                encoding="utf-8",
            )
            audio = live / "live.wav"
            audio.write_bytes(b"RIFF-test")
            soak = root / "soak.json"
            soak.write_text(
                json.dumps(
                    {
                        "profile": "full",
                        "requiredSeconds": 7200,
                        "elapsedSeconds": 7200,
                        "blocks": 1,
                        "eventBlocks": 1,
                        "resourcePublishes": 1,
                        "resourceClears": 1,
                        "maxActiveVoices": 1,
                        "absoluteEnergy": 2.0,
                        "peak": 0.5,
                        "finite": True,
                        "noteOns": 1,
                        "noteOffs": 1,
                        "steals": 1,
                        "transitionHits": 1,
                        "transitionFallbacks": 1,
                        "midiEvents": 1,
                        "expressionEvents": 1,
                        "renderedFrames": 64,
                        "silentFramesNoResource": 1,
                        "eventOverflows": 0,
                        "result": "PASS",
                    }
                ),
                encoding="utf-8",
            )
            runner = root / "runner.json"
            runner.write_text('{"os":"Linux","architecture":"x86_64"}\n', encoding="utf-8")
            binary = root / "seam_phase12c_soak"
            binary.write_bytes(b"ELF-test")
            packet = root / "packet.json"

            create_packet(
                SoakPacketInputs(
                    root=root,
                    summary=summary.relative_to(root),
                    audio=audio.relative_to(root),
                    soak=soak.relative_to(root),
                    runner_metadata=runner.relative_to(root),
                    soak_binary=binary.relative_to(root),
                ),
                packet,
            )

            self.assertEqual(verify_packet(packet, root), [])

            relative_root = root.relative_to(ROOT)
            completed = subprocess.run(
                [
                    sys.executable,
                    "scripts/verify_phase12c_soak_packet.py",
                    "--create",
                    "--root",
                    str(relative_root),
                    "--packet",
                    str(relative_root / "packet-cli.json"),
                    "--summary",
                    str(relative_root / "live/summary.json"),
                    "--audio",
                    str(relative_root / "live/live.wav"),
                    "--soak",
                    str(relative_root / "soak.json"),
                    "--runner-metadata",
                    str(relative_root / "runner.json"),
                    "--soak-binary",
                    str(relative_root / "seam_phase12c_soak"),
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_verify_rejects_digest_mismatch_and_path_escape(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifact = root / "artifact.bin"
            artifact.write_bytes(b"known")
            packet = root / "packet.json"
            packet.write_text(
                json.dumps(
                    {
                        "schemaVersion": 1,
                        "recordType": "phase12c-full-soak-packet",
                        "profile": "full",
                        "requiredSeconds": 7200,
                        "elapsedSeconds": 7200,
                        "result": "PASS",
                        "artifacts": {
                            "soak": {"path": "../artifact.bin", "sha256": "0" * 64},
                        },
                    }
                ),
                encoding="utf-8",
            )

            errors = verify_packet(packet, root)

            self.assertTrue(any("path escapes" in error for error in errors))
            self.assertTrue(any("record soak" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
