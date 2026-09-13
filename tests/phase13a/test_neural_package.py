from __future__ import annotations

import hashlib
import json
import os
import subprocess
import shutil
import tempfile
import unittest
from pathlib import Path

from tools.phase13a.neural_package import build_neural_package_manifest, build_neural_deployment_descriptor
from tools.phase13a.payload_surfaces import PayloadPlatform
from tools.phase13a.payload_paths import PayloadAssemblyError


class NeuralPackageTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("SEAM_NEURAL_PACKAGE_PROBE"), "native probe not supplied")
    def test_generated_deployment_loads_exact_package_version(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            module = root / "module"
            shutil.copy2(os.environ["SEAM_NEURAL_PACKAGE_PROBE"], module)
            (root / "helper").write_bytes(b"not-an-execution-qualified-helper")
            for version in (1, 2):
                raw, _ = build_neural_package_manifest(root, "build", "module", "helper", (), protocol_version=version)
                (root / "manifest.json").write_bytes(raw)
                descriptor, digest = build_neural_deployment_descriptor(
                    root, "build", PayloadPlatform.MACOS_ARM64, "standalone", "module", "manifest.json",
                    protocol_version=version)
                self.assertEqual(digest, hashlib.sha256(descriptor).hexdigest())
                command = [str(module), "--seam-neural-deployment-load-probe", "build", "macos-arm64", "standalone", str(version)]
                accepted = subprocess.run(command, input=descriptor, capture_output=True, timeout=10)
                self.assertEqual(accepted.returncode, 0, accepted.stderr)
                command[-1] = str(3 - version)
                self.assertNotEqual(subprocess.run(command, input=descriptor, capture_output=True, timeout=10).returncode, 0)
                with self.assertRaises(PayloadAssemblyError):
                    build_neural_deployment_descriptor(root, "build", PayloadPlatform.MACOS_ARM64,
                        "standalone", "module", "manifest.json", protocol_version=3 - version)
                # Even a newly signed descriptor cannot mask a package-version mismatch.
                other, _ = build_neural_package_manifest(root, "build", "module", "helper", (), protocol_version=3 - version)
                (root / "manifest.json").write_bytes(other)
                modified = json.loads(descriptor)
                modified["manifestSha256"] = hashlib.sha256(other).hexdigest()
                command[-1] = str(version)
                self.assertNotEqual(subprocess.run(command, input=json.dumps(modified).encode(), capture_output=True, timeout=10).returncode, 0)

    def test_deployment_builder_rejects_changed_files_and_invalid_targets(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "module").write_bytes(b"module")
            (root / "helper").write_bytes(b"helper")
            raw, _ = build_neural_package_manifest(root, "build", "module", "helper", ())
            (root / "manifest.json").write_bytes(raw)
            def build(**changes):
                options = dict(package_root=root, build_id="build", platform=PayloadPlatform.MACOS_ARM64,
                               surface="standalone", module="module", manifest_path="manifest.json")
                options.update(changes)
                return build_neural_deployment_descriptor(**options)
            self.assertEqual(build(), build())
            for options in (dict(build_id="other"), dict(surface="unknown"),
                            dict(platform=PayloadPlatform.WINDOWS_X64, surface="auv2"),
                            dict(manifest_path="../manifest.json"), dict(manifest_path="a//b"),
                            dict(protocol_version=True), dict(module="helper")):
                with self.assertRaises(PayloadAssemblyError):
                    build(**options)
            (root / "helper").write_bytes(b"changed")
            with self.assertRaises(PayloadAssemblyError):
                build()

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
            raw_v2, digest_v2 = build_neural_package_manifest(root, "build-1", "module", "helper", ("runtime",), protocol_version=2)
            command[-1] = digest_v2
            accepted_v2 = subprocess.run(command, input=raw_v2, capture_output=True, timeout=10, check=False)
            self.assertEqual(accepted_v2.returncode, 0, accepted_v2.stderr)

    def test_manifest_binds_actual_files_deterministically(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("module", "helper", "runtime"):
                (root / name).write_bytes(name.encode())
            first = build_neural_package_manifest(root, "build-1", "module", "helper", ("runtime",))
            second = build_neural_package_manifest(root, "build-1", "module", "helper", ("runtime",), protocol_version=2)
            self.assertNotEqual(first, second)
            self.assertEqual(json.loads(second[0])["schemaVersion"], 2)
            self.assertEqual(json.loads(second[0])["protocolVersion"], 2)
            for invalid in (0, 3, True, 2.0, "2"):
                with self.assertRaises(PayloadAssemblyError):
                    build_neural_package_manifest(root, "build-1", "module", "helper", (), protocol_version=invalid)
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
