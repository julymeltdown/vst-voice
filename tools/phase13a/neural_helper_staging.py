"""Stage the neural worker and its runtime into a release payload.

Every payload surface launches a first-party helper beside its own module, so the
helper bytes, its runtime dependencies and the sealed helper manifest must all be
produced together. This module owns that step. It copies finalized bytes only,
and it refuses to seal an artifact whose container or machine does not match the
payload platform, so a macOS image cannot be sealed into a Windows surface (or
the reverse) just because the file names look plausible. Staging is two-phase:
every surface is validated before the first byte is written, and an already
staged artifact is left untouched when its bytes are identical and refused when
they are not.

This produces packaging input only. It does not qualify the worker, authorize a
release, or replace the release owner's signature over the deployment
descriptor.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
from pathlib import Path
from typing import Any, Iterable

from tools.phase13a.neural_package import (
    HELPER_MANIFEST_NAME,
    MAXIMUM_FILE_BYTES,
    build_neural_package_manifest,
    neural_package_layout,
)
from tools.phase13a.runtime_closure import derive_runtime_closure
from tools.phase13a.payload_paths import (
    PayloadAssemblyError,
    require_payload_path,
    require_real_directory,
)
from tools.phase13a.payload_surfaces import PayloadPlatform, surface_matrix

MACHO_MAGIC_THIN = frozenset({0xFEEDFACE, 0xFEEDFACF})
MACHO_MAGIC_FAT = frozenset({0xCAFEBABE, 0xCAFEBABF})
CPU_TYPE_X86 = 0x7
CPU_TYPE_X86_64 = 0x01000007
CPU_TYPE_ARM = 0xC
CPU_TYPE_ARM64 = 0x0100000C
CPU_NAMES = {
    CPU_TYPE_X86: "x86",
    CPU_TYPE_X86_64: "x86_64",
    CPU_TYPE_ARM: "arm",
    CPU_TYPE_ARM64: "arm64",
}
PE_MACHINE_I386 = 0x014C
PE_MACHINE_AMD64 = 0x8664
PE_MACHINE_ARM64 = 0xAA64
PE_MACHINE_NAMES = {
    PE_MACHINE_I386: "x86",
    PE_MACHINE_AMD64: "x86_64",
    PE_MACHINE_ARM64: "arm64",
}
HEADER_BYTES = 4096
MAXIMUM_FAT_SLICES = 32


def image_identity(header: bytes) -> tuple[str, tuple[str, ...]]:
    """Return the container name and declared machines of one image header.

    ``macho`` covers thin and universal Mach-O images; ``pe`` covers Windows
    images. An unrecognized header is refused rather than treated as a match.
    """
    if len(header) < 8:
        raise PayloadAssemblyError(("neural artifact header is truncated",))
    little = int.from_bytes(header[0:4], "little")
    big = int.from_bytes(header[0:4], "big")
    if big in MACHO_MAGIC_FAT:
        slices = int.from_bytes(header[4:8], "big")
        if not 1 <= slices <= MAXIMUM_FAT_SLICES:
            raise PayloadAssemblyError(("neural artifact declares an invalid slice count",))
        entry = 20 if big == 0xCAFEBABE else 32
        if len(header) < 8 + slices * entry:
            raise PayloadAssemblyError(("neural artifact fat header is truncated",))
        machines = tuple(
            CPU_NAMES.get(
                int.from_bytes(header[8 + index * entry : 12 + index * entry], "big"),
                f"cputype-0x{int.from_bytes(header[8 + index * entry : 12 + index * entry], 'big'):08x}",
            )
            for index in range(slices)
        )
        return "macho", machines
    if little in MACHO_MAGIC_THIN or big in MACHO_MAGIC_THIN:
        order = "little" if little in MACHO_MAGIC_THIN else "big"
        cpu = int.from_bytes(header[4:8], order)
        return "macho", (CPU_NAMES.get(cpu, f"cputype-0x{cpu:08x}"),)
    if header[0:2] == b"MZ" and len(header) >= 0x40:
        e_lfanew = int.from_bytes(header[0x3C:0x40], "little")
        if len(header) < e_lfanew + 6 or header[e_lfanew : e_lfanew + 4] != b"PE\0\0":
            raise PayloadAssemblyError(("neural artifact PE header is truncated",))
        machine = int.from_bytes(header[e_lfanew + 4 : e_lfanew + 6], "little")
        return "pe", (PE_MACHINE_NAMES.get(machine, f"machine-0x{machine:04x}"),)
    raise PayloadAssemblyError(("neural artifact is not a Mach-O or PE image",))


def helper_file_name(platform: PayloadPlatform) -> str:
    """Return the helper executable name the platform launches."""
    return "neural-helper.exe" if platform == PayloadPlatform.WINDOWS_X64 else "neural-helper"


def _expected_image(platform: PayloadPlatform) -> tuple[str, str]:
    if platform == PayloadPlatform.MACOS_ARM64:
        return "macho", "arm64"
    if platform == PayloadPlatform.WINDOWS_X64:
        return "pe", "x86_64"
    raise PayloadAssemblyError((f"neural helper staging does not support {platform}",))


def require_platform_image(path: Path, platform: PayloadPlatform, label: str) -> None:
    """Refuse an image whose container or machine does not match the platform."""
    expected_container, expected_machine = _expected_image(platform)
    with path.open("rb") as stream:
        header = stream.read(HEADER_BYTES)
    container, machines = image_identity(header)
    if container != expected_container or expected_machine not in machines:
        raise PayloadAssemblyError(
            (
                f"{label} is a {container} image declaring {', '.join(machines)} "
                f"but {platform} requires a {expected_container} image declaring {expected_machine}",
            )
        )


def _require_regular_file(path: Path, label: str) -> Path:
    absolute = Path(os.path.abspath(path))
    if absolute.is_symlink() or not absolute.is_file():
        raise PayloadAssemblyError((f"{label} must be a regular file: {absolute}",))
    if absolute.stat().st_size > MAXIMUM_FILE_BYTES:
        raise PayloadAssemblyError((f"{label} exceeds the neural file budget: {absolute}",))
    return absolute


def _copy_finalized(source: Path, destination: Path) -> None:
    if destination.exists():
        if destination.is_symlink() or destination.is_dir():
            raise PayloadAssemblyError(
                (f"staged neural path is not a regular file: {destination}",)
            )
        if destination.read_bytes() == source.read_bytes():
            return
        raise PayloadAssemblyError(
            (f"staged neural artifact already exists with different bytes: {destination}",)
        )
    shutil.copyfile(source, destination)
    if destination.read_bytes() != source.read_bytes():
        raise PayloadAssemblyError(
            (f"staged neural artifact differs from its source: {destination}",)
        )


def _require_unique(names: Iterable[str]) -> None:
    seen: set[str] = set()
    for name in names:
        if name in seen:
            raise PayloadAssemblyError(
                (f"neural staging would collide on the file name {name}",)
            )
        seen.add(name)


def stage_neural_helper(
    payload_root: Path,
    platform: PayloadPlatform,
    worker: Path,
    dependencies: tuple[Path, ...],
    build_id: str,
    *,
    runtime_search_paths: tuple[Path, ...] = (),
    surface_ids: tuple[str, ...] | None = None,
    protocol_version: int = 1,
) -> tuple[dict[str, Any], ...]:
    """Stage and seal the helper package for every selected payload surface.

    ``dependencies`` are explicit files staged under their own names. When
    ``runtime_search_paths`` is supplied, the worker's own load commands are read
    and its runtime closure is derived from those directories; a closure entry
    that would not resolve from the staged directory is refused rather than
    shipped as a library the helper cannot load.
    """
    payload = require_real_directory(payload_root, "payload root")
    worker_path = _require_regular_file(worker, "neural worker")
    staged_dependencies = [
        (_require_regular_file(path, f"neural dependency {path.name}"), path.name)
        for path in dependencies
    ]
    if runtime_search_paths:
        # The closure is read from the image's own linkage: load commands on macOS,
        # import descriptors on Windows. Both platforms resolve their runtime from
        # the directory the helper is launched out of, so the same rule applies.
        closure = derive_runtime_closure(
            worker_path,
            tuple(require_real_directory(path, "runtime search path") for path in runtime_search_paths),
            required_machine="arm64" if platform == PayloadPlatform.MACOS_ARM64 else "x86_64",
        )
        if closure.unresolved:
            raise PayloadAssemblyError(
                (
                    "neural worker does not resolve from the staged directory: "
                    + "; ".join(closure.unresolved),
                )
            )
        staged_dependencies.extend(
            (_require_regular_file(entry.source, f"neural dependency {entry.load_name}"), entry.staged_name)
            for entry in closure.entries
        )
    _require_unique([worker_path.name, *(name for _, name in staged_dependencies)])
    require_platform_image(worker_path, platform, "neural worker")
    for path, name in staged_dependencies:
        require_platform_image(path, platform, f"neural dependency {name}")

    selected = []
    for surface in surface_matrix(platform):
        if surface.identifier == "installer-verifier":
            continue
        if surface_ids is not None and surface.identifier not in surface_ids:
            continue
        selected.append(surface)
    if surface_ids is not None and len(selected) != len(set(surface_ids)):
        raise PayloadAssemblyError(
            (f"payload {platform} does not declare every requested surface",)
        )
    if not selected:
        raise PayloadAssemblyError((f"payload {platform} declares no stageable surface",))

    plans: list[dict[str, Any]] = []
    for surface in selected:
        package, manifest_relative = neural_package_layout(platform, surface)
        module_relative = Path(surface.binary_relative_path).relative_to(package)
        module_path = require_payload_path(payload, surface.binary_relative_path)
        if module_path.is_symlink() or not module_path.is_file():
            raise PayloadAssemblyError(
                (f"{surface.identifier}: payload module must be a regular file",)
            )
        resources_relative = manifest_relative.parent
        helper_relative = resources_relative / helper_file_name(platform)
        dependency_relatives = tuple(
            resources_relative / name for _, name in staged_dependencies
        )
        _require_unique(
            [
                module_relative.name,
                helper_relative.name,
                *(path.name for path in dependency_relatives),
            ]
        )
        plans.append(
            {
                "surface": surface.identifier,
                "package": package,
                "manifestRelative": manifest_relative,
                "moduleRelative": module_relative,
                "helperRelative": helper_relative,
                "dependencyRelatives": dependency_relatives,
            }
        )

    records: list[dict[str, Any]] = []
    for plan in plans:
        package_root = require_real_directory(payload / plan["package"], "neural package root")
        resources = package_root / plan["helperRelative"].parent
        if not resources.exists():
            resources.mkdir(parents=True)
        if resources.is_symlink() or not resources.is_dir():
            raise PayloadAssemblyError(
                (f"{plan['surface']}: neural resources path is not a real directory",)
            )
        _copy_finalized(worker_path, package_root / plan["helperRelative"])
        for (source, _), relative in zip(
            staged_dependencies, plan["dependencyRelatives"], strict=True
        ):
            _copy_finalized(source, package_root / relative)
        manifest, digest = build_neural_package_manifest(
            package_root,
            build_id,
            plan["moduleRelative"].as_posix(),
            plan["helperRelative"].as_posix(),
            tuple(path.as_posix() for path in plan["dependencyRelatives"]),
            protocol_version=protocol_version,
        )
        manifest_path = package_root / plan["manifestRelative"]
        temporary = manifest_path.with_name(f".{manifest_path.name}.staging")
        temporary.write_bytes(manifest)
        os.replace(temporary, manifest_path)
        records.append(
            {
                "surface": plan["surface"],
                "path": (plan["package"] / plan["manifestRelative"]).as_posix(),
                "status": "VERIFIED_FILES",
                "sha256": digest,
            }
        )
    return tuple(records)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Stage the neural worker and runtime into one release payload"
    )
    parser.add_argument("--payload", type=Path, required=True)
    parser.add_argument("--platform", choices=tuple(PayloadPlatform), required=True)
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--dependency", type=Path, action="append", default=[])
    parser.add_argument(
        "--runtime-search-path",
        type=Path,
        action="append",
        default=[],
        help="Derive the worker's runtime closure from its own linkage",
    )
    parser.add_argument("--build-id", required=True)
    parser.add_argument("--surface", action="append", default=None)
    parser.add_argument("--protocol-version", type=int, choices=(1, 2), default=1)
    arguments = parser.parse_args(argv)
    try:
        records = stage_neural_helper(
            arguments.payload,
            PayloadPlatform(arguments.platform),
            arguments.worker,
            tuple(arguments.dependency),
            arguments.build_id,
            surface_ids=tuple(arguments.surface) if arguments.surface else None,
            runtime_search_paths=tuple(arguments.runtime_search_path),
            protocol_version=arguments.protocol_version,
        )
    except PayloadAssemblyError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 3
    print(
        json.dumps(
            {"helperManifest": HELPER_MANIFEST_NAME, "surfaces": list(records)},
            ensure_ascii=False,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
