import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))
import distribution_manifest  # noqa: E402


class Vst3TestHostRunnerTests(unittest.TestCase):
    def _plugin(self, root):
        plugin = root / "ProjectSEAMEditor.vst3"
        binary = plugin / "Contents" / "x86_64-linux" / "ProjectSEAMEditor.so"
        binary.parent.mkdir(parents=True)
        binary.write_bytes(b"ELF")
        manifest = distribution_manifest.build_wrapper_manifest("VST3", "linux", "x86_64", "0.14.0", "com.project-seam.editor.vst3", "a" * 64, plugin)
        (plugin / "wrapper-manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        return plugin

    def test_test_host_receives_plugin_and_scenario_environment(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plugin = self._plugin(root)
            host = root / "VST3PluginTestHost"
            host.write_text("#!/usr/bin/env python3\nimport os,sys\nassert os.environ['SEAM_VST3_PLUGIN'].endswith('ProjectSEAMEditor.vst3')\nassert os.environ['SEAM_VST3_SCENARIO'].endswith('scenario.json')\nprint('lifecycle PASS')\n", encoding="utf-8")
            host.chmod(0o755)
            scenario = root / "scenario.json"
            scenario.write_text(json.dumps({"checks": ["scan", "instantiate", "editorOpen", "editorResize", "editorClose", "process", "stateSave", "stateRestore", "unload"]}), encoding="utf-8")
            output = root / "evidence"
            result = subprocess.run([sys.executable, str(ROOT / "scripts/run_vst3_test_host.py"), "--host", str(host), "--plugin", str(plugin), "--scenario", str(scenario), "--platform", "linux", "--output", str(output)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
            self.assertEqual(0, result.returncode, result.stdout)
            self.assertEqual("PASS", json.loads((output / "result.json").read_text(encoding="utf-8"))["status"])

    def test_missing_host_is_not_run(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plugin = self._plugin(root)
            output = root / "evidence"
            result = subprocess.run([sys.executable, str(ROOT / "scripts/run_vst3_test_host.py"), "--host", str(root / "missing"), "--plugin", str(plugin), "--platform", "linux", "--output", str(output)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
            self.assertNotEqual(0, result.returncode)
            payload = json.loads((output / "result.json").read_text(encoding="utf-8"))
            self.assertEqual("NOT_RUN", payload["status"])


if __name__ == "__main__":
    unittest.main()
