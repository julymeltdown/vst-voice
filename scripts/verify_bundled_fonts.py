#!/usr/bin/env python3
"""Verifies the bundled UI fonts in assets/fonts against their manifest.

The manifest pins every face by SHA-256 and size, names its PostScript name, the font roles it
serves and its license file, and records where it was downloaded from. The gate fails when a face
or license file differs from the manifest, a file in assets/fonts is not listed, a license is not
the SIL Open Font License 1.1, or a face's PostScript name does not match its name table. The
runtime (canvas2d_coregraphics.mm) checks the same hashes before it registers a face and falls back
to system faces for any face that does not match.

Usage: verify_bundled_fonts.py [--root DIR] [--write]
  --write  recompute sizes and hashes into the manifest (after a deliberate font update).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROLES = {"Ui", "UiMedium", "UiSemibold", "UiBold", "Mono", "Display", "DisplayRounded"}
LICENSE_MARKER = "SIL Open Font License, Version 1.1"
MAX_FACE_BYTES = 4 * 1024 * 1024


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def postscript_name(path: Path) -> str | None:
    """Name ID 6 from the font's name table (Windows Unicode or Macintosh Roman record)."""
    data = path.read_bytes()
    if len(data) < 12:
        return None
    tables = struct.unpack(">H", data[4:6])[0]
    for i in range(tables):
        tag, _, offset, _ = struct.unpack(">4sIII", data[12 + 16 * i:28 + 16 * i])
        if tag != b"name":
            continue
        _, count, strings = struct.unpack(">HHH", data[offset:offset + 6])
        mac = None
        for j in range(count):
            record = data[offset + 6 + 12 * j:offset + 18 + 12 * j]
            platform, _, language, name_id, length, start = struct.unpack(">HHHHHH", record)
            if name_id != 6:
                continue
            raw = data[offset + strings + start:offset + strings + start + length]
            if platform == 3 and language == 0x409:
                return raw.decode("utf-16-be")
            if platform == 1:
                mac = raw.decode("latin-1")
        return mac
    return None


def verify(root: Path, write: bool = False) -> list[str]:
    fonts = root / "assets" / "fonts"
    manifest_path = fonts / "manifest.json"
    errors: list[str] = []
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return [f"{manifest_path}: {error}"]
    if manifest.get("schemaVersion") != 1:
        errors.append("manifest: schemaVersion must be 1")
    faces = manifest.get("faces")
    if not isinstance(faces, list) or not faces:
        return errors + ["manifest: faces must be a non-empty list"]
    listed: set[str] = {"manifest.json", "README.md"}
    served: set[str] = set()
    for face in faces:
        name = face.get("file", "?")
        path = fonts / name
        license_path = fonts / face.get("licenseFile", "")
        listed.update({name, face.get("licenseFile", "")})
        if not path.is_file() or path.resolve().parent.parent != fonts.resolve():
            errors.append(f"{name}: missing, or not one directory below assets/fonts")
            continue
        if path.stat().st_size > MAX_FACE_BYTES:
            errors.append(f"{name}: larger than {MAX_FACE_BYTES} bytes")
        if write:
            face["bytes"] = path.stat().st_size
            face["sha256"] = sha256(path)
            face["licenseSha256"] = sha256(license_path) if license_path.is_file() else ""
        if face.get("bytes") != path.stat().st_size or face.get("sha256") != sha256(path):
            errors.append(f"{name}: size or SHA-256 differs from the manifest")
        if postscript_name(path) != face.get("postScriptName"):
            errors.append(f"{name}: PostScript name is {postscript_name(path)!r}, "
                          f"manifest says {face.get('postScriptName')!r}")
        if face.get("license") != "OFL-1.1":
            errors.append(f"{name}: license must be OFL-1.1")
        if not license_path.is_file():
            errors.append(f"{name}: license file {face.get('licenseFile')!r} is missing")
        else:
            if LICENSE_MARKER not in license_path.read_text(encoding="utf-8", errors="replace"):
                errors.append(f"{face['licenseFile']}: not the SIL Open Font License 1.1")
            if face.get("licenseSha256") != sha256(license_path):
                errors.append(f"{face['licenseFile']}: SHA-256 differs from the manifest")
        roles = face.get("roles", [])
        if not roles or not set(roles) <= ROLES:
            errors.append(f"{name}: roles must be drawn from {sorted(ROLES)}")
        for role in roles:
            if role in served:
                errors.append(f"{name}: role {role} is served by two faces")
            served.add(role)
        variation = face.get("variation", {})
        if not isinstance(variation, dict) or not all(
                isinstance(k, str) and len(k) == 4 and isinstance(v, (int, float))
                for k, v in variation.items()):
            errors.append(f"{name}: variation must map four-letter axis tags to numbers")
        if not str(face.get("source", "")).startswith("https://github.com/google/fonts/blob/"):
            errors.append(f"{name}: source must be the pinned Google Fonts repository path")
    if served != ROLES:
        errors.append(f"manifest: roles without a face: {sorted(ROLES - served)}")
    for path in fonts.rglob("*"):
        if path.is_file() and path.relative_to(fonts).as_posix() not in listed:
            errors.append(f"{path.relative_to(fonts).as_posix()}: not listed in the manifest")
    if write and not errors:
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args(argv)
    errors = verify(args.root.resolve(), args.write)
    for error in errors:
        print(f"verify_bundled_fonts: {error}", file=sys.stderr)
    if not errors:
        print("verify_bundled_fonts: every bundled face matches its manifest")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())

