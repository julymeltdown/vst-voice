from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.phase13a.neural_helper_staging import (
    helper_file_name,
    image_identity,
    stage_neural_helper,
)
from tools.phase13a.neural_package import neural_package_inventory
from tools.phase13a.payload_paths import PayloadAssemblyError
from tools.phase13a.payload_surfaces import PayloadPlatform, surface_matrix


ROOT = Path(__file__).resolve().parents[2]
ARM64 = 0x0100000C
X86_64 = 0x01000007
THIN_MAGIC = (0xFEEDFACF).to_bytes(4, "little")


def macho(cpu: int) -> bytes:
    """Build a header-only thin Mach-O image declaring one machine."""
    return THIN_MAGIC + cpu.to_bytes(4, "little") + bytes(0x40)


def universal(cpus: tuple[int, ...]) -> bytes:
    """Build a header-only universal Mach-O image declaring several machines."""
    header = b"\xca\xfe\xba\xbe" + len(cpus).to_bytes(4, "big")
    for index, cpu in enumerate(cpus):
        header += cpu.to_bytes(4, "big") + bytes(4)
        header += (0x1000 * (index + 1)).to_bytes(4, "big")
        header += (0x1000).to_bytes(4, "big")
        header += (12).to_bytes(4, "big")
    return header


def pe(machine: int = 0x8664) -> bytes:
    """Build a header-only PE image declaring one machine."""
    header = bytearray(0x40)
    header[0:2] = b"MZ"
    header[0x3C:0x40] = (0x80).to_bytes(4, "little")
    header.extend(bytes(0x80 - len(header)))
    header.extend(b"PE\0\0" + machine.to_bytes(2, "little") + bytes(0x20))
    return bytes(header)


class NeuralHelperStagingTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def payload(self, platform: PayloadPlatform) -> Path:
        root = self.root / f"payload-{platform}"
        for surface in surface_matrix(platform):
            if surface.identifier == "installer-verifier":
                continue
            module = root / surface.binary_relative_path
            module.parent.mkdir(parents=True, exist_ok=True)
            module.write_bytes(b"module-fixture")
        return root

    def image(self, name: str, content: bytes) -> Path:
        path = self.root / name
        path.write_bytes(content)
        return path

    def test_image_identity_reads_thin_universal_and_portable_images(self) -> None:
        self.assertEqual(("macho", ("arm64",)), image_identity(macho(ARM64)))
        self.assertEqual(("macho", ("x86_64",)), image_identity(macho(X86_64)))
        self.assertEqual(("macho", ("x86_64", "arm64")), image_identity(universal((X86_64, ARM64))))
        self.assertEqual(("pe", ("x86_64",)), image_identity(pe()))
        self.assertEqual(("pe", ("arm64",)), image_identity(pe(0xAA64)))
        with self.assertRaisesRegex(PayloadAssemblyError, "not a Mach-O or PE"):
            image_identity(b"#!/bin/sh\n" + bytes(0x40))
        with self.assertRaisesRegex(PayloadAssemblyError, "truncated"):
            image_identity(b"\xcf\xfa\xed\xfe\x0c")
        with self.assertRaisesRegex(PayloadAssemblyError, "invalid slice count"):
            image_identity(b"\xca\xfe\xba\xbe" + (99).to_bytes(4, "big"))

    def test_macos_surfaces_receive_one_sealed_helper_package(self) -> None:
        payload = self.payload(PayloadPlatform.MACOS_ARM64)
        worker = self.image("worker", macho(ARM64))
        runtime = self.image("libonnxruntime.dylib", macho(ARM64))
        records = stage_neural_helper(
            payload, PayloadPlatform.MACOS_ARM64, worker, (runtime,), "build-fixture"
        )
        self.assertEqual(
            [record["surface"] for record in records],
            ["standalone", "clap", "vst3", "auv2"],
        )
        inventory = neural_package_inventory(payload, PayloadPlatform.MACOS_ARM64, "build-fixture")
        self.assertEqual(len(inventory), 4)
        for row in inventory:
            self.assertEqual(row["status"], "VERIFIED_FILES")
        self.assertIn(
            "AU/ProjectSEAMEditor.component/Contents/Resources/neural-helper-package.json",
            [row["path"] for row in inventory],
        )
        staged = json.loads(
            (payload / "Standalone/Project SEAM.app/Contents/Resources/neural-helper-package.json").read_bytes()
        )
        self.assertEqual(staged["helper"]["path"], "Contents/Resources/neural-helper")
        self.assertEqual(staged["module"]["path"], "Contents/MacOS/Project SEAM")
        self.assertEqual(
            [entry["path"] for entry in staged["dependencies"]],
            ["Contents/Resources/libonnxruntime.dylib"],
        )

    def test_windows_surfaces_receive_the_helper_beside_their_loose_binaries(self) -> None:
        payload = self.payload(PayloadPlatform.WINDOWS_X64)
        worker = self.image("worker.exe", pe())
        records = stage_neural_helper(
            payload, PayloadPlatform.WINDOWS_X64, worker, (), "build-fixture"
        )
        self.assertEqual(
            [record["surface"] for record in records], ["standalone", "clap", "vst3"]
        )
        self.assertTrue((payload / "Standalone/Resources/neural-helper.exe").is_file())
        self.assertTrue(
            (payload / "CLAP/ProjectSEAMEditor.resources/neural-helper.exe").is_file()
        )
        staged = json.loads(
            (payload / "Standalone/Resources/neural-helper-package.json").read_bytes()
        )
        self.assertEqual(staged["module"]["path"], "seam_editor_native.exe")
        for row in neural_package_inventory(payload, PayloadPlatform.WINDOWS_X64, "build-fixture"):
            self.assertEqual(row["status"], "VERIFIED_FILES")
        self.assertEqual("neural-helper.exe", helper_file_name(PayloadPlatform.WINDOWS_X64))

    def test_images_from_the_other_platform_are_refused(self) -> None:
        payload = self.payload(PayloadPlatform.WINDOWS_X64)
        with self.assertRaisesRegex(PayloadAssemblyError, "requires a pe image"):
            stage_neural_helper(payload, PayloadPlatform.WINDOWS_X64, self.image("worker", macho(ARM64)), (), "build-fixture")
        macos = self.payload(PayloadPlatform.MACOS_ARM64)
        with self.assertRaisesRegex(PayloadAssemblyError, "requires a macho image"):
            stage_neural_helper(macos, PayloadPlatform.MACOS_ARM64, self.image("worker.exe", pe()), (), "build-fixture")
        with self.assertRaisesRegex(PayloadAssemblyError, "declaring x86_64"):
            stage_neural_helper(
                macos, PayloadPlatform.MACOS_ARM64, self.image("intel", macho(X86_64)), (), "build-fixture"
            )
        with self.assertRaisesRegex(PayloadAssemblyError, "not a Mach-O or PE"):
            stage_neural_helper(
                macos, PayloadPlatform.MACOS_ARM64, self.image("script", b"#!/bin/sh\n"), (), "build-fixture"
            )

    def test_universal_worker_is_accepted_only_when_it_declares_the_machine(self) -> None:
        payload = self.payload(PayloadPlatform.MACOS_ARM64)
        worker = self.image("universal", universal((X86_64, ARM64)))
        records = stage_neural_helper(payload, PayloadPlatform.MACOS_ARM64, worker, (), "build-fixture")
        self.assertEqual(len(records), 4)
        intel = self.image("intel-universal", universal((X86_64,)))
        with self.assertRaisesRegex(PayloadAssemblyError, "requires a macho image declaring arm64"):
            stage_neural_helper(
                self.payload(PayloadPlatform.MACOS_ARM64), PayloadPlatform.MACOS_ARM64, intel, (), "build-fixture"
            )

    def test_staging_refuses_name_conflicts_and_replaced_bytes(self) -> None:
        payload = self.payload(PayloadPlatform.MACOS_ARM64)
        worker = self.image("worker", macho(ARM64))
        collision = self.image("neural-helper", macho(ARM64))
        with self.assertRaisesRegex(PayloadAssemblyError, "would collide"):
            stage_neural_helper(
                payload, PayloadPlatform.MACOS_ARM64, worker, (collision,), "build-fixture"
            )
        stage_neural_helper(payload, PayloadPlatform.MACOS_ARM64, worker, (), "build-fixture")
        staged = payload / "Standalone/Project SEAM.app/Contents/Resources/neural-helper"
        self.assertEqual(staged.read_bytes(), worker.read_bytes())
        stage_neural_helper(payload, PayloadPlatform.MACOS_ARM64, worker, (), "build-fixture")
        self.assertEqual(staged.read_bytes(), worker.read_bytes())
        staged.write_bytes(macho(ARM64) + b"tampered")
        with self.assertRaisesRegex(PayloadAssemblyError, "different bytes"):
            stage_neural_helper(payload, PayloadPlatform.MACOS_ARM64, worker, (), "build-fixture")

    def test_staging_validates_every_surface_before_writing_any_byte(self) -> None:
        payload = self.payload(PayloadPlatform.MACOS_ARM64)
        (payload / "VST3/ProjectSEAMEditor.vst3/Contents/MacOS/ProjectSEAMEditor").unlink()
        worker = self.image("worker", macho(ARM64))
        with self.assertRaises(PayloadAssemblyError):
            stage_neural_helper(payload, PayloadPlatform.MACOS_ARM64, worker, (), "build-fixture")
        self.assertFalse(
            (payload / "Standalone/Project SEAM.app/Contents/Resources/neural-helper").exists()
        )
        self.assertFalse(
            (payload / "Standalone/Project SEAM.app/Contents/Resources/neural-helper-package.json").exists()
        )

    def test_cli_stages_one_surface_and_reports_refusals(self) -> None:
        payload = self.payload(PayloadPlatform.WINDOWS_X64)
        worker = self.image("worker.exe", pe())
        accepted = subprocess.run(
            [
                sys.executable, "-m", "tools.phase13a.neural_helper_staging",
                "--payload", str(payload), "--platform", "windows-x64",
                "--worker", str(worker), "--build-id", "build-fixture",
                "--surface", "standalone", "--surface", "vst3",
            ],
            cwd=ROOT, capture_output=True, text=True, timeout=60,
        )
        self.assertEqual(0, accepted.returncode, accepted.stderr)
        self.assertEqual(
            ["standalone", "vst3"],
            [row["surface"] for row in json.loads(accepted.stdout)["surfaces"]],
        )
        self.assertTrue((payload / "Standalone/Resources/neural-helper.exe").is_file())
        self.assertFalse((payload / "CLAP/ProjectSEAMEditor.resources").exists())
        refused = subprocess.run(
            [
                sys.executable, "-m", "tools.phase13a.neural_helper_staging",
                "--payload", str(payload), "--platform", "windows-x64",
                "--worker", str(worker), "--build-id", "build-fixture",
                "--surface", "clap", "--surface", "garbage",
            ],
            cwd=ROOT, capture_output=True, text=True, timeout=60,
        )
        self.assertEqual(3, refused.returncode)
        self.assertIn("does not declare every requested surface", refused.stderr)


if __name__ == "__main__":
    unittest.main()
