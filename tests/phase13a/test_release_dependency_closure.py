from __future__ import annotations

import tempfile
import unittest
from dataclasses import dataclass
from pathlib import Path

from tools.phase13a.dependency_closure import verify_dependency_closure
from tools.phase13a.payload_surfaces import PayloadPlatform, surface_matrix


@dataclass(frozen=True, slots=True)
class FakeInspector:
    payload: Path
    dependencies: dict[str, tuple[str, ...]]

    def imports(self, binary: Path) -> tuple[str, ...]:
        return self.dependencies[binary.relative_to(self.payload.resolve()).as_posix()]


class ReleaseDependencyClosureTests(unittest.TestCase):
    def __init__(self, methodName: str = "runTest") -> None:
        super().__init__(methodName)
        self.temporary = tempfile.TemporaryDirectory()
        self.payload = Path(self.temporary.name)

    def setUp(self) -> None:
        (self.payload / "SBOM.spdx.json").write_text("{}", encoding="utf-8")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def binary(self, relative: str, platform: PayloadPlatform) -> Path:
        path = self.payload / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        if platform is PayloadPlatform.MACOS_ARM64:
            header = bytes.fromhex("cffaedfe0c000001")
        else:
            value = bytearray(70)
            value[:2] = b"MZ"
            value[60:64] = (64).to_bytes(4, "little")
            value[64:68] = b"PE\x00\x00"
            value[68:70] = b"\x64\x86"
            header = bytes(value)
        path.write_bytes(header + b"fixture")
        return path

    def release_binaries(self, platform: PayloadPlatform) -> dict[str, tuple[str, ...]]:
        system = (
            ("/usr/lib/libc++.1.dylib",)
            if platform is PayloadPlatform.MACOS_ARM64
            else ("KERNEL32.dll",)
        )
        dependencies: dict[str, tuple[str, ...]] = {}
        for surface in surface_matrix(platform):
            self.binary(surface.binary_relative_path, platform)
            dependencies[surface.binary_relative_path] = system
        return dependencies

    def test_macos_system_only_binary_passes_static_closure(self) -> None:
        dependencies = self.release_binaries(PayloadPlatform.MACOS_ARM64)
        dependencies["Tools/seam_installer_verifier"] = (
            "/usr/lib/libc++.1.dylib",
            "/System/Library/Frameworks/Foundation.framework/Foundation",
        )
        inspector = FakeInspector(self.payload, dependencies)

        result = verify_dependency_closure(
            self.payload,
            PayloadPlatform.MACOS_ARM64,
            inspector,
            "a" * 40,
        )

        self.assertEqual("PASS", result.status)
        self.assertEqual(
            len(surface_matrix(PayloadPlatform.MACOS_ARM64)), len(result.binaries)
        )
        self.assertEqual((), result.errors)

    def test_macos_dynamic_libcrypto_is_rejected(self) -> None:
        dependencies = self.release_binaries(PayloadPlatform.MACOS_ARM64)
        dependencies["Standalone/Project SEAM.app/Contents/MacOS/Project SEAM"] = (
            "/opt/homebrew/opt/openssl@3/lib/libcrypto.3.dylib",
        )
        inspector = FakeInspector(self.payload, dependencies)

        result = verify_dependency_closure(
            self.payload,
            PayloadPlatform.MACOS_ARM64,
            inspector,
            "a" * 40,
        )

        self.assertEqual("BLOCKED", result.status)
        self.assertTrue(any("libcrypto" in error for error in result.errors))

    def test_windows_system_only_binary_passes_static_closure(self) -> None:
        dependencies = self.release_binaries(PayloadPlatform.WINDOWS_X64)
        dependencies["Tools/seam_installer_verifier.exe"] = (
            "KERNEL32.dll",
            "USER32.dll",
        )
        inspector = FakeInspector(self.payload, dependencies)

        result = verify_dependency_closure(
            self.payload,
            PayloadPlatform.WINDOWS_X64,
            inspector,
            "a" * 40,
        )

        self.assertEqual("PASS", result.status)
        self.assertEqual((), result.errors)

    def test_windows_audio_and_accessibility_system_dlls_are_allowed(self) -> None:
        dependencies = self.release_binaries(PayloadPlatform.WINDOWS_X64)
        dependencies["Standalone/seam_editor_native.exe"] = (
            "AVRT.dll",
            "OLEACC.dll",
            "UIAutomationCore.dll",
        )

        result = verify_dependency_closure(
            self.payload,
            PayloadPlatform.WINDOWS_X64,
            FakeInspector(self.payload, dependencies),
            "a" * 40,
        )

        self.assertEqual("PASS", result.status)
        self.assertEqual((), result.errors)

    def test_windows_dynamic_openssl_is_rejected(self) -> None:
        dependencies = self.release_binaries(PayloadPlatform.WINDOWS_X64)
        dependencies["Standalone/seam_editor_native.exe"] = ("libcrypto-3-x64.dll",)
        inspector = FakeInspector(self.payload, dependencies)

        result = verify_dependency_closure(
            self.payload,
            PayloadPlatform.WINDOWS_X64,
            inspector,
            "a" * 40,
        )

        self.assertEqual("BLOCKED", result.status)
        self.assertTrue(any("libcrypto" in error for error in result.errors))

    def test_windows_unaudited_api_set_prefixes_are_rejected(self) -> None:
        for dependency in (
            "api-ms-win-vendor-l1-1-0.dll",
            "ext-ms-win-missing-l1-1-0.dll",
        ):
            with self.subTest(dependency=dependency):
                dependencies = self.release_binaries(PayloadPlatform.WINDOWS_X64)
                dependencies["Standalone/seam_editor_native.exe"] = (dependency,)

                result = verify_dependency_closure(
                    self.payload,
                    PayloadPlatform.WINDOWS_X64,
                    FakeInspector(self.payload, dependencies),
                    "a" * 40,
                )

                self.assertEqual("BLOCKED", result.status)
                self.assertTrue(any(dependency in error for error in result.errors))

    def test_windows_vc_runtime_must_not_remain_a_dynamic_dependency(self) -> None:
        dependencies = self.release_binaries(PayloadPlatform.WINDOWS_X64)
        dependencies["Standalone/seam_editor_native.exe"] = ("MSVCP140.dll",)
        inspector = FakeInspector(self.payload, dependencies)

        result = verify_dependency_closure(
            self.payload,
            PayloadPlatform.WINDOWS_X64,
            inspector,
            "a" * 40,
        )

        self.assertEqual("BLOCKED", result.status)
        self.assertTrue(any("MSVCP140.dll" in error for error in result.errors))

    def test_windows_packaged_dependency_is_rejected_even_when_declared(self) -> None:
        dependencies = self.release_binaries(PayloadPlatform.WINDOWS_X64)
        dependencies["Standalone/seam_editor_native.exe"] = ("vendor.dll",)
        self.binary("Vendor/vendor.dll", PayloadPlatform.WINDOWS_X64)
        dependencies["Vendor/vendor.dll"] = ("KERNEL32.dll",)
        inspector = FakeInspector(self.payload, dependencies)

        blocked = verify_dependency_closure(
            self.payload,
            PayloadPlatform.WINDOWS_X64,
            inspector,
            "a" * 40,
        )
        (self.payload / "SBOM.spdx.json").write_text(
            '{"name":"vendor.dll"}', encoding="utf-8"
        )
        still_blocked = verify_dependency_closure(
            self.payload,
            PayloadPlatform.WINDOWS_X64,
            inspector,
            "a" * 40,
        )

        self.assertEqual("BLOCKED", blocked.status)
        self.assertEqual("BLOCKED", still_blocked.status)
        self.assertTrue(
            any("non-system Windows dependency" in error for error in blocked.errors)
        )

    def test_corrupt_claimed_binary_blocks_an_otherwise_valid_closure(self) -> None:
        dependencies = self.release_binaries(PayloadPlatform.WINDOWS_X64)
        standalone = self.payload / "Standalone/seam_editor_native.exe"
        standalone.write_text("not a PE file", encoding="utf-8")

        result = verify_dependency_closure(
            self.payload,
            PayloadPlatform.WINDOWS_X64,
            FakeInspector(self.payload, dependencies),
            "a" * 40,
        )

        self.assertEqual("BLOCKED", result.status)
        self.assertTrue(any("standalone" in error for error in result.errors))


if __name__ == "__main__":
    unittest.main()
