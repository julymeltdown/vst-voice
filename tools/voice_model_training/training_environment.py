"""Capture a bounded content identity for a local model-training environment."""
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import platform
import re
import stat
import sys
import sysconfig


MAX_DISTRIBUTIONS = 4096
MAX_FILES = 200_000
MAX_TOTAL_BYTES = 16 * 1024**3
MAX_SINGLE_FILE_BYTES = 4 * 1024**3
CHUNK_BYTES = 1024 * 1024


def _canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":")).encode("utf-8")


def _hash_file(path: Path, *, maximum: int) -> tuple[str, int]:
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    descriptor = os.open(path, flags)
    try:
        before = os.fstat(descriptor)
        if not stat.S_ISREG(before.st_mode) or before.st_size < 0 or before.st_size > maximum:
            raise ValueError("Training environment contains an unsafe or oversized file")
        digest = hashlib.sha256()
        size = 0
        while True:
            block = os.read(descriptor, CHUNK_BYTES)
            if not block:
                break
            size += len(block)
            if size > maximum:
                raise ValueError("Training environment file changed beyond its size budget")
            digest.update(block)
        after = os.fstat(descriptor)
        if ((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) !=
                (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns) or size != before.st_size):
            raise ValueError("Training environment file changed during capture")
        return digest.hexdigest(), size
    finally:
        os.close(descriptor)


def capture_environment() -> dict:
    """Hash installed package contents plus the Python/OS runtime identity.

    Generated Python bytecode caches are omitted; their source files are included.
    Package files and loaded core numerical module origins must resolve inside the
    active environment prefix and be present in its hashed package inventory, so
    editable, system-site, and out-of-prefix imports fail closed instead of being
    represented by a weak version string. The returned record is suitable for
    canonical JSON manifests.
    """
    prefix = Path(sys.prefix).resolve(strict=True)
    paths = sysconfig.get_paths()
    package_roots = []
    for key in ("purelib", "platlib"):
        value = paths.get(key)
        if not isinstance(value, str) or not value:
            raise ValueError("Training environment lacks a package root")
        package_root = Path(value).resolve(strict=True)
        try:
            package_root.relative_to(prefix)
        except ValueError as error:
            raise ValueError("Training environment package roots are outside the active prefix") from error
        if package_root not in package_roots:
            package_roots.append(package_root)
    executable = Path(sys.executable).resolve(strict=True)
    executable_sha256, executable_bytes = _hash_file(executable, maximum=256 * 1024**2)
    # Restrict discovery to this environment's canonical package roots. Walking
    # every sys.path entry would also admit nested, vendored *.dist-info records
    # and packages from an accidentally enabled system-site-packages path.
    distributions = []
    seen_distributions = set()
    for package_root in package_roots:
        for distribution in importlib.metadata.distributions(path=[str(package_root)]):
            identity = (str(Path(distribution.locate_file("")).resolve(strict=True)),
                        distribution.metadata.get("Name"), distribution.version)
            if identity not in seen_distributions:
                seen_distributions.add(identity)
                distributions.append(distribution)
    if not 1 <= len(distributions) <= MAX_DISTRIBUTIONS:
        raise ValueError("Training environment distribution count is outside its bounds")

    packages = []
    captured_paths = set()
    total_files = 0
    total_bytes = 0
    for distribution in distributions:
        name = distribution.metadata.get("Name")
        version = distribution.version
        if (not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}", name)
                or not isinstance(version, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.+!_-]{0,127}", version)):
            raise ValueError("Training environment contains malformed distribution metadata")
        files = distribution.files
        if files is None or len(files) > MAX_FILES:
            raise ValueError("Training environment distribution lacks a bounded file inventory")
        entries = []
        distribution_bytes = 0
        seen = set()
        for item in files:
            # Resolve wheel-record paths (which can legitimately contain parent
            # segments for scripts), then bind the installed canonical location.
            try:
                candidate = Path(distribution.locate_file(item)).resolve(strict=True)
            except OSError as error:
                raise ValueError(f"Training environment file is missing or unreadable in {name}: {str(item)[:256]}") from error
            try:
                canonical_path = candidate.relative_to(prefix).as_posix()
            except ValueError as error:
                raise ValueError("Training package file resolves outside the active environment") from error
            if (not canonical_path or "__pycache__" in candidate.parts
                    or candidate.suffix in (".pyc", ".pyo")):
                continue
            if canonical_path in seen:
                continue
            seen.add(canonical_path)
            captured_paths.add(canonical_path)
            digest, size = _hash_file(candidate, maximum=MAX_SINGLE_FILE_BYTES)
            total_files += 1
            total_bytes += size
            distribution_bytes += size
            if total_files > MAX_FILES or total_bytes > MAX_TOTAL_BYTES:
                raise ValueError("Training environment file inventory exceeds its aggregate budget")
            entries.append([canonical_path, size, digest])
        if not entries:
            raise ValueError("Training distribution has no hashable installed files")
        entries.sort()
        packages.append(dict(name=name, version=version,
                             filesSha256=hashlib.sha256(_canonical(entries)).hexdigest(),
                             fileCount=len(entries), installedBytes=distribution_bytes))

    imported_modules = {}
    for module_name in ("numpy", "scipy", "torch", "onnxruntime"):
        module = sys.modules.get(module_name)
        if module is None:
            continue
        module_file = getattr(module, "__file__", None)
        if not isinstance(module_file, str) or not module_file:
            raise ValueError(f"Training numerical module has no captured origin: {module_name}")
        try:
            origin = Path(module_file).resolve(strict=True)
            relative_origin = origin.relative_to(prefix).as_posix()
        except (OSError, ValueError) as error:
            raise ValueError(f"Training numerical module resolves outside the active environment: {module_name}") from error
        if relative_origin not in captured_paths:
            raise ValueError(f"Training numerical module is absent from the hashed package inventory: {module_name}")
        imported_modules[module_name] = relative_origin

    packages.sort(key=lambda value: (value["name"].casefold(), value["version"]))
    body = dict(formatId="com.project-seam.training-environment", schemaVersion=1,
                runtime=dict(pythonVersion=sys.version, implementation=sys.implementation.name,
                             cacheTag=sys.implementation.cache_tag, platform=platform.platform(),
                             machine=platform.machine(), executableSha256=executable_sha256,
                             executableBytes=executable_bytes,
                             importedNumericalModules=imported_modules),
                packages=packages, distributionCount=len(packages), installedFileCount=total_files,
                installedBytes=total_bytes, bytecodePolicy="source-hashed-bytecode-cache-excluded")
    body["environmentSha256"] = hashlib.sha256(_canonical(body)).hexdigest()
    return body
