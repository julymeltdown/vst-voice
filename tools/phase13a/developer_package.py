#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXED_TIMESTAMP = (1980, 1, 1, 0, 0, 0)

INSTALL_SCRIPT = b"""#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
data_root="${SEAM_INSTALL_ROOT:-${XDG_DATA_HOME:-$HOME/.local/share}/ProjectSEAM}"
clap_root="${SEAM_CLAP_ROOT:-$HOME/.clap}"
mkdir -p "$data_root" "$clap_root"
cp "$root/CLAP/ProjectSEAMEditor.clap" "$clap_root/ProjectSEAMEditor.clap"
rm -rf "$data_root/ProjectSEAMEditor.resources"
cp -R "$root/CLAP/ProjectSEAMEditor.resources" "$data_root/ProjectSEAMEditor.resources"
cp "$root/developer-package.json" "$data_root/developer-package.json"
printf '%s\n' "$clap_root/ProjectSEAMEditor.clap" > "$data_root/installed-files.txt"
"""

UNINSTALL_SCRIPT = b"""#!/usr/bin/env bash
set -euo pipefail
data_root="${SEAM_INSTALL_ROOT:-${XDG_DATA_HOME:-$HOME/.local/share}/ProjectSEAM}"
clap_root="${SEAM_CLAP_ROOT:-$HOME/.clap}"
rm -f "$clap_root/ProjectSEAMEditor.clap"
rm -rf "$data_root"
"""


def _write_bytes(archive: zipfile.ZipFile, name: str, data: bytes, executable: bool = False) -> None:
    info = zipfile.ZipInfo(name, FIXED_TIMESTAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    info.external_attr = ((0o755 if executable else 0o644) & 0xFFFF) << 16
    archive.writestr(info, data)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def create_developer_package(
    clap_module: Path,
    resources: Path,
    output: Path,
    version: str,
) -> dict[str, object]:
    if not clap_module.is_file() or clap_module.stat().st_size == 0:
        raise ValueError("CLAP module must be a non-empty file")
    if not resources.is_dir():
        raise ValueError("resource directory must exist")
    output.parent.mkdir(parents=True, exist_ok=True)
    manifest = {
        "schemaVersion": 1,
        "product": "Project SEAM",
        "version": version,
        "buildClass": "UNSIGNED_DEVELOPMENT",
        "releaseEligible": False,
        "clapSha256": _sha256(clap_module),
    }
    notice = (
        "UNSIGNED DEVELOPMENT BUILD\n"
        "This package is not code-signed, notarized, validator-certified, or approved for release.\n"
    ).encode("utf-8")
    with zipfile.ZipFile(output, "w") as archive:
        _write_bytes(archive, "ProjectSEAM/UNSIGNED-DEVELOPMENT-BUILD.txt", notice)
        _write_bytes(
            archive,
            "ProjectSEAM/developer-package.json",
            (json.dumps(manifest, sort_keys=True, indent=2) + "\n").encode("utf-8"),
        )
        _write_bytes(archive, "ProjectSEAM/install.sh", INSTALL_SCRIPT, executable=True)
        _write_bytes(archive, "ProjectSEAM/uninstall.sh", UNINSTALL_SCRIPT, executable=True)
        for source_name in ("THIRD_PARTY_NOTICES.md", "SBOM.spdx.json"):
            source = ROOT / source_name
            if not source.is_file() or source.stat().st_size == 0:
                raise ValueError(f"required distribution notice is missing: {source}")
            _write_bytes(archive, f"ProjectSEAM/{source_name}", source.read_bytes())
        _write_bytes(
            archive,
            f"ProjectSEAM/CLAP/{clap_module.name}",
            clap_module.read_bytes(),
            executable=True,
        )
        for path in sorted(item for item in resources.rglob("*") if item.is_file()):
            relative = path.relative_to(resources).as_posix()
            _write_bytes(
                archive,
                f"ProjectSEAM/CLAP/ProjectSEAMEditor.resources/{relative}",
                path.read_bytes(),
            )
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Create deterministic unsigned Project SEAM developer package")
    parser.add_argument("--clap", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args(argv)
    try:
        manifest = create_developer_package(args.clap, args.resources, args.output, args.version)
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}")
        return 3
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
