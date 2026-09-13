"""Read what a Windows PE image imports, so packaging can seal its runtime.

The Windows loader resolves a helper's imports from the directory that holds the
helper before it searches anywhere else, so a packaged helper needs every
non-system module beside it. This module reads the import descriptors of one PE
image and returns the module names the image asks for, which is the Windows
counterpart of reading a Mach-O load command table. A reference that names a path
(an absolute path, a drive letter or a directory part) is returned as written:
the closure owner reports it as unresolved, because copying the module would not
repair a reference that does not resolve from the staged directory.
"""

from __future__ import annotations

from tools.phase13a.payload_paths import PayloadAssemblyError


PE_SIGNATURE = b"PE\0\0"
DOS_HEADER_BYTES = 0x40
COFF_HEADER_BYTES = 20
IMPORT_DESCRIPTOR_BYTES = 20
IMPORT_DIRECTORY_INDEX = 1
OPTIONAL_PE32 = 0x10B
OPTIONAL_PE32_PLUS = 0x20B
PE32_DATA_DIRECTORY_OFFSET = 96
PE32_PLUS_DATA_DIRECTORY_OFFSET = 112
MAXIMUM_SECTIONS = 96
MAXIMUM_IMPORT_DESCRIPTORS = 4096


def read_imports(data: bytes) -> tuple[str, ...]:
    """Return the module names one PE image imports, in descriptor order."""
    if len(data) < DOS_HEADER_BYTES or data[0:2] != b"MZ":
        raise PayloadAssemblyError(("file is not a PE image",))
    pe_offset = int.from_bytes(data[0x3C:0x40], "little")
    if pe_offset < DOS_HEADER_BYTES or pe_offset + 24 > len(data) or data[pe_offset : pe_offset + 4] != PE_SIGNATURE:
        raise PayloadAssemblyError(("PE header is missing or truncated",))
    coff = pe_offset + 4
    section_count = int.from_bytes(data[coff + 2 : coff + 4], "little")
    optional_size = int.from_bytes(data[coff + 16 : coff + 18], "little")
    optional = coff + COFF_HEADER_BYTES
    if section_count == 0 or section_count > MAXIMUM_SECTIONS:
        raise PayloadAssemblyError(("PE section count is invalid",))
    if optional + optional_size > len(data) or optional_size < 2:
        raise PayloadAssemblyError(("PE optional header is truncated",))
    magic = int.from_bytes(data[optional : optional + 2], "little")
    if magic == OPTIONAL_PE32:
        directory_offset = optional + PE32_DATA_DIRECTORY_OFFSET
    elif magic == OPTIONAL_PE32_PLUS:
        directory_offset = optional + PE32_PLUS_DATA_DIRECTORY_OFFSET
    else:
        raise PayloadAssemblyError(("PE optional header magic is unsupported",))
    directory = directory_offset + IMPORT_DIRECTORY_INDEX * 8
    if directory + 8 > optional + optional_size or directory + 8 > len(data):
        raise PayloadAssemblyError(("PE data directory is truncated",))
    import_rva = int.from_bytes(data[directory : directory + 4], "little")
    import_size = int.from_bytes(data[directory + 4 : directory + 8], "little")
    section_table = optional + optional_size
    if section_table + section_count * 40 > len(data):
        raise PayloadAssemblyError(("PE section table is truncated",))
    sections: list[tuple[int, int, int, int]] = []
    for index in range(section_count):
        entry = section_table + index * 40
        virtual_size = int.from_bytes(data[entry + 8 : entry + 12], "little")
        virtual_address = int.from_bytes(data[entry + 12 : entry + 16], "little")
        raw_size = int.from_bytes(data[entry + 16 : entry + 20], "little")
        raw_pointer = int.from_bytes(data[entry + 20 : entry + 24], "little")
        sections.append((virtual_address, virtual_size, raw_size, raw_pointer))

    def offset_for(rva: int) -> int | None:
        for virtual_address, virtual_size, raw_size, raw_pointer in sections:
            span = max(virtual_size, raw_size)
            if virtual_address <= rva < virtual_address + span:
                offset = raw_pointer + (rva - virtual_address)
                return offset if 0 <= offset < len(data) else None
        return None

    if import_rva == 0:
        return ()
    names: list[str] = []
    cursor = offset_for(import_rva)
    if cursor is None:
        raise PayloadAssemblyError(("PE import directory is outside the image",))
    limit = MAXIMUM_IMPORT_DESCRIPTORS
    if import_size >= IMPORT_DESCRIPTOR_BYTES:
        limit = min(limit, import_size // IMPORT_DESCRIPTOR_BYTES + 1)
    for _ in range(limit):
        if cursor + IMPORT_DESCRIPTOR_BYTES > len(data):
            raise PayloadAssemblyError(("PE import descriptor is truncated",))
        descriptor = data[cursor : cursor + IMPORT_DESCRIPTOR_BYTES]
        if descriptor == bytes(IMPORT_DESCRIPTOR_BYTES):
            return tuple(names)
        name_rva = int.from_bytes(descriptor[12:16], "little")
        if name_rva == 0:
            raise PayloadAssemblyError(("PE import descriptor has no module name",))
        name_offset = offset_for(name_rva)
        if name_offset is None:
            raise PayloadAssemblyError(("PE import name is outside the image",))
        terminator = data.find(b"\0", name_offset, min(len(data), name_offset + 4096))
        if terminator < 0:
            raise PayloadAssemblyError(("PE import name is not terminated",))
        try:
            name = data[name_offset:terminator].decode("ascii")
        except UnicodeDecodeError as error:
            raise PayloadAssemblyError(("PE import name is not valid text",)) from error
        if not name:
            raise PayloadAssemblyError(("PE import name is empty",))
        names.append(name)
        cursor += IMPORT_DESCRIPTOR_BYTES
    raise PayloadAssemblyError(("PE import descriptor table exceeds its budget",))


def is_path_reference(module: str) -> bool:
    """Return whether an import names a path instead of a loadable module."""
    return any(character in module for character in ("/", "\\", ":"))


__all__ = ["is_path_reference", "read_imports"]
