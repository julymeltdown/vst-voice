from __future__ import annotations

import shutil
from collections.abc import Iterable, Mapping
from pathlib import Path


def materialize_supporting_files(
    source_root: Path,
    dependency_root: Path,
    dependencies: Iterable[Mapping[str, object]],
    output: Path,
    include_auv2: bool,
) -> None:
    notices = output / "Notices"
    documentation = output / "Documentation"
    for directory in (notices, documentation):
        if directory.is_symlink() or directory.is_file():
            directory.unlink()
        elif directory.exists():
            shutil.rmtree(directory)
        directory.mkdir(parents=True)
    for dependency in dependencies:
        name = str(dependency["name"])
        if name == "AudioUnitSDK" and not include_auv2:
            continue
        checkout = dependency_root / name
        for license_path in list(checkout.glob("LICENSE*")) + list(
            checkout.glob("COPYING*")
        ):
            if license_path.is_file():
                shutil.copy2(license_path, notices / f"{name}-{license_path.name}")
    for notice in ("THIRD_PARTY_NOTICES.md", "SBOM.spdx.json"):
        shutil.copy2(source_root / notice, output / notice)
    for documentation_root in (
        source_root / "docs/manual",
        source_root / "docs/support",
    ):
        destination = documentation / documentation_root.name
        destination.mkdir(parents=True, exist_ok=True)
        for document in sorted(documentation_root.glob("*.md")):
            shutil.copy2(document, destination / document.name)
    documentation_manifest = (
        source_root / "docs/product/external-beta-documentation.json"
    )
    shutil.copy2(
        documentation_manifest,
        documentation / documentation_manifest.name,
    )
