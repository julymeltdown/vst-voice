from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.phase13a.static_openssl import (
    build_static_openssl, native_windows_perl, openssl_build_plan,
    prepare_static_openssl,
)


ROOT = Path(__file__).resolve().parents[2]
OPENSSL_COMMIT = "8cf17aaeb4599f8af87fefd810b5b5fee90fe69e"


class StaticOpenSslTests(unittest.TestCase):
    def test_native_perl_skips_git_bash_perl_even_when_first_on_path(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            candidates = [root / name for name in ("git usr", "strawberry")]
            for path in candidates:
                path.mkdir()
                (path / "perl.exe").touch()
            with patch("tools.phase13a.static_openssl.os.get_exec_path", return_value=list(map(str, candidates))), patch(
                "tools.phase13a.static_openssl.subprocess.run",
                side_effect=[subprocess.CompletedProcess([], 0, "msys"),
                             subprocess.CompletedProcess([], 0, "MSWin32")],
            ) as run:
                self.assertEqual(str((candidates[1] / "perl.exe").resolve()), native_windows_perl())
                self.assertEqual(2, run.call_count)
                self.assertEqual(10, run.call_args.kwargs["timeout"])

    def test_native_perl_rejects_missing_unix_and_broken_interpreters(self) -> None:
        for outcome in (subprocess.CompletedProcess([], 0, "cygwin"),
                        subprocess.TimeoutExpired("perl", 10), OSError("unavailable")):
            with self.subTest(outcome=outcome), patch(
                "tools.phase13a.static_openssl.os.get_exec_path", return_value=["/candidate"]
            ), patch("tools.phase13a.static_openssl.Path.is_file", return_value=True), patch(
                "tools.phase13a.static_openssl.subprocess.run",
                **({"side_effect": outcome} if isinstance(outcome, Exception) else {"return_value": outcome}),
            ):
                with self.assertRaisesRegex(RuntimeError, "native Windows Perl"):
                    native_windows_perl()
        with patch("tools.phase13a.static_openssl.os.get_exec_path", return_value=[]):
            with self.assertRaisesRegex(RuntimeError, "native Windows Perl"):
                native_windows_perl()

    def test_windows_preparation_binds_verified_perl_into_build_and_receipt(self) -> None:
        with patch("tools.phase13a.static_openssl.native_windows_perl", return_value="C:/Strawberry/perl/bin/perl.exe"), patch(
            "tools.phase13a.static_openssl.build_static_openssl"
        ) as build:
            prepare_static_openssl("Windows", "AMD64", Path("C:/source"), Path("C:/build"), OPENSSL_COMMIT)
            plan = build.call_args.args[0]
            self.assertEqual("C:/Strawberry/perl/bin/perl.exe", plan.configure[0])
            self.assertIn("VC-WIN64A", plan.configure)

    def test_missing_native_perl_fails_before_build_or_cache_mutation(self) -> None:
        with patch("tools.phase13a.static_openssl.native_windows_perl", side_effect=RuntimeError("native Windows Perl unavailable")), patch(
            "tools.phase13a.static_openssl.build_static_openssl"
        ) as build:
            with self.assertRaisesRegex(RuntimeError, "native Windows Perl"):
                prepare_static_openssl("Windows", "AMD64", Path("C:/source"), Path("C:/build"), OPENSSL_COMMIT)
            build.assert_not_called()

    def test_non_windows_preparation_does_not_probe_windows_perl(self) -> None:
        with patch("tools.phase13a.static_openssl.native_windows_perl") as probe, patch(
            "tools.phase13a.static_openssl.build_static_openssl"
        ) as build:
            prepare_static_openssl("Linux", "x86_64", Path("/source"), Path("/build"), OPENSSL_COMMIT)
            probe.assert_not_called()
            self.assertEqual("perl", build.call_args.args[0].configure[0])

    def test_dependency_lock_pins_openssl_357(self) -> None:
        lock = json.loads(
            (ROOT / "phase13a/dependency-lock.json").read_text(encoding="utf-8")
        )
        openssl = next(
            dependency
            for dependency in lock["dependencies"]
            if dependency["name"] == "openssl"
        )

        self.assertEqual("openssl-3.5.7", openssl["tag"])
        self.assertEqual(OPENSSL_COMMIT, openssl["commit"])
        self.assertEqual("Apache-2.0", openssl["license"])

    def test_macos_build_plan_selects_static_pic_crypto(self) -> None:
        plan = openssl_build_plan(
            "Darwin", "arm64", Path("/source"), Path("/prefix"), OPENSSL_COMMIT
        )

        self.assertIn("darwin64-arm64-cc", plan.configure)
        self.assertIn("no-shared", plan.configure)
        self.assertIn("no-module", plan.configure)
        self.assertEqual(Path("/prefix/lib/libcrypto.a"), plan.archive)

    def test_windows_build_plan_selects_static_x64_crypto(self) -> None:
        plan = openssl_build_plan(
            "Windows", "AMD64", Path("C:/source"), Path("C:/prefix"), OPENSSL_COMMIT
        )

        self.assertIn("VC-WIN64A", plan.configure)
        self.assertIn("/MT", plan.configure)
        self.assertEqual(("nmake", "build_sw"), plan.build)
        self.assertEqual(Path("C:/prefix/lib/libcrypto.lib"), plan.archive)

    def test_existing_archive_requires_a_matching_build_receipt(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plan = openssl_build_plan(
                "Darwin",
                "arm64",
                root / "source",
                root / "prefix",
                OPENSSL_COMMIT,
            )
            plan.archive.parent.mkdir(parents=True)
            plan.archive.write_bytes(b"stale")
            stale_header = plan.receipt.parent / "include/openssl/removed.h"
            stale_header.parent.mkdir(parents=True)
            stale_header.write_text("stale", encoding="utf-8")

            def complete_install(command, **_kwargs):
                if tuple(command) == plan.install:
                    plan.archive.parent.mkdir(parents=True, exist_ok=True)
                    plan.archive.write_bytes(b"rebuilt")

            with patch("tools.phase13a.static_openssl.subprocess.run", side_effect=complete_install) as run:
                build_static_openssl(plan, root / "build")
                self.assertEqual(3, run.call_count)
                self.assertFalse(stale_header.exists())
                build_static_openssl(plan, root / "build")
                self.assertEqual(3, run.call_count)

    def test_release_cmake_fails_closed_without_static_openssl(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        builder = (ROOT / "scripts/build_phase13a_formats.py").read_text(
            encoding="utf-8"
        )

        self.assertIn("SEAM_REQUIRE_STATIC_OPENSSL", cmake)
        self.assertIn("OPENSSL_USE_STATIC_LIBS", cmake)
        self.assertIn("-DSEAM_REQUIRE_STATIC_OPENSSL=ON", builder)
        self.assertIn("OPENSSL_ROOT_DIR", builder)
        self.assertIn("CMAKE_MSVC_RUNTIME_LIBRARY", builder)


if __name__ == "__main__":
    unittest.main()
