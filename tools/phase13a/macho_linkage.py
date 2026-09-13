"""Read what a Mach-O image loads, so packaging can tell whether it will run.

A payload helper is launched from the bundle's resources directory, so every
image it loads must resolve from there. The development worker links
``@rpath/libonnxruntime.1.dylib`` through an absolute build-directory rpath, which
silently works on the build machine and fails on a user's machine. Copying the
library into the package would not repair that, because the load command still
resolves through an rpath that does not exist outside the build tree.

This module answers the question the packager actually has: which images does
this one load, which of them are provided by the operating system, and which of
them would resolve from the staged directory. It refuses to guess: an absolute
reference, an ``@rpath`` reference with no staged rpath, and a reference that
cannot be found in the supplied search paths are all reported instead of being
silently copied.
"""

from __future__ import annotations

from dataclasses import dataclass

from tools.phase13a.payload_paths import PayloadAssemblyError


MACHO_MAGIC_THIN_64 = 0xFEEDFACF
MACHO_MAGIC_THIN_32 = 0xFEEDFACE
MACHO_MAGIC_FAT_32 = 0xCAFEBABE
MACHO_MAGIC_FAT_64 = 0xCAFEBABF
LC_REQ_DYLD = 0x80000000
LC_ID_DYLIB = 0xD
LC_LOAD_DYLIB = 0xC
LC_LOAD_WEAK_DYLIB = 0x18 | LC_REQ_DYLD
LC_REEXPORT_DYLIB = 0x1F | LC_REQ_DYLD
LC_LOAD_UPWARD_DYLIB = 0x23 | LC_REQ_DYLD
LC_LAZY_LOAD_DYLIB = 0x20
LC_RPATH = 0x1C | LC_REQ_DYLD
DEPENDENCY_COMMANDS = frozenset(
    {
        LC_LOAD_DYLIB,
        LC_LOAD_WEAK_DYLIB,
        LC_REEXPORT_DYLIB,
        LC_LOAD_UPWARD_DYLIB,
        LC_LAZY_LOAD_DYLIB,
    }
)
MAXIMUM_LOAD_COMMANDS = 4096
MAXIMUM_LOAD_COMMAND_BYTES = 1 << 20
SYSTEM_PREFIXES = ("/usr/lib/", "/System/Library/", "/System/iOSSupport/")
STAGED_RPATH_ROOTS = (
    "@executable_path",
    "@executable_path/",
    "@executable_path/.",
    "@loader_path",
    "@loader_path/",
    "@loader_path/.",
)


@dataclass(frozen=True, slots=True)
class MachoLinkage:
    """The load commands of one Mach-O image that decide its runtime closure."""

    install_name: str | None
    rpaths: tuple[str, ...]
    dependencies: tuple[str, ...]

    def staged_dependencies(self) -> tuple[str, ...]:
        """Return the dependencies a package must provide itself."""
        return tuple(name for name in self.dependencies if not is_system_library(name))


def is_system_library(load_name: str) -> bool:
    """Return whether the host provides this image rather than the package."""
    return load_name.startswith(SYSTEM_PREFIXES)


def rpath_reaches_staged_directory(rpath: str) -> bool:
    """Return whether an rpath resolves to the directory holding the helper."""
    return rpath in STAGED_RPATH_ROOTS


def staged_name_for(load_name: str) -> str | None:
    """Return the staged file name a load command resolves to, if it can."""
    if is_system_library(load_name) or load_name.startswith("@"):
        prefix, _, tail = load_name.rpartition("/")
        if prefix.startswith("@rpath") or prefix.startswith("@executable_path") or prefix.startswith("@loader_path"):
            return tail or None
        return None
    name = load_name.rsplit("/", 1)[-1]
    return name or None


def resolves_from_staged_directory(load_name: str, rpaths: tuple[str, ...]) -> bool:
    """Return whether the load command resolves inside the staged directory."""
    if is_system_library(load_name):
        return True
    if load_name.startswith("@executable_path/") or load_name.startswith("@loader_path/"):
        return True
    if load_name.startswith("@rpath/"):
        return any(rpath_reaches_staged_directory(rpath) for rpath in rpaths)
    return False


def _read_c_string(data: bytes, start: int, limit: int, label: str) -> str:
    if start < 0 or start >= limit:
        raise PayloadAssemblyError((f"Mach-O {label} offset is out of range",))
    end = data.find(b"\0", start, limit)
    if end < 0:
        raise PayloadAssemblyError((f"Mach-O {label} is not terminated",))
    try:
        value = data[start:end].decode("utf-8")
    except UnicodeDecodeError as error:
        raise PayloadAssemblyError((f"Mach-O {label} is not valid text",)) from error
    if not value or any(ord(character) < 0x20 or ord(character) == 0x7F for character in value):
        raise PayloadAssemblyError((f"Mach-O {label} is empty or contains control characters",))
    return value


