from __future__ import annotations

import hashlib
import subprocess
from pathlib import Path


Patch = tuple[str, str]


class CompatibilityPatchError(RuntimeError):
    pass


def apply_patch_once(checkout: Path, patch: Path) -> str:
    if not checkout.is_dir() or not (checkout / "src").exists():
        return ""
    digest = hashlib.sha256(patch.read_bytes()).hexdigest()
    marker = checkout / ".phase13a-applied-patches"
    entries = (
        set(marker.read_text(encoding="utf-8").splitlines())
        if marker.is_file()
        else set()
    )
    entry = f"{patch.name}:{digest}"
    if entry in entries:
        return entry
    check = subprocess.run(
        ["git", "-C", str(checkout), "apply", "--unidiff-zero", "--check", str(patch)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if check.returncode == 0:
        subprocess.run(
            ["git", "-C", str(checkout), "apply", "--unidiff-zero", str(patch)], check=True
        )
    else:
        reverse = subprocess.run(
            [
                "git",
                "-C",
                str(checkout),
                "apply",
                "--unidiff-zero",
                "--reverse",
                "--check",
                str(patch),
            ],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if reverse.returncode != 0:
            detail = check.stderr.strip() or reverse.stderr.strip()
            raise CompatibilityPatchError(
                f"unable to apply compatibility patch {patch.name}: {detail}"
            )
    entries.add(entry)
    marker.write_text("\n".join(sorted(entries)) + "\n", encoding="utf-8")
    return entry


def apply_phase13a_patches(
    source_root: Path,
    dependencies: Path,
    host_system: str,
    patches: tuple[Patch, ...],
) -> list[str]:
    applied: list[str] = []
    for dependency_name, relative_patch in patches:
        if (
            relative_patch.endswith("macos-cfstring-buffer.patch")
            and host_system != "Darwin"
        ):
            continue
        entry = apply_patch_once(
            dependencies / dependency_name,
            (source_root / relative_patch).resolve(),
        )
        if entry:
            applied.append(entry)
    return applied
