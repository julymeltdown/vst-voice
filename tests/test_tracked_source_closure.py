from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts.verify_tracked_source_closure import audit_source_closure  # noqa: E402


class TrackedSourceClosureTests(unittest.TestCase):
    def initialize(self, root: Path) -> None:
        subprocess.run(["git", "init", "--quiet"], cwd=root, check=True)
        (root / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.25)\n", encoding="utf-8"
        )
        subprocess.run(["git", "add", "CMakeLists.txt"], cwd=root, check=True)

    def test_existing_source_missing_from_index_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.initialize(root)
            source = root / "libs/example/src/example.cpp"
            source.parent.mkdir(parents=True)
            source.write_text("int example() { return 1; }\n", encoding="utf-8")

            issues = audit_source_closure(root)

        self.assertTrue(
            any(
                issue.path == "libs/example/src/example.cpp"
                and issue.problem == "not indexed"
                for issue in issues
            )
        )

    def test_workflow_reference_reports_its_consumer(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.initialize(root)
            workflow = root / ".github/workflows/ci.yml"
            workflow.parent.mkdir(parents=True)
            workflow.write_text(
                "steps:\n  - run: python3 scripts/check_release.py\n",
                encoding="utf-8",
            )
            script = root / "scripts/check_release.py"
            script.parent.mkdir(parents=True)
            script.write_text("raise SystemExit(0)\n", encoding="utf-8")
            subprocess.run(
                ["git", "add", ".github/workflows/ci.yml"], cwd=root, check=True
            )

            issues = audit_source_closure(root)

        matching = [issue for issue in issues if issue.path == "scripts/check_release.py"]
        self.assertTrue(matching)
        self.assertIn(".github/workflows/ci.yml", matching[0].consumer)

    def test_deleted_indexed_source_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.initialize(root)
            source = root / "tools/check.py"
            source.parent.mkdir(parents=True)
            source.write_text("raise SystemExit(0)\n", encoding="utf-8")
            subprocess.run(["git", "add", "tools/check.py"], cwd=root, check=True)
            source.unlink()

            issues = audit_source_closure(root)

        self.assertTrue(
            any(
                issue.path == "tools/check.py" and issue.problem == "missing"
                for issue in issues
            )
        )

    def test_generated_build_output_is_outside_the_source_graph(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.initialize(root)
            generated = root / "build-local/generated.cpp"
            generated.parent.mkdir(parents=True)
            generated.write_text("int generated = 1;\n", encoding="utf-8")

            issues = audit_source_closure(root)

        self.assertEqual([], issues)

    def test_python_bytecode_cache_is_outside_the_source_graph(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.initialize(root)
            cached = root / "tools/check/__pycache__/check.cpython-314.pyc"
            cached.parent.mkdir(parents=True)
            cached.write_bytes(b"local-bytecode")

            issues = audit_source_closure(root)

        self.assertEqual([], issues)

    def test_reference_extensions_are_not_matched_by_prefix(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.initialize(root)
            source = root / "libs/example.cpp"
            source.parent.mkdir(parents=True)
            source.write_text("int example() { return 1; }\n", encoding="utf-8")
            (root / "CMakeLists.txt").write_text(
                "add_library(example libs/example.cpp)\n", encoding="utf-8"
            )
            subprocess.run(
                ["git", "add", "CMakeLists.txt", "libs/example.cpp"],
                cwd=root,
                check=True,
            )

            issues = audit_source_closure(root)

        self.assertEqual([], issues)

    def test_fully_indexed_source_graph_passes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.initialize(root)
            source = root / "libs/example.cpp"
            source.parent.mkdir(parents=True)
            source.write_text("int example() { return 1; }\n", encoding="utf-8")
            subprocess.run(["git", "add", "libs/example.cpp"], cwd=root, check=True)

            issues = audit_source_closure(root)

        self.assertEqual([], issues)


if __name__ == "__main__":
    unittest.main()
