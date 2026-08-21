from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.external_beta import release_gate


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
