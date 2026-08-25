from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.external_beta import release_gate  # noqa: E402
from tools.phase13a.release_identity import read_project_version  # noqa: E402


class ExternalBetaReleaseIdentityTests(unittest.TestCase):
    def test_generated_header_identity_is_parsed_as_one_release_identity(self) -> None:
        header = (
            'inline constexpr std::string_view kApplicationVersion{"0.13.1"};\n'
            'inline constexpr std::string_view kBuildId{"candidate-build-001"};\n'
            'inline constexpr std::string_view kSourceCommit{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};\n'
            'inline constexpr std::uint64_t kBuildEpoch{1755768000ULL};\n'
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "version.hpp"
            path.write_text(header, encoding="utf-8")
            identity = release_gate.read_generated_identity(path)
        self.assertEqual("0.13.1", identity.version)
        self.assertEqual("candidate-build-001", identity.build_id)
        self.assertEqual("a" * 40, identity.source_commit)
        self.assertEqual(1_755_768_000, identity.build_epoch)

    def test_root_release_identity_uses_cmake_project_version(self) -> None:
        identity = release_gate.read_source_identity(ROOT)
        self.assertEqual("Project SEAM", identity.product)
        self.assertEqual("0.13.1", identity.version)

    def test_project_version_reader_uses_the_requested_source_tree(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text(
                "project(ProjectSEAM VERSION 7.8.9 LANGUAGES CXX)\n",
                encoding="utf-8",
            )
            self.assertEqual("7.8.9", read_project_version(root))

    def test_release_surfaces_do_not_embed_the_previous_version(self) -> None:
        release_surfaces = (
            ROOT / "apps/seam-clap-editor-host/main.cpp",
            ROOT / "libs/seam-clap-editor/src/plugin_entry.cpp",
            ROOT / "packaging/macos/ProjectSEAMEditor-Info.plist",
            ROOT / "packaging/phase13a/wrapper-project/CMakeLists.txt",
            ROOT / "scripts/build_phase13a_formats.py",
            ROOT / "scripts/generate_phase13a_evidence.py",
            ROOT / "scripts/generate_phase13b_evidence.py",
            ROOT / "scripts/package_macos_clap.sh",
            ROOT / "scripts/package_macos_installer.sh",
            ROOT / ".github/workflows/phase13a-plugin-formats.yml",
            ROOT / ".github/workflows/phase13a-distribution.yml",
        )
        for path in release_surfaces:
            with self.subTest(path=path.relative_to(ROOT)):
                contents = path.read_text(encoding="utf-8")
                self.assertNotIn("0.11.0", contents)
                self.assertNotIn("0.13.0", contents)

    def test_reachable_legacy_macos_packagers_use_the_canonical_identity(self) -> None:
        clap_packager = (ROOT / "scripts/package_macos_clap.sh").read_text(
            encoding="utf-8"
        )
        installer = (ROOT / "scripts/package_macos_installer.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn("tools/phase13a/release_identity.py", clap_packager)
        self.assertIn("ProjectSEAMEditor-Info.plist.in", clap_packager)
        self.assertIn("output .clap bundle already exists", clap_packager)
        self.assertNotIn('rm -rf "$bundle"', clap_packager)
        self.assertIn("tools/phase13a/release_identity.py", installer)
        self.assertIn("CFBundleShortVersionString", installer)

    def test_release_evidence_generators_use_the_source_version(self) -> None:
        for path in (
            ROOT / "scripts/generate_phase13a_evidence.py",
            ROOT / "scripts/generate_phase13b_evidence.py",
        ):
            with self.subTest(path=path.relative_to(ROOT)):
                source = path.read_text(encoding="utf-8")
                self.assertIn("read_project_version", source)
                self.assertNotIn('"0.13.1"', source)
                self.assertNotIn("'0.13.1'", source)
                self.assertNotIn('"0.13.0"', source)
                self.assertNotIn("'0.13.0'", source)

    def test_clap_descriptor_uses_the_generated_application_version(self) -> None:
        source = (ROOT / "libs/seam-clap-editor/src/plugin_entry.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn('#include "seam/build/version.hpp"', source)
        self.assertIn(".version = build::kApplicationVersion.data()", source)

    def test_clap_runtime_probe_rejects_descriptor_version_drift(self) -> None:
        source = (ROOT / "apps/seam-clap-editor-host/main.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn('#include "seam/build/version.hpp"', source)
        self.assertIn("descriptor->version", source)
        self.assertIn("seam::build::kApplicationVersion.data()", source)

    def test_identity_mismatch_is_rejected(self) -> None:
        expected = release_gate.ReleaseIdentity(
            product="Project SEAM",
            version="0.13.1",
            build_id="candidate-build-001",
            source_commit="a" * 40,
            build_epoch=1_755_768_000,
        )
        actual = release_gate.ReleaseIdentity(
            product="Project SEAM",
            version="0.13.0",
            build_id="candidate-build-001",
            source_commit="a" * 40,
            build_epoch=1_755_768_000,
        )
        self.assertEqual([], release_gate.compare_identity(expected, expected))
        self.assertTrue(any("version" in item for item in release_gate.compare_identity(expected, actual)))


if __name__ == "__main__":
    unittest.main()
