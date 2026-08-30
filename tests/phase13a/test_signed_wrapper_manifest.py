from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.phase13a import distribution_manifest


ROOT = Path(__file__).resolve().parents[2]


class SignedWrapperManifestTests(unittest.TestCase):
    def test_refresh_rebinds_wrapper_hashes_to_signed_payload_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            clap = root / "CLAP" / "ProjectSEAMEditor.clap"
            clap.mkdir(parents=True)
            (clap / "module").write_bytes(b"signed clap")
            vst3 = root / "VST3" / "ProjectSEAMEditor.vst3"
            (vst3 / "Contents" / "MacOS").mkdir(parents=True)
            (vst3 / "Contents" / "MacOS" / "module").write_bytes(b"signed vst3")
            (vst3 / "Contents" / "Info.plist").write_bytes(b"plist")
            (vst3 / "Contents" / "Resources").mkdir(parents=True)
            au = root / "AU" / "ProjectSEAMEditor.component"
            (au / "Contents" / "MacOS").mkdir(parents=True)
            (au / "Contents" / "MacOS" / "module").write_bytes(b"signed au")
            (au / "Contents" / "Info.plist").write_bytes(b"plist")
            (au / "Contents" / "Resources").mkdir(parents=True)
            for format_name, path, identifier in (
                ("VST3", vst3, "com.project-seam.editor.vst3"),
                ("AUv2", au, "com.project-seam.editor.auv2"),
            ):
                location = path / "Contents" / "Resources" / "wrapper-manifest.json"
                location.write_text(
                    json.dumps(
                        distribution_manifest.build_wrapper_manifest(
                            format_name,
                            "macos",
                            "arm64",
                            "0.13.1",
                            identifier,
                            "0" * 64,
                            path,
                            {
                                "product": "Project SEAM",
                                "version": "0.13.1",
                                "buildId": "signed-test",
                                "sourceCommit": "a" * 40,
                                "buildEpoch": 1,
                            },
                        )
                    ),
                    encoding="utf-8",
                )
            completed = subprocess.run(
                [sys.executable, str(ROOT / "scripts/refresh_phase13a_wrapper_manifests.py"), str(root)],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(0, completed.returncode, completed.stdout)
            clap_sha = distribution_manifest.tree_sha256(clap)
            for format_name, path in (("VST3", vst3), ("AUv2", au)):
                manifest_path = path / "Contents" / "Resources" / "wrapper-manifest.json"
                manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                self.assertEqual(clap_sha, manifest["canonicalClapSha256"])
                self.assertEqual("signed-test", manifest["releaseIdentity"]["buildId"])
                (path / "Contents" / "MacOS" / "module").write_bytes(b"post-signature")
                (path / "Contents" / "_CodeSignature").mkdir(parents=True)
                (path / "Contents" / "_CodeSignature" / "CodeResources").write_bytes(b"codesign")
                self.assertEqual(
                    [],
                    distribution_manifest.validate_wrapper_bundle(
                        format_name.lower(), path, "macos", clap_sha
                    ),
                )

    def test_refresh_supports_windows_folder_vst3_after_authenticode_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            clap = root / "CLAP" / "ProjectSEAMEditor.clap"
            clap.parent.mkdir(parents=True)
            clap.write_bytes(b"unsigned clap")
            vst3 = root / "VST3" / "ProjectSEAMEditor.vst3"
            binary = vst3 / "x86_64-win" / "ProjectSEAMEditor.vst3"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"unsigned vst3")
            (vst3 / "moduleinfo.json").write_text("{}\n", encoding="utf-8")
            (vst3 / "wrapper-manifest.json").write_text(
                json.dumps(
                    distribution_manifest.build_wrapper_manifest(
                        "VST3",
                        "windows",
                        "x64",
                        "0.13.1",
                        "com.project-seam.editor.vst3",
                        distribution_manifest.tree_sha256(clap),
                        vst3,
                    )
                ),
                encoding="utf-8",
            )
            clap.write_bytes(b"authenticode clap")
            binary.write_bytes(b"authenticode vst3")
            completed = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts/refresh_phase13a_wrapper_manifests.py"),
                    str(root),
                    "--platform",
                    "windows",
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(0, completed.returncode, completed.stdout)
            clap_sha = distribution_manifest.tree_sha256(clap)
            manifest = json.loads((vst3 / "wrapper-manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(clap_sha, manifest["canonicalClapSha256"])
            self.assertEqual([], distribution_manifest.validate_wrapper_bundle("vst3", vst3, "windows", clap_sha))


if __name__ == "__main__":
    unittest.main()
