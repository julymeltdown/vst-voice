"""Derive the runtime closure a packaged helper needs beside it.

A payload helper is launched from the bundle's resources directory, so every
image it loads must resolve from there. The two platforms express that
differently and the difference matters:

* macOS records a load name per dependency and resolves ``@rpath`` entries through
  the image's own LC_RPATH list, so an absolute build-directory rpath works here
  and fails on a user's machine. See ``macho_linkage``.
* Windows records only module names in its import descriptors, and the loader
  searches the directory of the running image before anywhere else. A bare module
  name therefore resolves beside the helper, while a descriptor that names a path
  does not. See ``pe_linkage``.

This module answers the packaging question for either container: which modules the
host provides, which ones the package must ship, and which references would not
resolve after installation. It refuses to guess, and a module that cannot be found
in the supplied search paths is reported rather than silently omitted.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from tools.phase13a import pe_linkage
from tools.phase13a.macho_linkage import (
    is_system_library,
    read_linkage,
    resolves_from_staged_directory,
    staged_name_for,
)
from tools.phase13a.payload_paths import PayloadAssemblyError


# Modules the Windows host provides. Anything outside this set is treated as a
# package responsibility, including the VC++ runtime: assuming it is present is
# how a helper that runs on the build machine fails on a user's machine.
WINDOWS_SYSTEM_MODULES = frozenset(
    {
        "advapi32.dll",
        "bcrypt.dll",
        "comdlg32.dll",
        "crypt32.dll",
        "dbghelp.dll",
        "gdi32.dll",
        "imm32.dll",
        "kernel32.dll",
        "mfplat.dll",
        "msvcrt.dll",
        "ntdll.dll",
        "ole32.dll",
        "oleaut32.dll",
        "powrprof.dll",
        "setupapi.dll",
        "shell32.dll",
        "shlwapi.dll",
        "user32.dll",
        "userenv.dll",
        "uiautomationcore.dll",
        "uuid.dll",
        "version.dll",
        "winmm.dll",
        "ws2_32.dll",
    }
)
WINDOWS_SYSTEM_PREFIXES = ("api-ms-win-", "ext-ms-win-")
MAXIMUM_CLOSURE_DEPTH = 8


@dataclass(frozen=True, slots=True)
class ClosureEntry:
    """One file to stage, and the name its dependent asks for."""

    source: Path
    staged_name: str
    load_name: str


@dataclass(frozen=True, slots=True)
class RuntimeClosure:
    entries: tuple[ClosureEntry, ...]
    unresolved: tuple[str, ...]


def is_windows_system_module(module: str) -> bool:
    """Return whether the Windows host provides this module."""
    name = module.casefold()
    return name in WINDOWS_SYSTEM_MODULES or name.startswith(WINDOWS_SYSTEM_PREFIXES)


def _find_in_search_paths(name: str, search_paths: tuple[Path, ...]) -> Path | None:
    """Find a module by exact name, case-insensitively, following symbolic links.

    Windows resolves module names case-insensitively and publishes the versioned
    library behind a plain-name copy, so the search follows links and returns the
    real file that gets staged under the requested name.
    """
    folded = name.casefold()
    for directory in search_paths:
        direct = directory / name
        if direct.exists():
            resolved = direct.resolve()
            if resolved.is_file():
                return resolved
        if not directory.is_dir():
            continue
        for entry in sorted(directory.iterdir(), key=lambda item: item.name.casefold()):
            if entry.name.casefold() != folded:
                continue
            resolved = entry.resolve()
            if resolved.is_file():
                return resolved
    return None


def _windows_closure(
    worker: Path, search_paths: tuple[Path, ...], maximum_depth: int
) -> RuntimeClosure:
    entries: list[ClosureEntry] = []
    unresolved: list[str] = []
    seen: set[str] = set()
    pending: list[tuple[Path, tuple[str, ...], int]] = [
        (worker, pe_linkage.read_imports(worker.read_bytes()), 0)
    ]
    while pending:
        _, imports, depth = pending.pop(0)
        for module in imports:
            if is_windows_system_module(module):
                continue
            if pe_linkage.is_path_reference(module):
                unresolved.append(
                    f"{module} (import descriptor names a path, so it does not "
                    "resolve from the staged directory beside the helper)"
                )
                continue
            folded = module.casefold()
            if folded in seen:
                continue
            source = _find_in_search_paths(module, search_paths)
            if source is None:
                unresolved.append(
                    f"{module} (not found in the supplied search paths, so the "
                    "package would ship a helper whose import cannot load)"
                )
                continue
            seen.add(folded)
            entries.append(ClosureEntry(source, module, module))
            if depth + 1 >= maximum_depth:
                unresolved.append(f"{module} (closure depth limit reached)")
                continue
            pending.append((source, pe_linkage.read_imports(source.read_bytes()), depth + 1))
    return RuntimeClosure(tuple(entries), tuple(unresolved))


def _macho_closure(
    worker: Path, search_paths: tuple[Path, ...], required_machine: str | None, maximum_depth: int
) -> RuntimeClosure:
    entries: list[ClosureEntry] = []
    unresolved: list[str] = []
    seen_names: set[str] = set()
    pending: list[tuple[Path, object, int]] = [
        (worker, read_linkage(worker.read_bytes(), required_machine=required_machine), 0)
    ]
    while pending:
        _, linkage, depth = pending.pop(0)
        for load_name in linkage.dependencies:
            if is_system_library(load_name):
                continue
            name = staged_name_for(load_name)
            if name is None or name in seen_names:
                continue
            source = _resolve_macho_load_name(load_name, search_paths)
            if not resolves_from_staged_directory(load_name, linkage.rpaths):
                detail = f"found at {source}" if source is not None else "not found in the supplied search paths"
                unresolved.append(
                    f"{load_name} ({detail}, but the image references it through a path "
                    "or rpath that does not exist in the staged directory; rpaths: "
                    f"{', '.join(linkage.rpaths) or 'none'})"
                )
                continue
            if source is None:
                unresolved.append(
                    f"{load_name} (resolves from the staged directory, but no file was "
                    "found in the supplied search paths)"
                )
                continue
            seen_names.add(name)
            entries.append(ClosureEntry(source, name, load_name))
            if depth + 1 >= maximum_depth:
                unresolved.append(f"{load_name} (closure depth limit reached)")
                continue
            pending.append(
                (source, read_linkage(source.read_bytes(), required_machine=required_machine), depth + 1)
            )
    return RuntimeClosure(tuple(entries), tuple(unresolved))


def _resolve_macho_load_name(load_name: str, search_paths: tuple[Path, ...]) -> Path | None:
    name = staged_name_for(load_name)
    if name is None:
        return None
    for directory in search_paths:
        candidate = directory / name
        if not candidate.exists():
            continue
        resolved = candidate.resolve()
        if resolved.is_file():
            return resolved
    return None


def derive_runtime_closure(
    worker: Path,
    search_paths: tuple[Path, ...],
    *,
    required_machine: str | None = None,
    maximum_depth: int = MAXIMUM_CLOSURE_DEPTH,
) -> RuntimeClosure:
    """Return every image the worker needs the package to ship.

    A module the host provides is skipped; a module the package must ship is
    listed with the name its dependent asks for; and a reference that would not
    resolve from the staged directory is reported instead of copied.
    """
    if not worker.is_file():
        raise PayloadAssemblyError((f"neural artifact is not a regular file: {worker}",))
    header = worker.read_bytes()[:2]
    if header == b"MZ":
        return _windows_closure(worker, search_paths, maximum_depth)
    return _macho_closure(worker, search_paths, required_machine, maximum_depth)


__all__ = [
    "ClosureEntry",
    "RuntimeClosure",
    "derive_runtime_closure",
    "is_windows_system_module",
]
