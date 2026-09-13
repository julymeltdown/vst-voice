from __future__ import annotations

import json
import os
import shutil
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
from tools.phase13a.macho_linkage import (
    read_linkage,
    resolves_from_staged_directory,
    staged_name_for,
)
from tools.phase13a.pe_linkage import read_imports
from tools.phase13a.runtime_closure import derive_runtime_closure


ROOT = Path(__file__).resolve().parents[2]
ARM64 = 0x0100000C
X86_64 = 0x01000007
THIN_MAGIC = (0xFEEDFACF).to_bytes(4, "little")


def macho(cpu: int) -> bytes:
    """Build a header-only thin Mach-O image declaring one machine."""
    return THIN_MAGIC + cpu.to_bytes(4, "little") + bytes(0x40)


LC_ID_DYLIB = 0xD
LC_LOAD_DYLIB = 0xC
LC_RPATH = 0x1C | 0x80000000


def _dylib_command(command: int, name: str) -> bytes:
    payload = name.encode("utf-8") + b"\0"
    size = (8 + 16 + len(payload) + 7) & ~7
    raw = bytearray(size)
    raw[0:4] = command.to_bytes(4, "little")
    raw[4:8] = size.to_bytes(4, "little")
    raw[8:12] = (24).to_bytes(4, "little")
    raw[24 : 24 + len(payload)] = payload
    return bytes(raw)


def _rpath_command(path: str) -> bytes:
    payload = path.encode("utf-8") + b"\0"
    size = (8 + 4 + len(payload) + 7) & ~7
    raw = bytearray(size)
    raw[0:4] = LC_RPATH.to_bytes(4, "little")
    raw[4:8] = size.to_bytes(4, "little")
    raw[8:12] = (12).to_bytes(4, "little")
    raw[12 : 12 + len(payload)] = payload
    return bytes(raw)


def macho_image(
    *,
    cpu: int = ARM64,
    install_name: str | None = None,
    dependencies: tuple[str, ...] = (),
    rpaths: tuple[str, ...] = (),
) -> bytes:
    """Build a thin Mach-O image whose load commands declare real linkage."""
    commands = bytearray()
    count = 0
    if install_name is not None:
        commands += _dylib_command(LC_ID_DYLIB, install_name)
        count += 1
    for name in dependencies:
        commands += _dylib_command(LC_LOAD_DYLIB, name)
        count += 1
    for path in rpaths:
        commands += _rpath_command(path)
        count += 1
    header = bytearray(32)
    header[0:4] = THIN_MAGIC
    header[4:8] = cpu.to_bytes(4, "little")
    header[12:16] = (2).to_bytes(4, "little")
    header[16:20] = count.to_bytes(4, "little")
    header[20:24] = len(commands).to_bytes(4, "little")
    return bytes(header + commands)


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


