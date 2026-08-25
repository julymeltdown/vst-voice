import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))
import distribution_manifest  # noqa: E402


class Vst3ValidatorRunnerTests(unittest.TestCase):
    def test_preflight_failure_preserves_empty_stderr_log(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "evidence"
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts/run_vst3_validator.py"),
                    "--validator", str(root / "missing-validator"),
                    "--plugin", str(root / "missing-plugin.vst3"),
                    "--output", str(output),
                    "--platform", "linux",
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
            self.assertNotEqual(0, result.returncode)
            self.assertTrue((output / "validator.stderr.log").is_file())
            self.assertEqual("", (output / "validator.stderr.log").read_text(encoding="utf-8"))

    def test_clap_path_is_forwarded_to_validator_process(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plugin = root / 'ProjectSEAMEditor.vst3'
            binary = plugin / 'Contents' / 'x86_64-linux' / 'ProjectSEAMEditor.so'
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b'not-empty')
            wrapper_manifest = distribution_manifest.build_wrapper_manifest(
                'VST3', 'linux', 'x86_64', '0.14.0', 'com.project-seam.editor.vst3', 'a' * 64, plugin
            )
            (plugin / 'wrapper-manifest.json').write_text(json.dumps(wrapper_manifest), encoding='utf-8')
            clap_path = root / 'CLAP'
            clap_path.mkdir()
            validator = root / 'validator'
            validator.write_text(
                '#!/usr/bin/env python3\n'
                'import os\n'
                f'expected={str(clap_path.resolve())!r}\n'
                'raise SystemExit(0 if os.environ.get("CLAP_PATH")==expected else 9)\n',
                encoding='utf-8',
            )
            validator.chmod(0o755)
            output = root / 'evidence'
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / 'scripts/run_vst3_validator.py'),
                    '--validator', str(validator),
                    '--plugin', str(plugin),
                    '--output', str(output),
                    '--clap-path', str(clap_path),
                    '--platform', 'linux',
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(0, result.returncode, result.stdout)
            payload = json.loads((output / 'result.json').read_text(encoding='utf-8'))
            self.assertEqual('PASS', payload['status'])
            self.assertEqual(str(clap_path.resolve()), payload['clapPath'])


if __name__ == '__main__':
    unittest.main()
