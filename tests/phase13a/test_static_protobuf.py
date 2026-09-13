from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.phase13a.static_protobuf import prepare_static_protobuf_sdk
from tools.neural_runtime.build_static_protobuf import (
    ABSEIL_SHA256,
    ABSEIL_VERSION,
    PROTOBUF_SHA256,
    PROTOBUF_VERSION,
    PROTOC_ASSETS,
    StaticProtobufSdk,
    parse_std_options,
    prepare_static_protobuf,
    verify_static_install,
)


ROOT = Path(__file__).resolve().parents[2]
PINNED_ABSEIL_PINS = {"STRING_VIEW": "1", "ORDERING": "0"}


class StaticProtobufTests(unittest.TestCase):
    def prefix(self, *, shared: str | None = None, pinned: dict[str, str] | None = None,
               archive: str | None = "libprotobuf.a", cmake_package: bool = True) -> Path:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        prefix = Path(temporary.name)
        (prefix / "lib").mkdir(parents=True)
        (prefix / "include/absl/base").mkdir(parents=True)
        pins = PINNED_ABSEIL_PINS if pinned is None else pinned
        header = "".join(
            f"#define ABSL_OPTION_USE_STD_{feature} {value}\n" for feature, value in pins.items()
        )
        (prefix / "include/absl/base/options.h").write_text(header, encoding="utf-8")
        if archive is not None:
            (prefix / "lib" / archive).write_bytes(b"!<arch>\n")
        if cmake_package:
            (prefix / "lib/cmake/protobuf").mkdir(parents=True)
            (prefix / "lib/cmake/protobuf/protobuf-config.cmake").write_text("# package\n", encoding="utf-8")
            (prefix / "lib/cmake/absl").mkdir(parents=True)
        if shared is not None:
            (prefix / "lib" / shared).write_bytes(b"\xcf\xfa\xed\xfe\n")
        return prefix

    def test_pins_are_exact_and_digested(self) -> None:
        self.assertEqual("33.4", PROTOBUF_VERSION)
        self.assertEqual("20250512.1", ABSEIL_VERSION)
        self.assertEqual(
            "bc670a4e34992c175137ddda24e76562bb928f849d712a0e3c2fb2e19249bea1",
            PROTOBUF_SHA256,
        )
        self.assertEqual(
            "9b7a064305e9fd94d124ffa6cc358592eb42b5da588fb4e07d09254aa40086db",
            ABSEIL_SHA256,
        )

    def test_protoc_asset_pins_carry_a_digest(self) -> None:
        self.assertTrue(PROTOC_ASSETS)
        for host, (name, url, digest) in PROTOC_ASSETS.items():
            self.assertEqual(2, len(host))
            self.assertTrue(name.startswith("protoc-"))
            self.assertIn("https://", url)
            self.assertRegex(digest, r"^[0-9a-f]{64}$")

    def test_linkage_probe_source_is_shipped(self) -> None:
        probe = ROOT / "tools/neural_runtime/static_protobuf_probe.cpp"
        self.assertTrue(probe.is_file())
        self.assertIn("FileDescriptorProto", probe.read_text(encoding="utf-8"))

    def test_std_options_parser_reads_only_pin_definitions(self) -> None:
        header = (
            "// ABSL_OPTION_USE_STD_STRING_VIEW 2\n"
            "#define ABSL_OPTION_USE_STD_STRING_VIEW 1\n"
            "#define ABSL_OPTION_USE_STD_ORDERING 0\n"
            "#define ABSL_OPTION_INLINE_NAMESPACE_NAME lts_20250512\n"
        )
        self.assertEqual(
            {"STRING_VIEW": "1", "ORDERING": "0"}, parse_std_options(header)
        )

    def test_static_install_accepts_a_consistent_prefix(self) -> None:
        verify_static_install(self.prefix())

    def test_static_install_refuses_a_shared_library(self) -> None:
        with self.assertRaises(SystemExit) as raised:
            verify_static_install(self.prefix(shared="libprotobuf.33.4.0.dylib"))
        self.assertIn("shared libraries", str(raised.exception))

    def test_static_install_refuses_an_abi_pin_mismatch(self) -> None:
        # The defect this guards: Abseil archives compiled against std::string_view
        # with a header pinned to Abseil's own type make every consumer's link fail
        # with unresolved absl symbols that the archive demonstrably defines.
        with self.assertRaises(SystemExit) as raised:
            verify_static_install(
                self.prefix(pinned={"STRING_VIEW": "0", "ORDERING": "0"})
            )
        self.assertIn("compiled ABI and the installed header disagree", str(raised.exception))

    def test_static_install_refuses_a_missing_archive(self) -> None:
        with self.assertRaises(SystemExit) as raised:
            verify_static_install(self.prefix(archive=None))
        self.assertIn("no Protobuf archive", str(raised.exception))

    def test_static_install_refuses_a_missing_cmake_package(self) -> None:
        with self.assertRaises(SystemExit) as raised:
            verify_static_install(self.prefix(cmake_package=False))
        self.assertIn("no CMake package", str(raised.exception))

    def test_probe_only_mode_refuses_a_missing_prefix(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            missing = Path(temporary) / "static-protobuf-install"
            with self.assertRaises(SystemExit) as raised:
                prepare_static_protobuf(
                    missing, build_dir=Path(temporary) / "build", build=False
                )
            self.assertIn("does not exist", str(raised.exception))

    def test_probe_only_mode_refuses_without_a_probe_build_directory(self) -> None:
        with self.assertRaises(SystemExit) as raised:
            prepare_static_protobuf(self.prefix(), build=False)
        self.assertIn("probe-build-dir", str(raised.exception))

    def test_building_requires_a_source_directory(self) -> None:
        with self.assertRaises(SystemExit) as raised:
            prepare_static_protobuf(
                self.prefix(), build_dir=Path(tempfile.gettempdir()) / "seam-build", build=True
            )
        self.assertIn("source-dir", str(raised.exception))

    def test_distribution_entry_point_places_sources_and_prefix(self) -> None:
        captured: dict[str, object] = {}

        def fake(prefix, *, source_dir=None, build_dir=None, jobs=8, build=True):
            captured.update(prefix=prefix, source_dir=source_dir, build_dir=build_dir, jobs=jobs, build=build)
            return StaticProtobufSdk(prefix=prefix, protoc=Path("protoc"), probe=Path("probe"))

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            with patch("tools.phase13a.static_protobuf.prepare_static_protobuf", fake):
                sdk = prepare_static_protobuf_sdk(root / "dependencies", root / "build", jobs=3)
            self.assertEqual(root / "dependencies" / "protobuf", Path(captured["source_dir"]))
            self.assertEqual(root / "build" / "static-protobuf-install", Path(captured["prefix"]))
            self.assertEqual(root / "build" / "static-protobuf-build", Path(captured["build_dir"]))
            self.assertEqual(3, captured["jobs"])
            self.assertTrue(captured["build"])
            self.assertEqual(Path("protoc"), sdk.protoc)


if __name__ == "__main__":
    unittest.main()
