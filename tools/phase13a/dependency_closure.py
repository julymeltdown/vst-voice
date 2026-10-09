from __future__ import annotations

import re
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Final, Protocol, TypedDict, assert_never

from tools.phase13a.payload_binaries import inspect_payload_binaries
from tools.phase13a.payload_surfaces import PayloadPlatform


class BinaryInspector(Protocol):
    def imports(self, binary: Path) -> tuple[str, ...]: ...


class DependencyInspectionError(Exception):
    __slots__ = ("detail",)

    def __init__(self, detail: str) -> None:
        self.detail = detail
        super().__init__(detail)


WINDOWS_SYSTEM_DLLS: Final = frozenset(
    {
        "advapi32.dll",
        "avrt.dll",
        "bcrypt.dll",
        "comdlg32.dll",
        "crypt32.dll",
        "gdi32.dll",
        "imm32.dll",
        "kernel32.dll",
        "ntdll.dll",
        "ole32.dll",
        "oleacc.dll",
        "oleaut32.dll",
        "rpcrt4.dll",
        "secur32.dll",
        "setupapi.dll",
        "shell32.dll",
        "shlwapi.dll",
        "ucrtbase.dll",
        "uiautomationcore.dll",
        "user32.dll",
        "winmm.dll",
        "ws2_32.dll",
    }
)
WINDOWS_DLL_PATTERN: Final = re.compile(r"^[A-Za-z0-9_.+-]+\.dll$", re.IGNORECASE)


@dataclass(frozen=True, slots=True)
class BinaryDependencyRecord:
    path: str
    imports: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class DependencyClosureReport:
    platform: PayloadPlatform
    status: str
    openssl_commit: str
    binaries: tuple[BinaryDependencyRecord, ...]
    errors: tuple[str, ...]


class BinaryJson(TypedDict):
    path: str
    imports: list[str]


class ClosureJson(TypedDict):
    schemaVersion: int
    purpose: str
    platform: PayloadPlatform
    status: str
    pinnedOpenSslCommit: str
    binaries: list[BinaryJson]
    errors: list[str]


def dependency_closure_json(report: DependencyClosureReport) -> ClosureJson:
    return {
        "schemaVersion": 1,
        "purpose": "release-dependency-closure",
        "platform": report.platform,
        "status": report.status,
        "pinnedOpenSslCommit": report.openssl_commit,
        "binaries": [
            {"path": binary.path, "imports": list(binary.imports)}
            for binary in report.binaries
        ],
        "errors": list(report.errors),
    }


@dataclass(frozen=True, slots=True)
class CommandBinaryInspector:
    platform: PayloadPlatform

    def imports(self, binary: Path) -> tuple[str, ...]:
        match self.platform:
            case PayloadPlatform.MACOS_ARM64:
                command = shutil.which("otool")
                arguments = [command or "otool", "-L", str(binary)]
            case PayloadPlatform.WINDOWS_X64:
                command = shutil.which("dumpbin.exe") or shutil.which("dumpbin")
                arguments = [command or "dumpbin.exe", "/DEPENDENTS", str(binary)]
            case unreachable:
                assert_never(unreachable)
        if command is None:
            raise DependencyInspectionError(
                f"dependency inspection tool is unavailable for {self.platform}"
            )
        completed = subprocess.run(
            arguments,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if completed.returncode != 0:
            raise DependencyInspectionError(
                f"dependency inspection failed for {binary}: {completed.stderr.strip()}"
            )
        match self.platform:
            case PayloadPlatform.MACOS_ARM64:
                return tuple(
                    line.strip().split(" ", 1)[0]
                    for line in completed.stdout.splitlines()[1:]
                    if line.strip()
                )
            case PayloadPlatform.WINDOWS_X64:
                return tuple(
                    line.strip()
                    for line in completed.stdout.splitlines()
                    if WINDOWS_DLL_PATTERN.fullmatch(line.strip())
                )
            case unreachable:
                assert_never(unreachable)


def _macos_error(dependency: str) -> str | None:
    lowered = dependency.lower()
    if "libcrypto" in lowered or "libssl" in lowered:
        return f"external OpenSSL runtime is forbidden: {dependency}"
    if dependency.startswith(("/usr/lib/", "/System/Library/")):
        return None
    return f"non-system macOS dependency is forbidden: {dependency}"


def _windows_error(dependency: str) -> str | None:
    name = Path(dependency).name.lower()
    if "libcrypto" in name or "libssl" in name:
        return f"external OpenSSL runtime is forbidden: {dependency}"
    if name in WINDOWS_SYSTEM_DLLS:
        return None
    return f"non-system Windows dependency is forbidden: {dependency}"


def verify_dependency_closure(
    payload_root: Path,
    platform: PayloadPlatform,
    inspector: BinaryInspector,
    openssl_commit: str,
) -> DependencyClosureReport:
    payload = payload_root.resolve()
    errors: list[str] = []
    if len(openssl_commit) != 40 or any(
        character not in "0123456789abcdef" for character in openssl_commit
    ):
        errors.append("pinned OpenSSL commit must be a lowercase 40-character SHA-1")
    sbom_path = payload / "SBOM.spdx.json"
    if sbom_path.is_symlink() or not sbom_path.is_file():
        errors.append("SBOM.spdx.json is required for dependency closure")
    inventory = inspect_payload_binaries(payload, platform)
    binaries = inventory.binaries
    errors.extend(
        f"payload symbolic link is forbidden: {path}" for path in inventory.links
    )
    errors.extend(inventory.errors)
    if not binaries:
        errors.append("release payload contains no inspectable platform binaries")
    records: list[BinaryDependencyRecord] = []
    for binary in binaries:
        try:
            imports = inspector.imports(binary)
        except DependencyInspectionError as error:
            errors.append(error.detail)
            continue
        relative = binary.relative_to(payload).as_posix()
        records.append(BinaryDependencyRecord(path=relative, imports=imports))
        for dependency in imports:
            match platform:
                case PayloadPlatform.MACOS_ARM64:
                    issue = _macos_error(dependency)
                case PayloadPlatform.WINDOWS_X64:
                    issue = _windows_error(dependency)
                case unreachable:
                    assert_never(unreachable)
            if issue is not None:
                errors.append(f"{relative}: {issue}")
    return DependencyClosureReport(
        platform=platform,
        status="PASS" if not errors else "BLOCKED",
        openssl_commit=openssl_commit,
        binaries=tuple(records),
        errors=tuple(errors),
    )
