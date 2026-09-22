"""Explicit source-only WORLD fetch/verification for an offline developer experiment.

No automatic build-time download, dependency installation, assets, or model intake.
The committed lock is the source authority; an interrupted fetch leaves a partial
directory that cannot verify. A new fetch never overwrites an existing directory.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import urllib.request

LOCK_PATH = Path(__file__).with_name("world_reference.lock.json")
REVISION = "f8dd5fb289db6a7f7f704497752bf32b258f9151"
MAX_SOURCE_BYTES = 256 * 1024


def load_lock() -> tuple[dict, str]:
    raw = LOCK_PATH.read_bytes()
    if len(raw) > 64 * 1024:
        raise ValueError("WORLD reference lock exceeds limit")
    lock = json.loads(raw)
    if (lock.get("schemaVersion") != 1 or lock.get("revision") != REVISION
            or lock.get("repository") != "https://github.com/mmorise/World"
            or len(lock.get("files", [])) != 25):
        raise ValueError("Unexpected WORLD reference authority")
    names = set()
    for item in lock["files"]:
        name = item["path"]
        path = PurePosixPath(name)
        if (path.is_absolute() or ".." in path.parts or "\\" in name or name in names
                or (name != "LICENSE.txt" and not
                    (name.startswith("src/") and path.suffix in (".cpp", ".h")))
                or not re.fullmatch(r"[a-f0-9]{40}", item["gitBlobSha1"])
                or type(item["size"]) is not int or not 0 < item["size"] <= MAX_SOURCE_BYTES):
            raise ValueError("Invalid WORLD source entry")
        names.add(name)
    if "LICENSE.txt" not in names:
        raise ValueError("WORLD copyright/license notice is mandatory")
    return lock, hashlib.sha256(raw).hexdigest()


def validate_bytes(raw: bytes, item: dict) -> str:
    if len(raw) != item["size"]:
        raise ValueError("WORLD source size mismatch: " + item["path"])
    identity = hashlib.sha1(b"blob " + str(len(raw)).encode("ascii") + b"\0" + raw).hexdigest()
    if identity != item["gitBlobSha1"]:
        raise ValueError("WORLD Git blob mismatch: " + item["path"])
    digest = hashlib.sha256(raw).hexdigest()
    if "sha256" in item and digest != item["sha256"]:
        raise ValueError("WORLD SHA-256 mismatch: " + item["path"])
    return digest


def expected_files(lock: dict, variant: str) -> list[dict]:
    if variant == "upstream":
        return lock["files"]
    if variant != "openutau-d4c-guard":
        raise ValueError("Unknown reference variant")
    guard = lock["guardVariant"]
    if guard["id"] != variant or guard["d4c"]["path"] != "src/d4c.cpp":
        raise ValueError("Invalid explicit guard authority")
    return [guard["d4c"] if item["path"] == "src/d4c.cpp" else item for item in lock["files"]] + [
        guard["fullPatch"], guard["guardHunk"], guard["license"]]


def verify_inventory(directory: Path, items: list[dict]) -> None:
    """Reject unpinned include-shadowing files without traversing unknown trees."""
    files = {item["path"] for item in items}
    directories = {str(parent) for name in files for parent in PurePosixPath(name).parents}
    pending = [PurePosixPath(".")]
    while pending:
        relative = pending.pop()
        with os.scandir(directory / relative) as entries:
            for entry in entries:
                name = relative / entry.name
                if entry.is_symlink():
                    raise ValueError("WORLD source symlinks are not admitted")
                if str(name) in directories:
                    if not entry.is_dir(follow_symlinks=False):
                        raise ValueError("WORLD source parent must be a directory: " + str(name))
                    pending.append(name)
                elif str(name) in files:
                    if not entry.is_file(follow_symlinks=False):
                        raise ValueError("WORLD source must be a regular file: " + str(name))
                else:
                    raise ValueError("Unlisted WORLD source entry: " + str(name))


def verify(directory: Path, variant: str = "upstream") -> dict:
    lock, lock_hash = load_lock()
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError("WORLD source must be a real directory")
    items = expected_files(lock, variant)
    verify_inventory(directory, items)
    files = []
    for item in items:
        path = directory / item["path"]
        for depth in range(1, len(PurePosixPath(item["path"]).parts) + 1):
            component = directory.joinpath(*PurePosixPath(item["path"]).parts[:depth])
            if component.is_symlink():
                raise ValueError("WORLD source symlinks are not admitted")
        if not path.is_file():
            raise ValueError("WORLD source is missing: " + item["path"])
        with path.open("rb") as handle:
            raw = handle.read(MAX_SOURCE_BYTES + 1)
        files.append({"path": item["path"], "sha256": validate_bytes(raw, item), "size": len(raw)})
    canonical = json.dumps(files, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return {"revision": REVISION, "variant": variant, "lockSha256": lock_hash,
            "sourceManifestSha256": hashlib.sha256(canonical).hexdigest(), "files": files}


def fetch(directory: Path) -> dict:
    lock, _ = load_lock()
    directory.mkdir(parents=False, exist_ok=False)
    for item in lock["files"]:
        url = f"https://raw.githubusercontent.com/mmorise/World/{REVISION}/{item['path']}"
        with urllib.request.urlopen(url, timeout=30) as response:
            raw = response.read(MAX_SOURCE_BYTES + 1)
        validate_bytes(raw, item)
        target = directory / item["path"]
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("xb") as output:
            output.write(raw)
    return verify(directory)


def prepare_guard(upstream: Path, directory: Path, patch_path: Path, license_path: Path) -> dict:
    """Explicit opt-in derivation in a NEW directory; original source is untouched."""
    verify(upstream)
    lock, _ = load_lock()
    guard = lock["guardVariant"]
    with patch_path.open("rb") as handle:
        patch = handle.read(MAX_SOURCE_BYTES + 1)
    with license_path.open("rb") as handle:
        notice = handle.read(MAX_SOURCE_BYTES + 1)
    validate_bytes(patch, guard["fullPatch"])
    validate_bytes(notice, guard["license"])
    hunk = patch.split(b"diff --git a/src/synthesis.cpp", 1)[0]
    validate_bytes(hunk, guard["guardHunk"])
    body = hunk.split(b"@@", 2)[2].split(b"\n", 1)[1].splitlines(keepends=True)
    old = b"".join(line[1:] for line in body if line[:1] in (b" ", b"-"))
    new = b"".join(line[1:] for line in body if line[:1] in (b" ", b"+"))
    directory.mkdir(parents=False, exist_ok=False)
    for item in lock["files"]:
        with (upstream / item["path"]).open("rb") as handle:
            raw = handle.read(MAX_SOURCE_BYTES + 1)
        validate_bytes(raw, item)
        if item["path"] == "src/d4c.cpp":
            if raw.count(old) != 1:
                raise ValueError("Pinned guard context does not match exactly once")
            raw = raw.replace(old, new, 1)
            validate_bytes(raw, guard["d4c"])
        target = directory / item["path"]
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("xb") as handle:
            handle.write(raw)
    for key, raw in (("fullPatch", patch), ("guardHunk", hunk), ("license", notice)):
        with (directory / guard[key]["path"]).open("xb") as handle:
            handle.write(raw)
    return verify(directory, "openutau-d4c-guard")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("fetch", "verify", "guard"))
    parser.add_argument("directory", type=Path)
    parser.add_argument("--variant", choices=("upstream", "openutau-d4c-guard"), default="upstream")
    parser.add_argument("--upstream", type=Path)
    parser.add_argument("--openutau-patch", type=Path)
    parser.add_argument("--openutau-license", type=Path)
    args = parser.parse_args()
    try:
        if args.mode == "guard":
            if not all((args.upstream, args.openutau_patch, args.openutau_license)):
                parser.error("guard requires explicit --upstream, --openutau-patch and --openutau-license")
            result = prepare_guard(args.upstream, args.directory, args.openutau_patch, args.openutau_license)
        elif args.mode == "fetch":
            if args.variant != "upstream":
                parser.error("fetch only prepares the unmodified upstream variant")
            result = fetch(args.directory)
        else:
            result = verify(args.directory, args.variant)
    except (OSError, ValueError) as error:
        parser.exit(2, f"WORLD_REFERENCE=ERROR: {error}\n")
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
