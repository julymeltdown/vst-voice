#!/usr/bin/env python3
"""Verify the bounded, shipped EMO/SCENE UI asset set.

This checks exact runtime paths, manifest metadata, PNG integrity and decoded
alpha. It cannot establish art ownership, visual brand clearance, or the hashes
of generator originals that are not bundled with this repository.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import zlib


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ASSETS = ROOT / "assets/ui-design"
MODES = ("emo", "scene")
ROLES = ("portrait", "stage", "wordmark")
MAX_ENCODED = 8 * 1024 * 1024  # Same limit as paint::ImageLimits.
MAX_PIXELS = 4096 * 4096
MAX_MANIFEST = 64 * 1024
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _png_alpha_range(encoded: bytes) -> tuple[tuple[int, int], tuple[int, int]]:
    if not encoded.startswith(PNG_SIGNATURE):
        raise ValueError("PNG signature is missing")
    offset = len(PNG_SIGNATURE)
    size: tuple[int, int] | None = None
    compressed = bytearray()
    saw_end = False
    saw_image_data = False
    ended_image_data = False
    while offset < len(encoded):
        if len(encoded) - offset < 12:
            raise ValueError("truncated PNG chunk")
        length = struct.unpack_from(">I", encoded, offset)[0]
        end = offset + 12 + length
        if length > MAX_ENCODED or end > len(encoded):
            raise ValueError("oversized or truncated PNG chunk")
        kind = encoded[offset + 4:offset + 8]
        payload = encoded[offset + 8:offset + 8 + length]
        crc = struct.unpack_from(">I", encoded, offset + 8 + length)[0]
        if zlib.crc32(kind + payload) != crc:
            raise ValueError("PNG chunk CRC mismatch")
        if size is None and kind != b"IHDR":
            raise ValueError("IHDR must be first")
        if kind == b"IHDR":
            if size is not None or length != 13:
                raise ValueError("invalid or duplicate IHDR")
            width, height, depth, color, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
            if (not width or not height or width * height > MAX_PIXELS
                    or (depth, color, compression, filtering, interlace) != (8, 6, 0, 0, 0)):
                raise ValueError("expected bounded 8-bit noninterlaced RGBA PNG")
            size = width, height
        elif kind == b"IDAT":
            if ended_image_data:
                raise ValueError("IDAT chunks are not consecutive")
            compressed.extend(payload)
            saw_image_data = True
        elif kind == b"IEND":
            if length or not saw_image_data or end != len(encoded):
                raise ValueError("invalid IEND or trailing data")
            saw_end = True
            break
        else:
            if saw_image_data:
                ended_image_data = True
            if kind[0] & 0x20 == 0:  # Reject unknown critical chunks.
                raise ValueError("unsupported critical PNG chunk")
        offset = end
    if size is None or not saw_end:
        raise ValueError("incomplete PNG")

    width, height = size
    stride = width * 4
    expected = height * (stride + 1)
    decoder = zlib.decompressobj()
    raw = decoder.decompress(bytes(compressed), expected + 1)
    if len(raw) != expected or decoder.unconsumed_tail or not decoder.eof or decoder.unused_data:
        raise ValueError("invalid or oversized PNG pixel stream")
    previous = bytearray(stride)
    minimum, maximum = 255, 0
    for y in range(height):
        row_start = y * (stride + 1)
        filter_type = raw[row_start]
        if filter_type > 4:
            raise ValueError("invalid PNG row filter")
        row = bytearray(raw[row_start + 1:row_start + 1 + stride])
        for i in range(stride):
            left = row[i - 4] if i >= 4 else 0
            above = previous[i]
            upper_left = previous[i - 4] if i >= 4 else 0
            if filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = above
            elif filter_type == 3:
                predictor = (left + above) // 2
            elif filter_type == 4:
                estimate = left + above - upper_left
                distances = (abs(estimate - left), abs(estimate - above),
                             abs(estimate - upper_left))
                predictor = (left, above, upper_left)[distances.index(min(distances))]
            else:
                predictor = 0
            row[i] = (row[i] + predictor) & 0xff
        alpha = row[3::4]
        minimum = min(minimum, min(alpha))
        maximum = max(maximum, max(alpha))
        previous = row
    return size, (minimum, maximum)


def verify(asset_root: Path = DEFAULT_ASSETS) -> list[str]:
    errors: list[str] = []
    manifest_path = asset_root / "manifest.json"
    try:
        if manifest_path.is_symlink():
            return ["manifest.json must not be a symlink"]
        if manifest_path.stat().st_size > MAX_MANIFEST:
            return ["manifest.json exceeds 64 KiB"]
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        return [f"manifest.json cannot be read: {exc}"]
    if not isinstance(manifest, dict) or manifest.get("schemaVersion") != 1:
        return ["manifest schemaVersion must be 1"]
    if set(manifest) != {"schemaVersion", "developmentOnly", "assets"}:
        errors.append("manifest has undeclared top-level fields")
    if manifest.get("developmentOnly") is not True:
        errors.append("developmentOnly must remain true until an independently cleared release")
    entries = manifest.get("assets")
    if not isinstance(entries, list) or len(entries) != len(MODES) * len(ROLES):
        return errors + ["manifest must list exactly six mode/role assets"]
    expected_paths = {f"{mode}/{role}.png" for mode in MODES for role in ROLES}
    seen: set[str] = set()
    for entry in entries:
        if not isinstance(entry, dict):
            errors.append("asset entry must be an object")
            continue
        if set(entry) != {"path", "role", "size", "sha256", "sourceSha256",
                          "alpha", "colorSpace"}:
            errors.append("asset entry has undeclared metadata fields")
        relative = entry.get("path")
        if not isinstance(relative, str) or relative not in expected_paths:
            errors.append(f"unexpected runtime asset path: {relative!r}")
            continue
        if relative in seen:
            errors.append(f"duplicate runtime asset path: {relative}")
            continue
        seen.add(relative)
        role = relative.split("/")[1][:-4]
        if entry.get("role") != role:
            errors.append(f"role mismatch: {relative}")
        if entry.get("alpha") != "premultiplied-on-load":
            errors.append(f"alpha loading contract mismatch: {relative}")
        if entry.get("colorSpace") != "sRGB":
            errors.append(f"color space contract mismatch: {relative}")
        for key in ("sha256", "sourceSha256"):
            if not isinstance(entry.get(key), str) or not SHA256.fullmatch(entry[key]):
                errors.append(f"invalid {key}: {relative}")
        path = asset_root / relative
        try:
            if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_ENCODED:
                raise ValueError("missing, linked, or larger than the runtime's 8 MiB limit")
            encoded = path.read_bytes()
            size, alpha = _png_alpha_range(encoded)
        except (OSError, ValueError, zlib.error) as exc:
            errors.append(f"invalid PNG {relative}: {exc}")
            continue
        if hashlib.sha256(encoded).hexdigest() != entry.get("sha256"):
            errors.append(f"file SHA-256 mismatch: {relative}")
        declared_size = entry.get("size")
        if (not isinstance(declared_size, list) or len(declared_size) != 2
                or any(type(x) is not int or x <= 0 for x in declared_size)
                or size != tuple(declared_size)):
            errors.append(f"dimension mismatch: {relative}")
        if role in ("stage", "wordmark") and alpha != (0, 255):
            errors.append(f"{role} must contain fully transparent and opaque pixels: {relative}")
        if alpha[1] == 0:
            errors.append(f"fully transparent asset: {relative}")
    if seen != expected_paths:
        errors.append(f"mode/role parity mismatch: missing {sorted(expected_paths - seen)}")
    packaged = {p.relative_to(asset_root).as_posix()
                for p in asset_root.rglob("*") if p.is_file() or p.is_symlink()}
    expected_package = expected_paths | {"manifest.json", "PROVENANCE.md"}
    if packaged != expected_package:
        errors.append("unmanifested or missing packaged files: "
                      + repr(sorted(packaged ^ expected_package)))
    provenance = asset_root / "PROVENANCE.md"
    try:
        if provenance.is_symlink():
            raise ValueError("must not be a symlink")
        provenance_text = provenance.read_text(encoding="utf-8")
    except (OSError, UnicodeError, ValueError) as exc:
        errors.append(f"PROVENANCE.md cannot be read: {exc}")
    else:
        for relative in expected_paths:
            if f"`{relative}`" not in provenance_text:
                errors.append(f"provenance omits runtime asset: {relative}")
        if "development only" not in provenance_text.lower():
            errors.append("provenance omits development-only status")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset-root", type=Path, default=DEFAULT_ASSETS)
    args = parser.parse_args()
    errors = verify(args.asset_root)
    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print("PASS: six EMO/SCENE PNGs, manifest, alpha, hashes and provenance references")
    print("NOT_VERIFIED: generator originals, art rights, visual brand clearance")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
