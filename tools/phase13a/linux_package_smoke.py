#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import shutil
import stat
import subprocess
import zipfile
from pathlib import Path, PurePosixPath

MAX_ENTRIES = 4096
MAX_UNCOMPRESSED_BYTES = 512 * 1024 * 1024


def _safe_extract(package: Path, destination: Path) -> None:
    with zipfile.ZipFile(package) as archive:
        infos = archive.infolist()
        if len(infos) > MAX_ENTRIES:
            raise ValueError("developer package contains too many entries")
        total = 0
        root = destination.resolve()
        for info in infos:
            name = PurePosixPath(info.filename)
            if name.is_absolute() or any(part in {"", ".", ".."} for part in name.parts):
                raise ValueError(f"unsafe archive path: {info.filename}")
            mode = (info.external_attr >> 16) & 0xFFFF
            if stat.S_ISLNK(mode):
                raise ValueError(f"symbolic links are forbidden: {info.filename}")
            total += info.file_size
            if total > MAX_UNCOMPRESSED_BYTES:
                raise ValueError("developer package exceeds uncompressed size limit")
            target = (root / Path(*name.parts)).resolve()
            try:
                target.relative_to(root)
            except ValueError as exc:
                raise ValueError(f"archive path escapes destination: {info.filename}") from exc
        archive.extractall(destination)
    for script in destination.rglob("*.sh"):
        script.chmod(script.stat().st_mode | stat.S_IXUSR)


def run_smoke(package: Path, sandbox: Path) -> dict[str, object]:
    if not package.is_file() or package.stat().st_size == 0:
        raise ValueError("developer package must be a non-empty ZIP file")
    if sandbox.exists():
        shutil.rmtree(sandbox)
    extraction = sandbox / "extracted"
    home = sandbox / "home"
    data_root = sandbox / "data" / "ProjectSEAM"
    clap_root = sandbox / "clap"
    extraction.mkdir(parents=True)
    home.mkdir(parents=True)
    _safe_extract(package, extraction)
    payload = extraction / "ProjectSEAM"
    install = payload / "install.sh"
    uninstall = payload / "uninstall.sh"
    if not install.is_file() or not uninstall.is_file():
        raise ValueError("developer package is missing install or uninstall script")
    env = os.environ.copy()
    env.update({
        "HOME": str(home),
        "XDG_DATA_HOME": str(sandbox / "data"),
        "SEAM_INSTALL_ROOT": str(data_root),
        "SEAM_CLAP_ROOT": str(clap_root),
    })
    subprocess.run(["bash", str(install)], cwd=payload, env=env, check=True)
    installed = (clap_root / "ProjectSEAMEditor.clap").is_file()
    resources = (data_root / "ProjectSEAMEditor.resources").is_dir()
    subprocess.run(["bash", str(uninstall)], cwd=payload, env=env, check=True)
    uninstalled = not (clap_root / "ProjectSEAMEditor.clap").exists() and not data_root.exists()
    status = "PASS" if installed and resources and uninstalled else "FAIL"
    return {
        "schemaVersion": 1,
        "status": status,
        "installedClap": installed,
        "installedResources": resources,
        "uninstalledClap": uninstalled,
        "sandbox": str(sandbox),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run Project SEAM Linux developer-package clean install smoke")
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--sandbox", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        result = run_smoke(args.package, args.sandbox)
    except (OSError, ValueError, zipfile.BadZipFile, subprocess.CalledProcessError) as exc:
        result = {"schemaVersion": 1, "status": "FAIL", "error": str(exc)}
    text = json.dumps(result, indent=2) + "\n"
    print(text, end="")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    return 0 if result.get("status") == "PASS" else 3


if __name__ == "__main__":
    raise SystemExit(main())
