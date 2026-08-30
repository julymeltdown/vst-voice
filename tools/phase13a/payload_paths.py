from __future__ import annotations

import os
from pathlib import Path

from tools.phase13a.distribution_manifest import tree_sha256


class PayloadAssemblyError(Exception):
    __slots__ = ("issues",)

    def __init__(self, issues: tuple[str, ...]) -> None:
        self.issues = issues
        super().__init__(*issues)

    def __str__(self) -> str:
        return "; ".join(self.issues)


def require_real_directory(path: Path, label: str) -> Path:
    absolute = Path(os.path.abspath(path))
    if absolute.is_symlink():
        raise PayloadAssemblyError((f"{label} is a symbolic link: {absolute}",))
    resolved = absolute.resolve()
    if not resolved.is_dir():
        raise PayloadAssemblyError((f"{label} must be a real directory",))
    return resolved


def require_payload_path(root: Path, relative_path: str) -> Path:
    relative = Path(relative_path)
    if (
        relative.is_absolute()
        or not relative.parts
        or any(part in {".", ".."} for part in relative.parts)
    ):
        raise PayloadAssemblyError((f"payload path is unsafe: {relative_path}",))
    root = root.resolve()
    path = root / relative
    current = root
    for part in relative.parts:
        current /= part
        if current.is_symlink():
            raise PayloadAssemblyError((f"payload path is linked: {relative_path}",))
    if not path.resolve(strict=False).is_relative_to(root):
        raise PayloadAssemblyError((f"payload path escapes root: {relative_path}",))
    if path.is_symlink() or not path.exists():
        raise PayloadAssemblyError(
            (f"required payload path is missing: {relative_path}",)
        )
    for item in path.rglob("*") if path.is_dir() else ():
        if item.is_symlink():
            raise PayloadAssemblyError((f"payload contains a symbolic link: {item}",))
    return path


def payload_entry(
    root: Path, identifier: str, relative_path: str
) -> dict[str, str | int]:
    try:
        path = require_payload_path(root, relative_path)
    except PayloadAssemblyError as error:
        raise PayloadAssemblyError((f"{identifier}: {error}",)) from error
    size = (
        path.stat().st_size
        if path.is_file()
        else sum(item.stat().st_size for item in path.rglob("*") if item.is_file())
    )
    return {
        "id": identifier,
        "path": relative_path,
        "size": size,
        "sha256": tree_sha256(path),
    }
