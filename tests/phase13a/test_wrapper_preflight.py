import json
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))

import wrapper_preflight  # noqa: E402


class WrapperPreflightTests(unittest.TestCase):
    def _lock(self):
        return json.loads((ROOT / "phase13a/dependency-lock.json").read_text(encoding="utf-8"))

    def _checkouts(self, root):
        root.mkdir(parents=True)
        for dependency in self._lock()["dependencies"]:
            checkout = root / dependency["name"]
            checkout.mkdir()
            (checkout / ".phase13a-revision").write_text(dependency["commit"], encoding="utf-8")
            (checkout / "LICENSE").write_text("permissive\n", encoding="utf-8")

    def test_exact_offline_folder_preflight_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dependencies = root / "deps"
            self._checkouts(dependencies)
            errors, result = wrapper_preflight.validate_preflight(
                lock=self._lock(),
                dependencies=dependencies,
                wrapper_project=ROOT / "packaging/phase13a/wrapper-project/CMakeLists.txt",
                target="macos",
                architecture="arm64",
                version="0.14.0",
                build_id="external-beta.20260821.1",
                source_commit="a" * 40,
                auv2=True,
            )
            self.assertEqual([], errors)
            self.assertEqual("PASS", result["status"])
            self.assertFalse(result["networkDownloads"])

    def test_single_file_wrapper_setting_is_blocked(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dependencies = root / "deps"
            self._checkouts(dependencies)
            project = root / "CMakeLists.txt"
            project.write_text("set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES OFF)\nset(CLAP_WRAPPER_COPY_AFTER_BUILD OFF)\nset(CLAP_WRAPPER_WINDOWS_SINGLE_FILE ON)\n", encoding="utf-8")
            errors, _ = wrapper_preflight.validate_preflight(
                lock=self._lock(), dependencies=dependencies, wrapper_project=project,
                target="windows", architecture="x64", version="0.14.0", build_id="build-1", source_commit="a" * 40, auv2=False,
            )
            self.assertTrue(any("single-file" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
