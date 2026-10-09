from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True, slots=True)
class OpenSslBuildPlan:
    configure: tuple[str, ...]
    build: tuple[str, ...]
    install: tuple[str, ...]
    source_commit: str
    receipt: Path
    archive: Path


def openssl_build_plan(
    host_system: str,
    architecture: str,
    source: Path,
    prefix: Path,
    source_commit: str,
) -> OpenSslBuildPlan:
    system = host_system.casefold()
    machine = architecture.casefold()
    if system == "darwin":
        target = {
            "arm64": "darwin64-arm64-cc",
            "aarch64": "darwin64-arm64-cc",
            "x86_64": "darwin64-x86_64-cc",
        }.get(machine)
        tool = "make"
        archive = prefix / "lib" / "libcrypto.a"
    elif system == "windows":
        target = {
            "amd64": "VC-WIN64A",
            "x64": "VC-WIN64A",
            "x86_64": "VC-WIN64A",
        }.get(machine)
        tool = "nmake"
        archive = prefix / "lib" / "libcrypto.lib"
    elif system == "linux":
        target = {
            "amd64": "linux-x86_64",
            "x64": "linux-x86_64",
            "x86_64": "linux-x86_64",
            "arm64": "linux-aarch64",
            "aarch64": "linux-aarch64",
        }.get(machine)
        tool = "make"
        archive = prefix / "lib" / "libcrypto.a"
    else:
        target = None
        tool = ""
        archive = prefix / "lib" / "libcrypto.a"

    if target is None:
        raise ValueError(
            f"unsupported OpenSSL build target: {host_system}/{architecture}"
        )

    configure = (
        "perl",
        str(source / "Configure"),
        target,
        *(("/MT",) if system == "windows" else ()),
        "no-shared",
        "no-module",
        "no-tests",
        "no-apps",
        "no-docs",
        "no-legacy",
        f"--prefix={prefix}",
        "--libdir=lib",
    )
    return OpenSslBuildPlan(
        configure=configure,
        build=(tool, "build_sw"),
        install=(tool, "install_dev"),
        source_commit=source_commit,
        receipt=prefix / "seam-static-openssl-receipt.json",
        archive=archive,
    )


def _archive_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _receipt(plan: OpenSslBuildPlan) -> dict[str, object]:
    return {
        "schemaVersion": 1,
        "sourceCommit": plan.source_commit,
        "configure": list(plan.configure),
        "build": list(plan.build),
        "install": list(plan.install),
        "archiveSha256": _archive_sha256(plan.archive),
    }


def build_static_openssl(plan: OpenSslBuildPlan, build_directory: Path) -> Path:
    if plan.archive.is_file():
        try:
            stored = json.loads(plan.receipt.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError):
            stored = None
        if stored == _receipt(plan):
            return plan.archive
    if build_directory.exists():
        shutil.rmtree(build_directory)
    install_prefix = plan.receipt.parent
    if install_prefix.exists():
        shutil.rmtree(install_prefix)
    build_directory.mkdir(parents=True, exist_ok=True)
    for command in (plan.configure, plan.build, plan.install):
        print("+", " ".join(command))
        subprocess.run(command, cwd=build_directory, check=True)
    if not plan.archive.is_file():
        raise RuntimeError(
            f"OpenSSL static Crypto archive was not installed: {plan.archive}"
        )
    plan.receipt.write_text(
        json.dumps(_receipt(plan), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return plan.archive


def prepare_static_openssl(
    host_system: str,
    architecture: str,
    source: Path,
    build_root: Path,
    source_commit: str,
) -> Path:
    prefix = build_root / "static-openssl-install"
    plan = openssl_build_plan(
        host_system, architecture, source, prefix, source_commit
    )
    build_static_openssl(plan, build_root / "static-openssl-build")
    return prefix