def _parse_slice(data: bytes, offset: int) -> MachoLinkage:
    if offset + 28 > len(data):
        raise PayloadAssemblyError(("Mach-O header is truncated",))
    magic_little = int.from_bytes(data[offset : offset + 4], "little")
    magic_big = int.from_bytes(data[offset : offset + 4], "big")
    if magic_little in {MACHO_MAGIC_THIN_64, MACHO_MAGIC_THIN_32}:
        order, header_size = "little", 32 if magic_little == MACHO_MAGIC_THIN_64 else 28
    elif magic_big in {MACHO_MAGIC_THIN_64, MACHO_MAGIC_THIN_32}:
        order, header_size = "big", 32 if magic_big == MACHO_MAGIC_THIN_64 else 28
    else:
        raise PayloadAssemblyError(("file is not a Mach-O image slice",))
    commands = int.from_bytes(data[offset + 16 : offset + 20], order)
    command_bytes = int.from_bytes(data[offset + 20 : offset + 24], order)
    if commands > MAXIMUM_LOAD_COMMANDS or command_bytes > MAXIMUM_LOAD_COMMAND_BYTES:
        raise PayloadAssemblyError(("Mach-O declares an invalid load-command budget",))
    start = offset + header_size
    end = start + command_bytes
    if end > len(data):
        raise PayloadAssemblyError(("Mach-O load commands are truncated",))
    install_name: str | None = None
    rpaths: list[str] = []
    dependencies: list[str] = []
    cursor = start
    for _ in range(commands):
        if cursor + 8 > end:
            raise PayloadAssemblyError(("Mach-O load command is truncated",))
        command = int.from_bytes(data[cursor : cursor + 4], order)
        size = int.from_bytes(data[cursor + 4 : cursor + 8], order)
        if size < 8 or cursor + size > end:
            raise PayloadAssemblyError(("Mach-O load command size is invalid",))
        if command in DEPENDENCY_COMMANDS or command == LC_ID_DYLIB:
            name_offset = int.from_bytes(data[cursor + 8 : cursor + 12], order)
            name = _read_c_string(data, cursor + name_offset, cursor + size, "dylib name")
            if command == LC_ID_DYLIB:
                if install_name is not None:
                    raise PayloadAssemblyError(("Mach-O declares more than one install name",))
                install_name = name
            else:
                dependencies.append(name)
        elif command == LC_RPATH:
            path_offset = int.from_bytes(data[cursor + 8 : cursor + 12], order)
            rpaths.append(_read_c_string(data, cursor + path_offset, cursor + size, "rpath"))
        cursor += size
    return MachoLinkage(install_name, tuple(rpaths), tuple(dependencies))


def read_linkage(data: bytes, *, required_machine: str | None = None) -> MachoLinkage:
    """Return the load commands of one Mach-O image.

    Universal images carry one set of load commands per slice, and the slices can
    disagree, so ``required_machine`` selects the slice the payload will run.
    """
    if len(data) < 8:
        raise PayloadAssemblyError(("Mach-O header is truncated",))
    magic = int.from_bytes(data[0:4], "big")
    if magic not in {MACHO_MAGIC_FAT_32, MACHO_MAGIC_FAT_64}:
        return _parse_slice(data, 0)
    slices = int.from_bytes(data[4:8], "big")
    if not 1 <= slices <= 32:
        raise PayloadAssemblyError(("Mach-O fat header declares an invalid slice count",))
    entry = 20 if magic == MACHO_MAGIC_FAT_32 else 32
    if len(data) < 8 + slices * entry:
        raise PayloadAssemblyError(("Mach-O fat header is truncated",))
    cpu_names = {0x7: "x86", 0x01000007: "x86_64", 0xC: "arm", 0x0100000C: "arm64"}
    candidates: list[tuple[int, int]] = []
    for index in range(slices):
        base = 8 + index * entry
        cpu = int.from_bytes(data[base : base + 4], "big")
        if magic == MACHO_MAGIC_FAT_32:
            offset = int.from_bytes(data[base + 8 : base + 12], "big")
        else:
            offset = int.from_bytes(data[base + 8 : base + 16], "big")
        candidates.append((cpu, offset))
    chosen = None
    for cpu, offset in candidates:
        if required_machine is None or cpu_names.get(cpu) == required_machine:
            chosen = offset
            break
    if chosen is None:
        raise PayloadAssemblyError(
            (f"Mach-O universal image does not carry the {required_machine} slice",)
        )
    return _parse_slice(data, chosen)


__all__ = [
    "MachoLinkage",
    "is_system_library",
    "read_linkage",
    "resolves_from_staged_directory",
    "rpath_reaches_staged_directory",
    "staged_name_for",
]
