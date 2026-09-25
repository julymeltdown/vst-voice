import hashlib
import tempfile
from types import SimpleNamespace
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.voice_model_training import training_environment


class _Distribution:
    def __init__(self, root, name, files):
        self.root = Path(root)
        self.metadata = {"Name": name}
        self.version = "1.0"
        self.files = [Path(path) for path in files]

    def locate_file(self, item):
        return self.root / item


class TrainingEnvironmentTests(unittest.TestCase):
    def capture(self, root, executable, distributions, loaded_modules=None):
        site_packages = Path(root) / "site-packages"
        site_packages.mkdir(parents=True, exist_ok=True)
        # The full training suite loads real numerical modules before these fixture
        # environments run. Only the modules supplied by this test belong to the
        # synthetic prefix being inspected.
        numerical_modules = {name: None for name in ("numpy", "scipy", "torch", "onnxruntime")}
        numerical_modules.update(loaded_modules or {})
        with patch.object(training_environment.sys, "prefix", str(root)), \
                patch.object(training_environment.sys, "executable", str(executable)), \
                patch.object(training_environment.sysconfig, "get_paths",
                             return_value={"purelib": str(site_packages), "platlib": str(site_packages)}), \
                patch.object(training_environment.importlib.metadata, "distributions",
                             return_value=distributions), \
                patch.object(training_environment.platform, "platform", return_value="test-platform"), \
                patch.object(training_environment.platform, "machine", return_value="test-machine"), \
                patch.dict(training_environment.sys.modules, numerical_modules):
            return training_environment.capture_environment()

    def test_capture_is_canonical_and_binds_package_contents(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "python"
            executable.write_bytes(b"python runtime")
            package_file = root / "site-packages" / "sample.py"
            package_file.parent.mkdir()
            package_file.write_bytes(b"package v1")
            distributions = [_Distribution(root, "sample", ["site-packages/sample.py"])]

            first = self.capture(root, executable, distributions)
            second = self.capture(root, executable, distributions)
            self.assertEqual(first, second)
            expected = hashlib.sha256(b"package v1").hexdigest()
            self.assertEqual(first["packages"][0]["filesSha256"], hashlib.sha256(
                training_environment._canonical([["site-packages/sample.py", 10, expected]])).hexdigest())
            package_file.write_bytes(b"package v2")
            changed = self.capture(root, executable, distributions)
            self.assertNotEqual(first["environmentSha256"], changed["environmentSha256"])

    def test_shared_file_is_included_in_each_distribution_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "python"
            executable.write_bytes(b"runtime")
            package_file = root / "site-packages" / "shared.py"
            package_file.parent.mkdir()
            package_file.write_bytes(b"shared")
            distributions = [
                _Distribution(root, "first", ["site-packages/shared.py"]),
                _Distribution(root, "second", ["site-packages/shared.py"]),
            ]
            captured = self.capture(root, executable, distributions)
            self.assertEqual([entry["fileCount"] for entry in captured["packages"]], [1, 1])

    def test_out_of_prefix_distribution_files_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            root, external = base / "inside", base / "outside"
            root.mkdir()
            external.mkdir()
            executable = root / "python"
            executable.write_bytes(b"runtime")
            (external / "external.py").write_bytes(b"external")
            distributions = [_Distribution(root, "escape", ["../outside/external.py"])]
            with self.assertRaisesRegex(ValueError, "outside the active environment"):
                self.capture(root, executable, distributions)

    def test_loaded_numerical_module_outside_environment_fails_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            root, external = base / "inside", base / "outside"
            root.mkdir()
            external.mkdir()
            executable = root / "python"
            executable.write_bytes(b"runtime")
            (root / "site-packages").mkdir()
            (root / "site-packages" / "sample.py").write_bytes(b"sample")
            (external / "torch.py").write_bytes(b"external torch")
            distributions = [_Distribution(root, "sample", ["site-packages/sample.py"])]

            with self.assertRaisesRegex(ValueError, "resolves outside the active environment: torch"):
                self.capture(root, executable, distributions,
                             loaded_modules={"torch": SimpleNamespace(__file__=str(external / "torch.py"))})

    def test_loaded_numerical_module_must_be_in_hashed_distribution_inventory(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "python"
            executable.write_bytes(b"runtime")
            site_packages = root / "site-packages"
            package_file = site_packages / "torch" / "__init__.py"
            package_file.parent.mkdir(parents=True)
            package_file.write_bytes(b"untracked torch")
            distributions = [_Distribution(root, "sample", ["site-packages/sample.py"])]
            (site_packages / "sample.py").write_bytes(b"sample")

            with self.assertRaisesRegex(ValueError, "absent from the hashed package inventory: torch"):
                self.capture(root, executable, distributions,
                             loaded_modules={"torch": SimpleNamespace(__file__=str(package_file))})

    def test_distinct_platlib_distribution_is_captured_and_binds_loaded_module(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "python"
            executable.write_bytes(b"runtime")
            purelib = root / "purelib"
            platlib = root / "platlib"
            (purelib / "site-packages").mkdir(parents=True)
            package_file = platlib / "torch" / "__init__.py"
            package_file.parent.mkdir(parents=True)
            package_file.write_bytes(b"torch runtime")
            distribution = _Distribution(root, "torch", ["platlib/torch/__init__.py"])

            def discover(path):
                return [] if path == [str(purelib)] else [distribution]

            with patch.object(training_environment.sys, "prefix", str(root)), \
                    patch.object(training_environment.sys, "executable", str(executable)), \
                    patch.object(training_environment.sysconfig, "get_paths",
                                 return_value={"purelib": str(purelib), "platlib": str(platlib)}), \
                    patch.object(training_environment.importlib.metadata, "distributions",
                                 side_effect=discover), \
                    patch.object(training_environment.platform, "platform", return_value="test-platform"), \
                    patch.object(training_environment.platform, "machine", return_value="test-machine"), \
                    patch.dict(training_environment.sys.modules,
                               {"numpy": None, "scipy": None, "onnxruntime": None,
                                "torch": SimpleNamespace(__file__=str(package_file))}):
                captured = training_environment.capture_environment()

            self.assertEqual(captured["distributionCount"], 1)
            self.assertEqual(captured["runtime"]["importedNumericalModules"],
                             {"torch": "platlib/torch/__init__.py"})


if __name__ == "__main__":
    unittest.main()
