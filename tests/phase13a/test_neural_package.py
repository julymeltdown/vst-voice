from __future__ import annotations

import hashlib
import json
import os
import subprocess
import shutil
import tempfile
import unittest
from pathlib import Path

from tools.phase13a.neural_package import build_neural_package_manifest
from tools.phase13a.payload_paths import PayloadAssemblyError


class NeuralPackageTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("SEAM_NEURAL_PACKAGE_PROBE"), "native probe not supplied")
    def test_native_loader_reads_only_its_module_anchored_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as unrelated:
            root = Path(directory)
            module = root / "Contents/MacOS/module.exe"
            helper = root / "Contents/Helpers/helper.exe"
            manifest = root / "Contents/Resources/neural-helper-package.json"
            for path in (module, helper, manifest):
                path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(os.environ["SEAM_NEURAL_PACKAGE_PROBE"], module)
            shutil.copy2(os.environ["SEAM_NEURAL_PACKAGE_PROBE"], helper)
            runtime = root / "Contents/Helpers/runtime.bin"
            runtime.write_bytes(b"runtime-fixture")
            raw, digest = build_neural_package_manifest(root, "loader-build", module.relative_to(root).as_posix(),
                helper.relative_to(root).as_posix(), (runtime.relative_to(root).as_posix(),))
            manifest.write_bytes(raw)
            command = [str(module), "--seam-neural-package-load-probe", module.relative_to(root).as_posix(),
                       manifest.relative_to(root).as_posix(), digest, "loader-build"]
            environment = dict(os.environ, PATH="/nonexistent")
            def run(args: list[str]) -> subprocess.CompletedProcess[bytes]:
                return subprocess.run(args, cwd=unrelated, env=environment, capture_output=True, timeout=10, check=False)
            accepted = run(command)
            self.assertEqual(accepted.returncode, 0, accepted.stderr)
            for index, replacement in ((2, "wrong/module.exe"), (3, "../outside.json"),
                                       (4, "0" * 64), (5, "other-build")):
                bad = command.copy()
                bad[index] = replacement
                self.assertNotEqual(run(bad).returncode, 0)
            manifest.write_bytes(raw + b" ")
            self.assertNotEqual(run(command).returncode, 0)
            manifest.write_bytes(raw)
            runtime.write_bytes(b"substituted-runtime")
            self.assertNotEqual(run(command).returncode, 0)

    @unittest.skipUnless(os.environ.get("SEAM_NEURAL_PACKAGE_PROBE"), "native probe not supplied")
    def test_native_decoder_accepts_generated_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("module", "helper", "runtime"):
                (root / name).write_bytes(name.encode())
            raw, digest = build_neural_package_manifest(root, "build-1", "module", "helper", ("runtime",))
            command = [os.environ["SEAM_NEURAL_PACKAGE_PROBE"], "--seam-neural-package-probe", digest]
            accepted = subprocess.run(command, input=raw, capture_output=True, timeout=10, check=False)
            self.assertEqual(accepted.returncode, 0, accepted.stderr)
            changed = subprocess.run(command, input=raw + b" ", capture_output=True, timeout=10, check=False)
            self.assertNotEqual(changed.returncode, 0)

    def test_manifest_binds_actual_files_deterministically(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("module", "helper", "runtime"):
                (root / name).write_bytes(name.encode())
            first = build_neural_package_manifest(root, "build-1", "module", "helper", ("runtime",))
            self.assertEqual(first, build_neural_package_manifest(root, "build-1", "module", "helper", ("runtime",)))
            raw, digest = first
            self.assertEqual(hashlib.sha256(raw).hexdigest(), digest)
            value = json.loads(raw)
            self.assertEqual(value["formatId"], "com.project-seam.neural-helper-package")
            self.assertEqual(value["helper"]["sha256"], hashlib.sha256(b"helper").hexdigest())
            (root / "runtime").write_bytes(b"changed")
            changed = build_neural_package_manifest(root, "build-1", "module", "helper", ("runtime",))
            self.assertNotEqual(first, changed)
            self.assertEqual(json.loads(changed[0])["helper"], value["helper"])

    def test_rejects_missing_unsafe_duplicate_and_redirected_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "module").write_bytes(b"module")
            (root / "helper").write_bytes(b"helper")
            for name in ("../helper", "/helper", "C:helper", "a//b", "a/./b", "a\\b", "missing", "helper\x00"):
                with self.subTest(name=name), self.assertRaises((PayloadAssemblyError, OSError)):
                    build_neural_package_manifest(root, "build", "module", name, ())
            with self.assertRaises(PayloadAssemblyError):
                build_neural_package_manifest(root, "build", "module", "helper", ("helper",))
            with self.assertRaises(PayloadAssemblyError):
                build_neural_package_manifest(root, "build", "module", "helper", ("runtime",) * 65)
            for build in ("", "a" * 257, "bad\x00build"):
                with self.assertRaises(PayloadAssemblyError):
                    build_neural_package_manifest(root, build, "module", "helper", ())
            (root / "directory").mkdir()
            with self.assertRaises(PayloadAssemblyError):
                build_neural_package_manifest(root, "build", "module", "directory", ())
            try:
                (root / "redirect").symlink_to(root / "helper")
            except OSError:
                return  # Platforms without symlink permission retain the other checks.
            with self.assertRaises(PayloadAssemblyError):
                build_neural_package_manifest(root, "build", "module", "redirect", ())


if __name__ == "__main__":
    unittest.main()
