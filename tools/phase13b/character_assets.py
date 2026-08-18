from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from PIL import Image, ImageChops, ImageOps

TOOL_VERSION = "phase13b-character-assets-v1"


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _save_png(image: Image.Image, path: Path) -> None:
    image.save(path, format="PNG", optimize=False, compress_level=9)


def _fit(image: Image.Image, size: tuple[int, int], background=(18, 16, 22)) -> Image.Image:
    canvas = Image.new("RGB", size, background)
    copy = image.convert("RGB")
    copy.thumbnail(size, Image.Resampling.LANCZOS)
    x = (size[0] - copy.width) // 2
    y = (size[1] - copy.height) // 2
    canvas.paste(copy, (x, y))
    return canvas


def _silhouette(image: Image.Image, size: tuple[int, int]) -> Image.Image:
    fitted = _fit(image, size, background=(255, 255, 255))
    background = Image.new("RGB", size, fitted.getpixel((0, 0)))
    diff = ImageChops.difference(fitted, background).convert("L")
    mask = diff.point(lambda value: 255 if value > 18 else 0, mode="1")
    return ImageOps.invert(mask.convert("L"))


def _palette(image: Image.Image, colors: int = 8) -> list[str]:
    reduced = image.convert("RGB").resize((128, 128), Image.Resampling.BILINEAR)
    quantized = reduced.quantize(colors=colors, method=Image.Quantize.MEDIANCUT)
    palette = quantized.getpalette() or []
    counts = sorted(quantized.getcolors() or [], reverse=True)
    values: list[str] = []
    for _, index in counts:
        offset = index * 3
        if offset + 2 >= len(palette):
            continue
        rgb = tuple(palette[offset : offset + 3])
        value = "#{:02X}{:02X}{:02X}".format(*rgb)
        if value not in values:
            values.append(value)
    return values[:colors]


def generate_character_assets(source: Path, output: Path) -> dict[str, Any]:
    source = Path(source)
    output = Path(output)
    if source.is_symlink() or not source.is_file() or source.stat().st_size <= 0:
        raise ValueError("source character image must be a non-empty regular file")
    output.mkdir(parents=True, exist_ok=True)
    with Image.open(source) as opened:
        image = opened.convert("RGB")
    assets = {
        "key-art-1024.png": _fit(image, (1024, 1024)),
        "portrait-512.png": _fit(image, (512, 768)),
        "thumbnail-256.png": _fit(image, (256, 256)),
        "silhouette-256.png": _silhouette(image, (256, 256)),
    }
    for name, rendered in assets.items():
        _save_png(rendered, output / name)
    palette_payload = {
        "schemaVersion": 1,
        "characterId": "official.character.01",
        "developmentOnly": True,
        "colors": _palette(image),
    }
    (output / "palette.json").write_text(
        json.dumps(palette_payload, ensure_ascii=False, sort_keys=True, indent=2) + "\n",
        encoding="utf-8",
    )
    asset_hashes = {
        name: _sha256(output / name)
        for name in sorted([*assets.keys(), "palette.json"])
    }
    manifest = {
        "schemaVersion": 1,
        "characterId": "official.character.01",
        "sourceSha256": _sha256(source),
        "toolVersion": TOOL_VERSION,
        "developmentOnly": True,
        "productionStatus": "NOT_A_PRODUCTION_TURNAROUND",
        "assetSha256": asset_hashes,
    }
    (output / "asset-manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, sort_keys=True, indent=2) + "\n",
        encoding="utf-8",
    )
    return manifest