def pe_with_imports(modules: tuple[str, ...], machine: int = 0x8664) -> bytes:
    """Build a minimal PE32+ image whose import directory names real modules.

    The section table is real, so RVA to file-offset translation is exercised the
    same way a shipped image exercises it; only the descriptor and name bytes are
    present, because that is all linkage analysis reads.
    """
    section_rva = 0x1000
    section_offset = 0x400
    optional_size = 0xF0
    descriptors = bytearray()
    names = bytearray()
    if modules:
        names_base = section_rva + 20 * (len(modules) + 1)
        for module in modules:
            descriptors.extend(bytes(4))
            descriptors.extend(bytes(4))
            descriptors.extend(bytes(4))
            descriptors.extend((names_base + len(names)).to_bytes(4, "little"))
            descriptors.extend(bytes(4))
            names.extend(module.encode("ascii") + b"\0")
        descriptors.extend(bytes(20))
    section_data = bytes(descriptors) + bytes(names)
    optional = bytearray(optional_size)
    optional[0:2] = (0x20B).to_bytes(2, "little")
    directory = 112 + 8  # OPTIONAL_PE32_PLUS_DATA_DIRECTORY_OFFSET + import index
    optional[directory : directory + 4] = (
        section_rva if modules else 0
    ).to_bytes(4, "little")
    optional[directory + 4 : directory + 8] = len(descriptors).to_bytes(4, "little")
    coff = bytearray(20)
    coff[0:2] = machine.to_bytes(2, "little")
    coff[2:4] = (1).to_bytes(2, "little")
    coff[16:18] = optional_size.to_bytes(2, "little")
    section = bytearray(40)
    section[0:8] = b".rdata\0\0"
    section[8:12] = len(section_data).to_bytes(4, "little")
    section[12:16] = section_rva.to_bytes(4, "little")
    section[16:20] = len(section_data).to_bytes(4, "little")
    section[20:24] = section_offset.to_bytes(4, "little")
    header = bytearray(0x40)
    header[0:2] = b"MZ"
    header[0x3C:0x40] = (0x80).to_bytes(4, "little")
    image = bytes(header) + bytes(0x80 - len(header))
    image += b"PE\0\0" + bytes(coff) + bytes(optional) + bytes(section)
    image += bytes(section_offset - len(image))
    return image + section_data


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

    def test_linkage_reads_the_load_commands_that_decide_its_closure(self) -> None:
        image = macho_image(
            install_name="@rpath/libmodel.1.dylib",
            dependencies=(
                "@rpath/libonnxruntime.1.dylib",
                "/usr/lib/libc++.1.dylib",
                "/opt/homebrew/opt/protobuf/lib/libprotobuf.dylib",
            ),
            rpaths=("@executable_path", "/Users/build/onnx/lib"),
        )
        linkage = read_linkage(image)
        self.assertEqual("@rpath/libmodel.1.dylib", linkage.install_name)
        self.assertEqual(
            (
                "@rpath/libonnxruntime.1.dylib",
                "/usr/lib/libc++.1.dylib",
                "/opt/homebrew/opt/protobuf/lib/libprotobuf.dylib",
            ),
            linkage.dependencies,
        )
        self.assertEqual(("@executable_path", "/Users/build/onnx/lib"), linkage.rpaths)
        # The host provides the system library, so only the other two are the
        # package's problem.
        self.assertEqual(
            (
                "@rpath/libonnxruntime.1.dylib",
                "/opt/homebrew/opt/protobuf/lib/libprotobuf.dylib",
            ),
            linkage.staged_dependencies(),
        )
        self.assertTrue(
            resolves_from_staged_directory("@rpath/libonnxruntime.1.dylib", linkage.rpaths)
        )
        self.assertFalse(
            resolves_from_staged_directory(
                "/opt/homebrew/opt/protobuf/lib/libprotobuf.dylib", linkage.rpaths
            )
        )
        self.assertEqual("libonnxruntime.1.dylib", staged_name_for("@rpath/libonnxruntime.1.dylib"))
        self.assertIsNone(staged_name_for("@rpath/"))

    def test_linkage_refuses_a_truncated_or_oversized_command_table(self) -> None:
        oversized = bytearray(macho_image(rpaths=("@executable_path",)))
        oversized[20:24] = (1 << 30).to_bytes(4, "little")
        with self.assertRaisesRegex(PayloadAssemblyError, "load-command budget"):
            read_linkage(bytes(oversized))
        short = bytearray(macho_image(rpaths=("@executable_path",)))
        short[20:24] = (4096).to_bytes(4, "little")
        with self.assertRaisesRegex(PayloadAssemblyError, "truncated"):
            read_linkage(bytes(short))
        with self.assertRaisesRegex(PayloadAssemblyError, "not a Mach-O"):
            read_linkage(b"not-an-image" + bytes(64))

    def test_runtime_closure_stages_only_references_that_resolve_beside_the_helper(self) -> None:
        library = self.image(
            "libonnxruntime.1.30.0.dylib",
            macho_image(
                install_name="@rpath/libonnxruntime.1.dylib",
                dependencies=("/usr/lib/libc++.1.dylib",),
            ),
        )
        published = self.root / "lib"
        published.mkdir()
        (published / "libonnxruntime.1.dylib").symlink_to(library)
        worker = self.image(
            "worker",
            macho_image(
                dependencies=("@rpath/libonnxruntime.1.dylib",),
                rpaths=("@executable_path",),
            ),
        )
        closure = derive_runtime_closure(worker, (published,))
        self.assertEqual((), closure.unresolved)
        self.assertEqual(
            [("libonnxruntime.1.30.0.dylib", "libonnxruntime.1.dylib")],
            [(entry.source.name, entry.staged_name) for entry in closure.entries],
        )
        absolute = self.image(
            "absolute-worker",
            macho_image(
                dependencies=("@rpath/libonnxruntime.1.dylib",),
                rpaths=("/Users/build/onnx/lib",),
            ),
        )
        refused = derive_runtime_closure(absolute, (published,))
        self.assertEqual((), refused.entries)
        self.assertRegex(refused.unresolved[0], "does not exist in the staged directory")
        missing = self.image(
            "missing-worker",
            macho_image(
                dependencies=("@rpath/libnotfound.1.dylib",), rpaths=("@loader_path",)
            ),
        )
        unresolved = derive_runtime_closure(missing, (published,))
        self.assertRegex(unresolved.unresolved[0], "no file was found")

    def test_staging_derives_the_runtime_closure_for_macos_payloads(self) -> None:
        payload = self.payload(PayloadPlatform.MACOS_ARM64)
        library = self.image(
            "libonnxruntime.1.30.0.dylib",
            macho_image(install_name="@rpath/libonnxruntime.1.dylib"),
        )
        published = self.root / "lib"
        published.mkdir()
        (published / "libonnxruntime.1.dylib").symlink_to(library)
        worker = self.image(
            "worker",
            macho_image(
                dependencies=("@rpath/libonnxruntime.1.dylib",),
                rpaths=("@executable_path",),
            ),
        )
        stage_neural_helper(
            payload,
            PayloadPlatform.MACOS_ARM64,
            worker,
            (),
            "build-fixture",
            runtime_search_paths=(published,),
        )
        staged = payload / "Standalone/Project SEAM.app/Contents/Resources/libonnxruntime.1.dylib"
        self.assertEqual(library.read_bytes(), staged.read_bytes())
        manifest = json.loads(
            (payload / "Standalone/Project SEAM.app/Contents/Resources/neural-helper-package.json").read_bytes()
        )
        self.assertEqual(
            [entry["path"] for entry in manifest["dependencies"]],
            ["Contents/Resources/libonnxruntime.1.dylib"],
        )

    def test_staging_refuses_a_closure_that_cannot_resolve_beside_the_helper(self) -> None:
        payload = self.payload(PayloadPlatform.MACOS_ARM64)
        worker = self.image(
            "worker",
            macho_image(
                dependencies=("/opt/homebrew/opt/protobuf/lib/libprotobuf.dylib",),
                rpaths=("@executable_path",),
            ),
        )
        with self.assertRaisesRegex(PayloadAssemblyError, "does not resolve from the staged directory"):
            stage_neural_helper(
                payload,
                PayloadPlatform.MACOS_ARM64,
                worker,
                (),
                "build-fixture",
                runtime_search_paths=(self.root,),
            )
        self.assertFalse(
            (payload / "Standalone/Project SEAM.app/Contents/Resources/neural-helper").exists()
        )

    def test_pe_imports_are_read_from_the_image_import_directory(self) -> None:
        self.assertEqual((), read_imports(pe_with_imports(())))
        self.assertEqual(
            ("KERNEL32.dll", "onnxruntime.dll"),
            read_imports(pe_with_imports(("KERNEL32.dll", "onnxruntime.dll"))),
        )
        with self.assertRaisesRegex(PayloadAssemblyError, "not a PE image"):
            read_imports(b"not-a-portable-image")
        truncated = bytearray(pe_with_imports(("KERNEL32.dll",)))
        truncated[0x3C:0x40] = (0x100000).to_bytes(4, "little")
        with self.assertRaisesRegex(PayloadAssemblyError, "missing or truncated"):
            read_imports(bytes(truncated))

    def test_windows_closure_stages_module_names_and_reports_path_references(self) -> None:
        published = self.root / "runtime"
        published.mkdir()
        runtime = self.image("onnxruntime.dll", pe_with_imports(("KERNEL32.dll",)))
        shutil.copyfile(runtime, published / "ONNXRUNTIME.dll")
        vcruntime = self.image("vcruntime140.dll", pe_with_imports(("KERNEL32.dll",)))
        shutil.copyfile(vcruntime, published / "VCRUNTIME140.dll")
        worker = self.image("worker.exe", pe_with_imports((
            "KERNEL32.dll", "ONNXRUNTIME.dll", "VCRUNTIME140.dll",
            "C:\\\\tools\\\\lib\\\\other.dll", "missing.dll")))
        closure = derive_runtime_closure(worker, (published,))
        # The host provides KERNEL32, so only the package's own modules are staged,
        # under the exact names the descriptors ask for.
        self.assertEqual(
            [("ONNXRUNTIME.dll", "ONNXRUNTIME.dll"), ("VCRUNTIME140.dll", "VCRUNTIME140.dll")],
            [(entry.source.name, entry.staged_name) for entry in closure.entries],
        )
        self.assertEqual(2, len(closure.unresolved))
        self.assertRegex(closure.unresolved[0], "names a path")
        self.assertRegex(closure.unresolved[0], "other.dll")
        self.assertRegex(closure.unresolved[1], r"missing.dll \(not found")

    def test_staging_derives_the_runtime_closure_for_windows_payloads(self) -> None:
        payload = self.payload(PayloadPlatform.WINDOWS_X64)
        published = self.root / "runtime"
        published.mkdir()
        runtime = self.image("onnxruntime.dll", pe_with_imports(("KERNEL32.dll",)))
        shutil.copyfile(runtime, published / "onnxruntime.dll")
        worker = self.image("worker.exe", pe_with_imports(("onnxruntime.dll",)))
        records = stage_neural_helper(payload, PayloadPlatform.WINDOWS_X64, worker, (),
            "build-fixture", runtime_search_paths=(published,))
        self.assertEqual(3, len(records))
        staged = payload / "Standalone/Resources/onnxruntime.dll"
        self.assertEqual(runtime.read_bytes(), staged.read_bytes())
        manifest = json.loads(
            (payload / "Standalone/Resources/neural-helper-package.json").read_bytes()
        )
        self.assertEqual([entry["path"] for entry in manifest["dependencies"]],
            ["Resources/onnxruntime.dll"])
        for row in neural_package_inventory(payload, PayloadPlatform.WINDOWS_X64, "build-fixture"):
            self.assertEqual(row["status"], "VERIFIED_FILES")
        # A worker whose import cannot resolve beside the helper is still refused.
        unresolved = self.image("unresolved.exe", pe_with_imports(("absent.dll",)))
        with self.assertRaisesRegex(PayloadAssemblyError, "absent.dll"):
            stage_neural_helper(self.payload(PayloadPlatform.WINDOWS_X64),
                PayloadPlatform.WINDOWS_X64, unresolved, (), "build-fixture",
                runtime_search_paths=(published,))

    @unittest.skipUnless(
        os.environ.get("SEAM_NEURAL_WORKER_BINARY"), "built worker not supplied"
    )
    def test_development_worker_is_staged_only_when_its_closure_resolves(self) -> None:
        # This is an invariant over the real build, not a snapshot of its current
        # state: whichever the analysis reports, staging must agree with it. The
        # development worker currently links a Homebrew protobuf/abseil closure, so
        # the refusal path is the one that exercises today.
        worker = Path(os.environ["SEAM_NEURAL_WORKER_BINARY"])
        search = tuple(
            Path(path)
            for path in (os.environ.get("SEAM_ONNXRUNTIME_LIBRARY_DIR"),)
            if path
        )
        closure = derive_runtime_closure(worker, search, required_machine="arm64")
        payload = self.payload(PayloadPlatform.MACOS_ARM64)
        if closure.unresolved:
            with self.assertRaises(PayloadAssemblyError) as raised:
                stage_neural_helper(
                    payload,
                    PayloadPlatform.MACOS_ARM64,
                    worker,
                    (),
                    "build-fixture",
                    runtime_search_paths=search,
                )
            self.assertIn("does not resolve from the staged directory", str(raised.exception))
            self.assertIn(closure.unresolved[0].split(" (")[0], str(raised.exception))
        else:
            records = stage_neural_helper(
                payload,
                PayloadPlatform.MACOS_ARM64,
                worker,
                (),
                "build-fixture",
                runtime_search_paths=search,
            )
            self.assertEqual(4, len(records))

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
