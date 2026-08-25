from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))
import distribution_manifest  # noqa: E402


class ValidationAttachTests(unittest.TestCase):
    def test_attach_binds_real_validator_records_without_rebuilding_payload(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            vst3 = root / "ProjectSEAMEditor.vst3" / "Contents" / "x86_64-linux"
            vst3.mkdir(parents=True)
            (vst3 / "ProjectSEAMEditor.so").write_bytes(b"vst3")
            au = root / "ProjectSEAMEditor.component" / "Contents"
            (au / "MacOS").mkdir(parents=True)
            (au / "MacOS" / "ProjectSEAMEditor").write_bytes(b"au")
            (au / "Info.plist").write_bytes(b"plist")
            vst3_sha = distribution_manifest.tree_sha256(vst3.parents[1])
            au_sha = distribution_manifest.tree_sha256(au.parent)
            build_result = root / "phase13a-build-result.json"
            build_result.write_text(
                json.dumps(
                    {
                        "schemaVersion": 1,
                        "version": "0.13.1",
                        "artifacts": [
                            {"format": "VST3", "path": str(vst3.parents[1]), "sha256": vst3_sha},
                            {"format": "AUv2", "path": str(au.parent), "sha256": au_sha},
                        ],
                        "validation": {"vst3-validator": "NOT_RUN", "auval": "NOT_RUN"},
                    }
                ),
                encoding="utf-8",
            )
            vst3_result = root / "vst3.json"
            vst3_result.write_text(
                json.dumps({"schemaVersion": 1, "status": "PASS", "pluginSha256": vst3_sha}),
                encoding="utf-8",
            )
            au_result = root / "auval.json"
            au_result.write_text(
                json.dumps({"schemaVersion": 1, "status": "PASS", "componentSha256": au_sha}),
                encoding="utf-8",
            )
            completed = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts/attach_phase13a_validation.py"),
                    "--build-result",
                    str(build_result),
                    "--vst3-validation-result",
                    str(vst3_result),
                    "--auval-validation-result",
                    str(au_result),
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(0, completed.returncode, completed.stdout)
            value = json.loads(build_result.read_text(encoding="utf-8"))
            self.assertEqual({"vst3-validator": "PASS", "auval": "PASS"}, value["validation"])
            self.assertEqual("READY", value["releaseManifest"]["releaseStatus"])
            self.assertEqual(vst3_sha, distribution_manifest.tree_sha256(vst3.parents[1]))
            self.assertEqual(au_sha, distribution_manifest.tree_sha256(au.parent))


if __name__ == "__main__":
    unittest.main()
