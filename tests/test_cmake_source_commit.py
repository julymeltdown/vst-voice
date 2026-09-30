"""SEAM_SOURCE_COMMIT follows the checkout's HEAD unless a caller supplies one.

The local default used to be written into the CMake cache at the first configure and kept there, so a
build directory went on stamping export receipts with the commit it was first configured at long after
HEAD had moved. These tests run the real identity block from CMakeLists.txt in a throw-away Git project.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BEGIN = "# seam-source-commit:begin"
END = "# seam-source-commit:end"
ZERO = "0" * 40
CMAKE = os.environ.get("SEAM_TEST_CMAKE") or shutil.which("cmake") or "cmake"
GENERATOR = ["-G", "Ninja"] if shutil.which("ninja") else []
GIT_ENV = {
    **os.environ,
    "GIT_CONFIG_GLOBAL": os.devnull,
    "GIT_CONFIG_SYSTEM": os.devnull,
    "GIT_AUTHOR_NAME": "seam test",
    "GIT_AUTHOR_EMAIL": "seam@example.invalid",
    "GIT_COMMITTER_NAME": "seam test",
    "GIT_COMMITTER_EMAIL": "seam@example.invalid",
}


def identity_block() -> str:
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    start = text.index(BEGIN)
    end = text.index(END, start) + len(END)
    return text[start:end]


class SourceCommitFollowsHeadTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = Path(tempfile.mkdtemp(prefix="seam-source-commit-"))
        self.addCleanup(shutil.rmtree, self.temp, ignore_errors=True)
        self.source = self.temp / "source"
        self.build = self.temp / "build"
        self.source.mkdir()
        (self.source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.25)\n"
            "project(SourceCommitProbe LANGUAGES NONE)\n"
            + identity_block()
            + "\nadd_custom_target(probe ALL)\n",
            encoding="utf-8",
        )

    def git(self, *args: str) -> str:
        done = subprocess.run(["git", *args], cwd=self.source, env=GIT_ENV,
                              check=True, capture_output=True, text=True)
        return done.stdout.strip()

    def repository(self) -> None:
        self.git("init", "-q")

    def commit(self, message: str) -> str:
        self.git("commit", "-q", "--allow-empty", "-m", message)
        time.sleep(0.05)  # The build system compares modification times.
        return self.git("rev-parse", "HEAD")

    def cmake(self, *args: str) -> str:
        done = subprocess.run([CMAKE, *args], cwd=self.temp, env=GIT_ENV,
                              check=True, capture_output=True, text=True)
        return done.stdout + done.stderr

    def configure(self, *definitions: str) -> None:
        self.cmake("-S", str(self.source), "-B", str(self.build), *GENERATOR, *definitions)

    def build_all(self) -> None:
        self.cmake("--build", str(self.build))

    def cached(self) -> str:
        for line in (self.build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
            if line.startswith("SEAM_SOURCE_COMMIT:"):
                return line.split("=", 1)[1]
        raise AssertionError("SEAM_SOURCE_COMMIT is not in the CMake cache")

    def test_a_reconfigure_follows_head(self) -> None:
        self.repository()
        first = self.commit("first")
        self.configure()
        self.assertEqual(self.cached(), first)
        second = self.commit("second")
        self.assertNotEqual(first, second)
        self.configure()
        self.assertEqual(self.cached(), second)

    def test_a_build_after_a_commit_reconfigures_and_follows_head(self) -> None:
        self.repository()
        self.commit("first")
        self.configure()
        second = self.commit("second")
        self.build_all()
        self.assertEqual(self.cached(), second)

    def test_a_checkout_of_an_earlier_commit_is_followed_too(self) -> None:
        self.repository()
        first = self.commit("first")
        self.commit("second")
        self.configure()
        self.git("checkout", "-q", first)
        time.sleep(0.05)
        self.build_all()
        self.assertEqual(self.cached(), first)

    def test_an_explicit_commit_is_authoritative(self) -> None:
        self.repository()
        self.commit("first")
        explicit = "c" * 40
        self.configure(f"-DSEAM_SOURCE_COMMIT={explicit}")
        self.assertEqual(self.cached(), explicit)
        self.commit("second")
        self.configure()
        self.assertEqual(self.cached(), explicit)
        self.commit("third")
        self.build_all()
        self.assertEqual(self.cached(), explicit)

    def test_an_explicit_commit_replaces_an_earlier_default(self) -> None:
        self.repository()
        self.commit("first")
        self.configure()
        explicit = "d" * 40
        self.configure(f"-DSEAM_SOURCE_COMMIT={explicit}")
        self.assertEqual(self.cached(), explicit)
        self.commit("second")
        self.configure()
        self.assertEqual(self.cached(), explicit)

    def test_without_git_history_the_zero_base_is_kept(self) -> None:
        self.configure()
        self.assertEqual(self.cached(), ZERO)

    def test_the_release_gate_still_finds_the_literal_line_it_reads(self) -> None:
        # tools/external_beta/release_gate.py reads the source identity from this exact line shape.
        text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertRegex(text, r'set\(SEAM_SOURCE_COMMIT "[$][{]SEAM_LOCAL_SOURCE_BASE[}]" CACHE STRING')


if __name__ == "__main__":
    unittest.main()
