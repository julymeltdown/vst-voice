from __future__ import annotations

import hashlib
import json
from pathlib import Path
import zipfile

from common import safe_relative_path, sha256_file

_FIXED_TIME = (1980, 1, 1, 0, 0, 0)


def _write_bytes(archive: zipfile.ZipFile, name: str, payload: bytes) -> None:
    if not safe_relative_path(name):
        raise ValueError(f"unsafe candidate path: {name}")
    info = zipfile.ZipInfo(name, _FIXED_TIME)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, payload)


def build_candidate(*, output: Path, component_files: dict[str, Path], release_result: dict, product_version: str) -> dict:
    output = Path(output)
    entries: list[dict] = []
    payloads: dict[str, bytes] = {}
    for name, source in sorted(component_files.items()):
        if not safe_relative_path(name):
            raise ValueError(f"unsafe component path: {name}")
        source = Path(source)
        if source.is_symlink() or not source.is_file():
            raise ValueError(f"component file is missing or unsafe: {source}")
        payload = source.read_bytes()
        payloads[name] = payload
        entries.append({"path": name, "size": len(payload), "sha256": hashlib.sha256(payload).hexdigest()})
    manifest = {
        "schemaVersion": 1,
        "productVersion": product_version,
        "releaseEligible": bool(release_result.get("passed")),
        "blockedTargets": sorted(release_result.get("blockedTargets", [])),
        "entries": entries,
    }
    manifest_bytes = (json.dumps(manifest, sort_keys=True, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8")
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w") as archive:
        for name in sorted(payloads):
            _write_bytes(archive, name, payloads[name])
        _write_bytes(archive, "candidate-manifest.json", manifest_bytes)
        if not manifest["releaseEligible"]:
            _write_bytes(archive, "DEVELOPMENT_ONLY.txt", b"This candidate is not release eligible.\n")
    return {"path": str(output), "sha256": sha256_file(output), **manifest}
