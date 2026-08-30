from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Final, assert_never

from tools.phase13a.payload_surfaces import PayloadPlatform, surface_matrix


MACOS_MAGICS: Final = frozenset(
    {
        bytes.fromhex("feedface"),
        bytes.fromhex("feedfacf"),
        bytes.fromhex("cefaedfe"),
        bytes.fromhex("cffaedfe"),
        bytes.fromhex("cafebabe"),
        bytes.fromhex("cafebabf"),
        bytes.fromhex("bebafeca"),
        bytes.fromhex("bfbafeca"),
    }
)


@dataclass(frozen=True, slots=True)
class PayloadBinaryInventory:
    links: tuple[Path, ...]
    binaries: tuple[Path, ...]
    errors: tuple[str, ...]


def _is_binary(path: Path, platform: PayloadPlatform) -> bool:
    try:
        with path.open("rb") as stream:
            magic = stream.read(4)
    except OSError:
        return False
    match platform:
        case PayloadPlatform.MACOS_ARM64:
            return magic in MACOS_MAGICS
        case PayloadPlatform.WINDOWS_X64:
            return magic.startswith(b"MZ")
        case unreachable:
            assert_never(unreachable)


def _has_expected_architecture(path: Path, platform: PayloadPlatform) -> bool:
    try:
        with path.open("rb") as stream:
            header = stream.read(70)
            match platform:
                case PayloadPlatform.MACOS_ARM64:
                    if len(header) < 8:
                        return False
                    if header[:4] == bytes.fromhex("cffaedfe"):
                        return int.from_bytes(header[4:8], "little") == 0x0100000C
                    if header[:4] == bytes.fromhex("feedfacf"):
                        return int.from_bytes(header[4:8], "big") == 0x0100000C
                    return False
                case PayloadPlatform.WINDOWS_X64:
                    if len(header) < 64 or not header.startswith(b"MZ"):
                        return False
                    stream.seek(int.from_bytes(header[60:64], "little"))
                    pe_header = stream.read(6)
                    return (
                        pe_header[:4] == b"PE\x00\x00" and pe_header[4:6] == b"\x64\x86"
                    )
                case unreachable:
                    assert_never(unreachable)
    except OSError:
        return False


def inspect_payload_binaries(
    payload: Path, platform: PayloadPlatform
) -> PayloadBinaryInventory:
    links: list[Path] = []
    binaries: list[Path] = []
    errors: list[str] = []
    for path in sorted(payload.rglob("*")):
        if path.is_symlink():
            links.append(path)
            continue
        if not path.is_file():
            continue
        if _is_binary(path, platform):
            binaries.append(path)
    for surface in surface_matrix(platform):
        required = payload / surface.binary_relative_path
        if required.is_symlink() or not required.is_file():
            errors.append(
                f"{surface.identifier}: required platform binary is missing: "
                f"{surface.binary_relative_path}"
            )
        elif not _has_expected_architecture(required, platform):
            errors.append(
                f"{surface.identifier}: platform binary has wrong format or architecture: "
                f"{surface.binary_relative_path}"
            )
    return PayloadBinaryInventory(
        links=tuple(links),
        binaries=tuple(binaries),
        errors=tuple(errors),
    )
