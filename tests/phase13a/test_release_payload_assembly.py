from __future__ import annotations

import json
import hashlib
import plistlib
import subprocess
import tempfile
import unittest
from pathlib import Path

from scripts.assemble_release_payload import (
    PayloadAssemblyError,
    PayloadPlatform,
    assemble_release_payload,
)
from tools.phase13a.payload_manifest import verify_release_payload_manifest


class ReleasePayloadAssemblyTests(unittest.TestCase):
    def __init__(self, methodName: str = "runTest") -> None:
        super().__init__(methodName)
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.payload = self.root / "payload"
        self.commit = ""

    def setUp(self) -> None:
        self.source.mkdir()
        subprocess.run(["git", "init", "--quiet"], cwd=self.source, check=True)
        subprocess.run(
            ["git", "config", "user.email", "tests@project-seam.invalid"],
            cwd=self.source,
            check=True,
        )
        subprocess.run(
            ["git", "config", "user.name", "Project SEAM Tests"],
            cwd=self.source,
            check=True,
        )
        (self.source / "CMakeLists.txt").write_text(
            "project(ProjectSEAM VERSION 0.13.1 LANGUAGES CXX)\n",
            encoding="utf-8",
        )
        subprocess.run(["git", "add", "CMakeLists.txt"], cwd=self.source, check=True)
        subprocess.run(
            ["git", "commit", "--quiet", "-m", "fixture"],
            cwd=self.source,
            check=True,
        )
        self.commit = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=self.source, text=True
        ).strip()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write(self, relative: str, value: str = "fixture") -> Path:
        path = self.payload / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(value, encoding="utf-8")
        return path

    def create_payload(self, platform: PayloadPlatform) -> None:
        identity = {
            "product": "Project SEAM",
            "version": "0.13.1",
            "buildId": "0.13.1-test",
            "sourceCommit": self.commit,
            "buildEpoch": 1,
        }
        resource_inventory = json.dumps(
            {
                "schemaVersion": 1,
                "bundledVoicebanks": [],
                "voicebankHandoff": "per-user-installed-catalog",
                "characterPackage": {"required": False, "bundled": False},
            }
        )
        self.write(
            "RELEASE_IDENTITY.json",
            json.dumps(identity),
        )
        self.write("THIRD_PARTY_NOTICES.md")
        self.write("Notices/openssl-LICENSE.txt", "Apache-2.0")
        self.write("SBOM.spdx.json", '{"spdxVersion":"SPDX-2.3"}')
        self.write("Documentation/manual/USER_MANUAL.md")
        self.write("Documentation/support/SUPPORT.md")
        self.write("Documentation/external-beta-documentation.json", "{}")
        public_key = bytes(32)
        root_key_id = hashlib.sha256(public_key).hexdigest()
        self.write(
            "Trust/release-trust-roots.json",
            json.dumps(
                {
                    "schemaVersion": 1,
                    "purpose": "project-seam-release-trust-roots",
                    "testOnly": True,
                    "roots": [
                        {
                            "purpose": "update-root",
                            "keyId": root_key_id,
                            "publicKeyFile": "update-root-public-key.json",
                        }
                    ],
                }
            ),
        )
        self.write(
            "Trust/update-root-public-key.json",
            json.dumps(
                {
                    "type": "ed25519-public",
                    "schemaVersion": 1,
                    "keyId": root_key_id,
                    "publicKey": public_key.hex(),
                }
            ),
        )
        self.write("Ownership/installer-ownership.json", '{"ownedPaths":[]}')
        self.write(
            "release-dependency-closure.json",
            json.dumps({"schemaVersion": 1, "platform": platform, "status": "PASS"}),
        )
        if platform is PayloadPlatform.MACOS_ARM64:
            self.write("Standalone/Project SEAM.app/Contents/MacOS/Project SEAM")
            self.write(
                "Standalone/Project SEAM.app/Contents/Resources/release-resource-inventory.json",
                resource_inventory,
            )
            self.write(
                "Standalone/Project SEAM.app/Contents/Resources/RELEASE_IDENTITY.json",
                json.dumps(identity),
            )
            info = self.payload / "Standalone/Project SEAM.app/Contents/Info.plist"
            info.write_bytes(
                plistlib.dumps(
                    {
                        "CFBundleShortVersionString": identity["version"],
                        "ProjectSEAMBuildID": identity["buildId"],
                        "ProjectSEAMSourceCommit": identity["sourceCommit"],
                    }
                )
            )
            self.write("CLAP/ProjectSEAMEditor.clap/Contents/MacOS/ProjectSEAMEditor")
            self.write(
                "CLAP/ProjectSEAMEditor.clap/Contents/Resources/release-resource-inventory.json",
                resource_inventory,
            )
            self.write(
                "CLAP/ProjectSEAMEditor.clap/Contents/Resources/RELEASE_IDENTITY.json",
                json.dumps(identity),
            )
            self.write("VST3/ProjectSEAMEditor.vst3/Contents/MacOS/ProjectSEAMEditor")
            self.write(
                "AU/ProjectSEAMEditor.component/Contents/MacOS/ProjectSEAMEditor"
            )
            for relative in (
                "VST3/ProjectSEAMEditor.vst3/Contents/Resources/wrapper-manifest.json",
                "AU/ProjectSEAMEditor.component/Contents/Resources/wrapper-manifest.json",
            ):
                self.write(
                    relative,
                    json.dumps(
                        {"version": identity["version"], "releaseIdentity": identity}
                    ),
                )
            self.write("Tools/seam_installer_verifier")
        else:
            self.write("Standalone/seam_editor_native.exe")
            self.write(
                "Standalone/Resources/release-resource-inventory.json",
                resource_inventory,
            )
            self.write(
                "Standalone/Resources/RELEASE_IDENTITY.json",
                json.dumps(identity),
            )
            self.write("CLAP/ProjectSEAMEditor.clap")
            self.write(
                "CLAP/ProjectSEAMEditor.resources/release-resource-inventory.json",
                resource_inventory,
            )
            self.write(
                "CLAP/ProjectSEAMEditor.resources/RELEASE_IDENTITY.json",
                json.dumps(identity),
            )
            self.write(
                "VST3/ProjectSEAMEditor.vst3/Contents/x86_64-win/ProjectSEAMEditor.vst3"
            )
            self.write("VST3/ProjectSEAMEditor.vst3/moduleinfo.json", "{}")
            self.write(
                "VST3/ProjectSEAMEditor.vst3/wrapper-manifest.json",
                json.dumps(
                    {"version": identity["version"], "releaseIdentity": identity}
                ),
            )
            self.write("Tools/seam_installer_verifier.exe")

    def test_macos_payload_uses_one_identity_for_every_claimed_surface(self) -> None:
        self.create_payload(PayloadPlatform.MACOS_ARM64)

        result = assemble_release_payload(
            self.payload, self.source, PayloadPlatform.MACOS_ARM64
        )

        self.assertEqual(
            {"standalone", "clap", "vst3", "auv2", "installer-verifier"},
            set(result.surface_ids),
        )
        manifest = json.loads(result.path.read_text(encoding="utf-8"))
        self.assertEqual("0.13.1-test", manifest["releaseIdentity"]["buildId"])
        self.assertEqual(result.payload_sha256, manifest["payloadTreeSha256"])
        self.assertEqual("SBOM.spdx.json", manifest["sbom"]["path"])
        self.assertIn(
            "Notices/openssl-LICENSE.txt",
            {entry["path"] for entry in manifest["notices"]},
        )
        verified = verify_release_payload_manifest(
            self.payload, PayloadPlatform.MACOS_ARM64
        )
        self.assertEqual("0.13.1-test", verified.identity.build_id)

    def test_payload_manifest_rejects_bytes_changed_after_assembly(self) -> None:
        self.create_payload(PayloadPlatform.WINDOWS_X64)
        assemble_release_payload(self.payload, self.source, PayloadPlatform.WINDOWS_X64)
        self.write("Standalone/seam_editor_native.exe", "replaced")

        with self.assertRaisesRegex(PayloadAssemblyError, "standalone"):
            verify_release_payload_manifest(self.payload, PayloadPlatform.WINDOWS_X64)

    def test_macos_payload_rejects_a_missing_standalone(self) -> None:
        self.create_payload(PayloadPlatform.MACOS_ARM64)
        standalone = self.payload / "Standalone/Project SEAM.app"
        for child in sorted(standalone.rglob("*"), reverse=True):
            child.unlink() if child.is_file() else child.rmdir()
        standalone.rmdir()

        with self.assertRaisesRegex(PayloadAssemblyError, "standalone"):
            assemble_release_payload(
                self.payload, self.source, PayloadPlatform.MACOS_ARM64
            )

    def test_payload_rejects_a_missing_openssl_license_notice(self) -> None:
        self.create_payload(PayloadPlatform.MACOS_ARM64)
        (self.payload / "Notices/openssl-LICENSE.txt").unlink()

        with self.assertRaisesRegex(PayloadAssemblyError, "openssl-LICENSE"):
            assemble_release_payload(
                self.payload, self.source, PayloadPlatform.MACOS_ARM64
            )

    def test_payload_rejects_every_required_supporting_input(self) -> None:
        required = (
            "THIRD_PARTY_NOTICES.md",
            "Notices/openssl-LICENSE.txt",
            "SBOM.spdx.json",
            "Documentation/manual/USER_MANUAL.md",
            "Documentation/support/SUPPORT.md",
            "Documentation/external-beta-documentation.json",
            "Trust/release-trust-roots.json",
            "Trust/update-root-public-key.json",
            "Ownership/installer-ownership.json",
            "release-dependency-closure.json",
        )
        for index, relative in enumerate(required):
            with self.subTest(relative=relative):
                self.payload = self.root / f"payload-required-{index}"
                self.create_payload(PayloadPlatform.MACOS_ARM64)
                (self.payload / relative).unlink()
                with self.assertRaises(PayloadAssemblyError):
                    assemble_release_payload(
                        self.payload, self.source, PayloadPlatform.MACOS_ARM64
                    )

    def test_payload_rejects_a_missing_referenced_trust_root(self) -> None:
        self.create_payload(PayloadPlatform.MACOS_ARM64)
        (self.payload / "Trust/update-root-public-key.json").unlink()

        with self.assertRaisesRegex(PayloadAssemblyError, "update-root-public-key"):
            assemble_release_payload(
                self.payload, self.source, PayloadPlatform.MACOS_ARM64
            )

    def test_payload_rejects_an_extra_trust_sibling(self) -> None:
        self.create_payload(PayloadPlatform.MACOS_ARM64)
        self.write("Trust/retired-root-private-key.json", "secret")

        with self.assertRaisesRegex(PayloadAssemblyError, "trust contains undeclared"):
            assemble_release_payload(
                self.payload, self.source, PayloadPlatform.MACOS_ARM64
            )

    def test_verifier_rejects_a_trust_sibling_added_after_assembly(self) -> None:
        self.create_payload(PayloadPlatform.WINDOWS_X64)
        assemble_release_payload(self.payload, self.source, PayloadPlatform.WINDOWS_X64)
        self.write("Trust/retired-root.json")

        with self.assertRaisesRegex(PayloadAssemblyError, "trust contains undeclared"):
            verify_release_payload_manifest(self.payload, PayloadPlatform.WINDOWS_X64)

    def test_assembly_and_verification_reject_a_symlinked_payload_root(self) -> None:
        self.create_payload(PayloadPlatform.WINDOWS_X64)
        linked = self.root / "payload-link"
        try:
            linked.symlink_to(self.payload, target_is_directory=True)
        except OSError as error:
            self.skipTest(f"symlinks unavailable: {error}")

        with self.assertRaisesRegex(PayloadAssemblyError, "symbolic link"):
            assemble_release_payload(linked, self.source, PayloadPlatform.WINDOWS_X64)
        assemble_release_payload(self.payload, self.source, PayloadPlatform.WINDOWS_X64)
        with self.assertRaisesRegex(PayloadAssemblyError, "symbolic link"):
            verify_release_payload_manifest(linked, PayloadPlatform.WINDOWS_X64)

    def test_windows_payload_rejects_a_missing_claimed_plugin(self) -> None:
        self.create_payload(PayloadPlatform.WINDOWS_X64)
        (self.payload / "CLAP/ProjectSEAMEditor.clap").unlink()

        with self.assertRaisesRegex(PayloadAssemblyError, "clap"):
            assemble_release_payload(
                self.payload, self.source, PayloadPlatform.WINDOWS_X64
            )

    def test_payload_rejects_a_dirty_source_checkout(self) -> None:
        self.create_payload(PayloadPlatform.WINDOWS_X64)
        (self.source / "dirty.txt").write_text("dirty", encoding="utf-8")

        with self.assertRaisesRegex(PayloadAssemblyError, "dirty"):
            assemble_release_payload(
                self.payload, self.source, PayloadPlatform.WINDOWS_X64
            )

    def test_payload_rejects_a_mismatched_source_identity(self) -> None:
        self.create_payload(PayloadPlatform.WINDOWS_X64)
        identity_path = self.payload / "RELEASE_IDENTITY.json"
        identity = json.loads(identity_path.read_text(encoding="utf-8"))
        identity["sourceCommit"] = "f" * 40
        identity_path.write_text(json.dumps(identity), encoding="utf-8")

        with self.assertRaisesRegex(PayloadAssemblyError, "sourceCommit"):
            assemble_release_payload(
                self.payload, self.source, PayloadPlatform.WINDOWS_X64
            )

    def test_payload_rejects_a_mismatched_product_identity(self) -> None:
        self.create_payload(PayloadPlatform.WINDOWS_X64)
        identity_path = self.payload / "RELEASE_IDENTITY.json"
        identity = json.loads(identity_path.read_text(encoding="utf-8"))
        identity["product"] = "Different Product"
        identity_path.write_text(json.dumps(identity), encoding="utf-8")

        with self.assertRaisesRegex(PayloadAssemblyError, "identity product"):
            assemble_release_payload(
                self.payload, self.source, PayloadPlatform.WINDOWS_X64
            )

    def test_payload_rejects_a_wrapper_with_a_different_build_identity(self) -> None:
        self.create_payload(PayloadPlatform.WINDOWS_X64)
        wrapper = self.payload / "VST3/ProjectSEAMEditor.vst3/wrapper-manifest.json"
        value = json.loads(wrapper.read_text(encoding="utf-8"))
        value["releaseIdentity"]["buildId"] = "different-build"
        wrapper.write_text(json.dumps(value), encoding="utf-8")

        with self.assertRaisesRegex(PayloadAssemblyError, "VST3.*identity"):
            assemble_release_payload(
                self.payload, self.source, PayloadPlatform.WINDOWS_X64
            )

    def test_payload_rejects_an_undeclared_platform_surface(self) -> None:
        self.create_payload(PayloadPlatform.MACOS_ARM64)
        self.write("ProjectSEAMEditor.component/Contents/MacOS/ProjectSEAMEditor")

        with self.assertRaisesRegex(PayloadAssemblyError, "undeclared top-level"):
            assemble_release_payload(
                self.payload, self.source, PayloadPlatform.MACOS_ARM64
            )

    def test_payload_rejects_an_extra_bundle_inside_a_surface_root(self) -> None:
        self.create_payload(PayloadPlatform.MACOS_ARM64)
        self.write("AU/Unexpected.component/Contents/MacOS/Unexpected")

        with self.assertRaisesRegex(PayloadAssemblyError, "au contains undeclared"):
            assemble_release_payload(
                self.payload, self.source, PayloadPlatform.MACOS_ARM64
            )

    def test_verifier_rejects_an_extra_tool_added_after_assembly(self) -> None:
        self.create_payload(PayloadPlatform.WINDOWS_X64)
        assemble_release_payload(self.payload, self.source, PayloadPlatform.WINDOWS_X64)
        self.write("Tools/diagnostic-helper.exe")

        with self.assertRaisesRegex(PayloadAssemblyError, "tools contains undeclared"):
            verify_release_payload_manifest(self.payload, PayloadPlatform.WINDOWS_X64)

    def test_verifier_rejects_release_eligibility_and_bank_handoff_tampering(
        self,
    ) -> None:
        mutations = (
            ("releaseEligible", True),
            ("sourceClean", False),
            ("bankSidecar", {"distribution": "bundled"}),
        )
        for index, (field, replacement) in enumerate(mutations):
            with self.subTest(field=field):
                self.payload = self.root / f"payload-semantic-{index}"
                self.create_payload(PayloadPlatform.WINDOWS_X64)
                result = assemble_release_payload(
                    self.payload, self.source, PayloadPlatform.WINDOWS_X64
                )
                manifest = json.loads(result.path.read_text(encoding="utf-8"))
                manifest[field] = replacement
                result.path.write_text(json.dumps(manifest), encoding="utf-8")
                with self.assertRaisesRegex(
                    PayloadAssemblyError, "eligibility or bank handoff"
                ):
                    verify_release_payload_manifest(
                        self.payload, PayloadPlatform.WINDOWS_X64
                    )

    def test_verifier_rejects_a_manifest_entry_that_escapes_the_payload(self) -> None:
        self.create_payload(PayloadPlatform.WINDOWS_X64)
        result = assemble_release_payload(
            self.payload, self.source, PayloadPlatform.WINDOWS_X64
        )
        manifest = json.loads(result.path.read_text(encoding="utf-8"))
        manifest["documents"][0]["path"] = "../outside.txt"
        result.path.write_text(json.dumps(manifest), encoding="utf-8")

        with self.assertRaisesRegex(PayloadAssemblyError, "unsafe"):
            verify_release_payload_manifest(self.payload, PayloadPlatform.WINDOWS_X64)


if __name__ == "__main__":
    unittest.main()
