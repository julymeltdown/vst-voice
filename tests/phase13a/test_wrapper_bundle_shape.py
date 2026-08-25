import json
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))

import distribution_manifest  # noqa: E402


class WrapperBundleShapeTests(unittest.TestCase):
    def _windows_bundle(self, root):
        bundle = root / "ProjectSEAMEditor.vst3"
        binary = bundle / "Contents" / "x86_64-win" / "ProjectSEAMEditor.vst3"
        binary.parent.mkdir(parents=True)
        binary.write_bytes(b"MZ" + b"binary")
        (bundle / "moduleinfo.json").write_text("{}\n", encoding="utf-8")
        manifest = distribution_manifest.build_wrapper_manifest("VST3", "windows", "x64", "0.14.0", "com.project-seam.editor.vst3", "a" * 64, bundle)
        (bundle / "wrapper-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        return bundle

    def test_windows_package_requires_nested_binary_moduleinfo_and_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = self._windows_bundle(Path(directory))
            self.assertEqual([], distribution_manifest.validate_wrapper_bundle("vst3", bundle, "windows", "a" * 64))
            (bundle / "moduleinfo.json").unlink()
            self.assertTrue(any("moduleinfo" in error for error in distribution_manifest.validate_wrapper_bundle("vst3", bundle, "windows", "a" * 64)))

    def test_mismatched_canonical_clap_hash_is_blocked(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = self._windows_bundle(Path(directory))
            errors = distribution_manifest.validate_wrapper_bundle("vst3", bundle, "windows", "b" * 64)
            self.assertTrue(any("canonical CLAP hash" in error for error in errors))

    def test_windows_single_file_is_not_a_package(self):
        with tempfile.TemporaryDirectory() as directory:
            plugin = Path(directory) / "ProjectSEAMEditor.vst3"
            plugin.write_bytes(b"MZ")
            self.assertTrue(distribution_manifest.validate_wrapper_bundle("vst3", plugin, "windows", "a" * 64))


if __name__ == "__main__":
    unittest.main()
