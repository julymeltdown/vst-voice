import json
import os
import plistlib
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))
import distribution_manifest  # noqa: E402


class AuvalRunnerTests(unittest.TestCase):
    def _component(self, root):
        component = root / "ProjectSEAMEditor.component"
        binary = component / "Contents" / "MacOS" / "ProjectSEAMEditor"
        resources = component / "Contents" / "Resources"
        binary.parent.mkdir(parents=True)
        resources.mkdir(parents=True)
        binary.write_bytes(b"Mach-O")
        plist = {
            "AudioComponents": [{"type": "aumu", "subtype": "SEAM", "manufacturer": "PSEM"}],
            "CFBundleIdentifier": "com.project-seam.editor.auv2",
        }
        (component / "Contents" / "Info.plist").write_bytes(plistlib.dumps(plist))
        manifest = distribution_manifest.build_wrapper_manifest("AUv2", "macos", "arm64", "0.14.0", "com.project-seam.editor.auv2", "a" * 64, component)
        (resources / "wrapper-manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        return component

    def test_runner_discovers_installed_component_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            component = self._component(root)
            bin_dir = root / "bin"
            bin_dir.mkdir()
            auval = bin_dir / "auval"
            auval.write_text("#!/bin/sh\n[ \"$1\" = -v ] && [ \"$2\" = aumu ] && [ \"$3\" = SEAM ] && [ \"$4\" = PSEM ]\n", encoding="utf-8")
            auval.chmod(0o755)
            output = root / "evidence"
            environment = os.environ.copy()
            environment["PATH"] = f"{bin_dir}:{environment['PATH']}"
            result = subprocess.run([sys.executable, str(ROOT / "scripts/run_auval.py"), "--component", str(component), "--output", str(output), "--platform", "macos"], env=environment, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
            self.assertEqual(0, result.returncode, result.stdout)
            payload = json.loads((output / "result.json").read_text(encoding="utf-8"))
            self.assertEqual("PASS", payload["status"])
            self.assertEqual("SEAM", payload["discovered"]["subtype"])

    def test_code_mismatch_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            component = self._component(root)
            output = root / "evidence"
            result = subprocess.run([sys.executable, str(ROOT / "scripts/run_auval.py"), "--component", str(component), "--type", "aumu", "--subtype", "WRNG", "--manufacturer", "PSEM", "--output", str(output), "--platform", "macos"], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
            self.assertNotEqual(0, result.returncode)
            payload = json.loads((output / "result.json").read_text(encoding="utf-8"))
            self.assertEqual("FAIL", payload["status"])
            self.assertEqual("artifact-contract", payload["failureClass"])


if __name__ == "__main__":
    unittest.main()
