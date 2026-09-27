#!/usr/bin/env python3
"""Build Character 01's per-state runtime art and the EMO/SCENE splash key art.

Two steps, both deterministic for a given Pillow/NumPy version:

  import  Copies image-generator output into the committed masters: six state portraits
          resized to 640x960 under assets/character-01/source/states/, and two splash images
          resized/cropped to 1600x1000 RGBA under assets/ui-design/<mode>/splash.png. The
          generator output's SHA-256 is recorded in source/states/sources.json and, for the
          splash, as the ui-design manifest's sourceSha256.
  build   Derives everything the runtime loads from the committed masters: the six 320x480
          P6 state portraits, six 24x24 mouth sprites cut from the singing (focused) face so they
          sit exactly on it, the manifest's mouthPlacement, and a PNG contact sheet for review.
          A placed sprite's flat corner colour is transparent at runtime, so every sprite pixel
          the redrawn lips leave unchanged is written as that key colour.

Usage:
  build_character_state_art.py import --state neutral=/path/a.png ... --splash emo=/path/e.png ...
  build_character_state_art.py build

The masters are AI-generated development art made from the owner's reference sheet; see
assets/character-01/PROVENANCE.md. Nothing here decides whether the art is releasable.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
CHARACTER = ROOT / "assets/character-01"
MASTERS = CHARACTER / "source/states"
RUNTIME = CHARACTER / "runtime"
PREVIEWS = CHARACTER / "previews"
UI_DESIGN = ROOT / "assets/ui-design"

STATES = ("neutral", "focused", "rendering", "complete", "warning", "error")
MOUTHS = ("closed", "narrow", "nasal", "open", "wide", "round")
MODES = ("emo", "scene")

RUNTIME_SIZE = (320, 480)  # The standing-frame convention character_surface.cpp crops against.
MASTER_SIZE = (640, 960)  # 2x the runtime frame.
SPLASH_SIZE = (1600, 1000)  # Redesign plan section 8.2.

# The singing mouth in runtime pixels, measured on the focused master: the sprite box the
# manifest's normalized mouthPlacement declares, and the lips' centre and half extents in it.
MOUTH_BOX = (148, 90, 24, 24)
MOUTH_CENTER = (160.0, 101.5)
MOUTH_ERASE_RADII = (10.8, 9.0)
MOUTH_SCALE = 1.15  # The drawn shapes relative to their base half extents below.
SUPERSAMPLE = 4  # Drawing scale over the master for the mouth shapes.
# The sprite key: no pixel of the monochrome ink face is pure magenta.
MOUTH_KEY = (255, 0, 255)

# PPM payload bytes that the header's single trailing whitespace makes ambiguous.
PPM_WHITESPACE = {9, 10, 11, 12, 13, 32}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_ppm(image: Image.Image, path: Path) -> None:
    rgb = np.asarray(image.convert("RGB"), dtype=np.uint8).copy()
    # A first sample equal to a whitespace byte would be read as part of the header.
    if int(rgb[0, 0, 0]) in PPM_WHITESPACE:
        rgb[0, 0, 0] += 1
    height, width = rgb.shape[:2]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode("ascii") + rgb.tobytes())


def cover(image: Image.Image, size: tuple[int, int]) -> Image.Image:
    """Scale to cover size, then centre-crop; a 1586x992 generator frame loses under 1 px."""
    width, height = size
    scale = max(width / image.width, height / image.height)
    scaled = image.resize((round(image.width * scale), round(image.height * scale)),
                          Image.LANCZOS)
    left = (scaled.width - width) // 2
    top = (scaled.height - height) // 2
    return scaled.crop((left, top, left + width, top + height))


def import_art(states: dict[str, Path], splashes: dict[str, Path]) -> None:
    sources_path = MASTERS / "sources.json"
    sources = json.loads(sources_path.read_text(encoding="utf-8")) if sources_path.is_file() else {}
    MASTERS.mkdir(parents=True, exist_ok=True)
    for state, source in states.items():
        image = cover(Image.open(source).convert("RGB"), MASTER_SIZE)
        target = MASTERS / f"{state}.png"
        image.save(target, optimize=True)
        sources[state] = {"generatorSha256": sha256(source), "masterSha256": sha256(target)}
    manifest_path = UI_DESIGN / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for mode, source in splashes.items():
        image = cover(Image.open(source).convert("RGB"), SPLASH_SIZE).convert("RGBA")
        relative = f"{mode}/splash.png"
        target = UI_DESIGN / relative
        image.save(target, optimize=True)
        entry = {
            "path": relative,
            "role": "splash",
            "size": list(SPLASH_SIZE),
            "sha256": sha256(target),
            "sourceSha256": sha256(source),
            "alpha": "premultiplied-on-load",
            "colorSpace": "sRGB",
        }
        manifest["assets"] = [e for e in manifest["assets"] if e.get("path") != relative]
        manifest["assets"].append(entry)
        sources[f"splash-{mode}"] = {"generatorSha256": entry["sourceSha256"],
                                     "runtimeSha256": entry["sha256"]}
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    sources_path.write_text(json.dumps(sources, indent=2, sort_keys=True) + "\n",
                            encoding="utf-8")


def erase_mouth(master: np.ndarray) -> np.ndarray:
    """Fill the singing lips with the surrounding skin by Laplace relaxation from the border."""
    result = master.astype(np.float64)
    cx, cy = (MOUTH_CENTER[0] * 2.0, MOUTH_CENTER[1] * 2.0)
    rx, ry = (MOUTH_ERASE_RADII[0] * 2.0, MOUTH_ERASE_RADII[1] * 2.0)
    ys, xs = np.mgrid[0:master.shape[0], 0:master.shape[1]]
    mask = ((xs - cx) / rx) ** 2 + ((ys - cy) / ry) ** 2 <= 1.0
    ring = (((xs - cx) / (rx + 3.0)) ** 2 + ((ys - cy) / (ry + 3.0)) ** 2 <= 1.0) & ~mask
    result[mask] = result[ring].mean(axis=0)
    for _ in range(600):
        smoothed = (np.roll(result, 1, 0) + np.roll(result, -1, 0) +
                    np.roll(result, 1, 1) + np.roll(result, -1, 1)) * 0.25
        result[mask] = smoothed[mask]
    return result


INK = (34, 28, 32)
MOUTH_INSIDE = (62, 40, 46)
TONGUE = (122, 84, 90)
TEETH = (232, 228, 230)
LIP_SHADE = (168, 156, 160)


def draw_mouth(draw: ImageDraw.ImageDraw, shape: str, cx: float, cy: float, s: float) -> None:
    """One ink-style mouth, s pixels per runtime pixel, centred on (cx, cy)."""
    def box(half_width: float, half_height: float, dy: float = 0.0):
        k = MOUTH_SCALE
        return (cx - half_width * k * s, cy - half_height * k * s + dy * k * s,
                cx + half_width * k * s, cy + half_height * k * s + dy * k * s)

    line = max(1, round(0.9 * s))
    if shape == "closed":
        draw.arc(box(5.0, 1.6, -0.4), 15, 165, fill=INK, width=line)
        draw.arc(box(2.8, 1.2, 1.6), 20, 160, fill=LIP_SHADE, width=max(1, round(0.7 * s)))
        return
    half = {"narrow": (4.2, 1.4), "nasal": (4.6, 1.1), "open": (4.0, 3.6),
            "wide": (6.0, 2.5), "round": (2.6, 3.0)}[shape]
    draw.ellipse(box(*half), fill=MOUTH_INSIDE, outline=INK, width=line)
    if shape in ("open", "wide", "nasal"):
        # Upper teeth: a pale band under the top lip.
        teeth_h = {"open": 1.0, "wide": 0.9, "nasal": 0.6}[shape]
        k = MOUTH_SCALE
        top = cy - half[1] * k * s + line
        draw.chord((cx - half[0] * 0.78 * k * s, top, cx + half[0] * 0.78 * k * s,
                    top + teeth_h * 2.0 * k * s), 180, 360, fill=TEETH)
    if shape in ("open", "wide", "round"):
        draw.ellipse(box(half[0] * 0.55, half[1] * 0.35, half[1] * 0.45), fill=TONGUE)
    draw.ellipse(box(*half), outline=INK, width=line)
    draw.arc(box(half[0] * 0.7, 1.0, half[1] + 1.3), 25, 155, fill=LIP_SHADE,
             width=max(1, round(0.6 * s)))


def mouth_sprites(focused_master: Image.Image) -> dict[str, Image.Image]:
    """Each sprite is the focused portrait's own face under MOUTH_BOX with the lips redrawn.

    The whole master is re-downscaled with the patch replaced, so the sprite's border pixels are
    exactly the portrait's pixels and the overlay has no visible edge. Pixels equal to the
    portrait become MOUTH_KEY, which the runtime keys out, so only the retouched lips are drawn.
    """
    erased = erase_mouth(np.asarray(focused_master.convert("RGB")))
    x, y, w, h = MOUTH_BOX
    left, top, right, bottom = x * 2, y * 2, (x + w) * 2, (y + h) * 2
    patch = Image.fromarray(np.clip(erased[top:bottom, left:right].round(), 0, 255)
                            .astype(np.uint8), "RGB")
    scale = SUPERSAMPLE * 2  # canvas pixels per runtime pixel
    portrait = np.asarray(focused_master.convert("RGB").resize(RUNTIME_SIZE, Image.LANCZOS)
                          .crop((x, y, x + w, y + h)), dtype=np.int16)
    sprites = {}
    for shape in MOUTHS:
        canvas = patch.resize((patch.width * SUPERSAMPLE, patch.height * SUPERSAMPLE),
                              Image.BICUBIC)
        draw_mouth(ImageDraw.Draw(canvas), shape, (MOUTH_CENTER[0] - x) * scale,
                   (MOUTH_CENTER[1] - y) * scale, scale)
        composed = focused_master.convert("RGB").copy()
        composed.paste(canvas.resize(patch.size, Image.LANCZOS), (left, top))
        runtime = composed.resize(RUNTIME_SIZE, Image.LANCZOS)
        sprite = np.asarray(runtime.crop((x, y, x + w, y + h)), dtype=np.uint8).copy()
        changed = np.abs(sprite.astype(np.int16) - portrait).max(axis=2) > 0
        if changed[0, 0] or changed[0, -1] or changed[-1, 0] or changed[-1, -1]:
            raise SystemExit(f"mouth-{shape} changes a corner pixel; the key would be lost")
        if np.all(sprite[changed] == MOUTH_KEY, axis=1).any():
            raise SystemExit(f"mouth-{shape} contains the key colour")
        sprite[~changed] = MOUTH_KEY
        sprites[shape] = Image.fromarray(sprite, "RGB")
    return sprites


def key_mask(sprite: Image.Image) -> Image.Image:
    rgb = np.asarray(sprite.convert("RGB"))
    return Image.fromarray(np.where(np.all(rgb == MOUTH_KEY, axis=2), 0, 255).astype(np.uint8), "L")


def contact_sheet(portraits: dict[str, Image.Image], focused: Image.Image,
                  sprites: dict[str, Image.Image]) -> Image.Image:
    """States in manifest order (top row), the ring's circular crop of each (middle), and the
    singing face with each mouth sprite in MOUTHS order (bottom)."""
    gap = 16
    width, height = RUNTIME_SIZE
    ring = 160
    face = 128
    sheet = Image.new("RGB", (gap + len(STATES) * (width + gap),
                              gap * 4 + height + ring + face * 2), (58, 58, 64))
    for index, state in enumerate(STATES):
        x0 = gap + index * (width + gap)
        sheet.paste(portraits[state], (x0, gap))
        crop = portraits[state].crop((0, 0, width, width)).resize((ring, ring), Image.LANCZOS)
        mask = Image.new("L", (ring, ring), 0)
        ImageDraw.Draw(mask).ellipse((0, 0, ring - 1, ring - 1), fill=255)
        sheet.paste(crop, (x0 + (width - ring) // 2, gap * 2 + height), mask)
        shape = MOUTHS[index]
        composed = focused.copy()
        composed.paste(sprites[shape], MOUTH_BOX[:2], key_mask(sprites[shape]))
        region = composed.crop((96, 40, 224, 168)).resize((face * 2, face * 2), Image.NEAREST)
        sheet.paste(region, (x0 + (width - face * 2) // 2, gap * 3 + height + ring))
    return sheet


def build() -> None:
    masters = {state: Image.open(MASTERS / f"{state}.png").convert("RGB") for state in STATES}
    portraits = {}
    for state, master in masters.items():
        if master.size != MASTER_SIZE:
            raise SystemExit(f"{state} master is {master.size}, expected {MASTER_SIZE}")
        portraits[state] = master.resize(RUNTIME_SIZE, Image.LANCZOS)
        write_ppm(portraits[state], RUNTIME / f"{state}.ppm")
    sprites = mouth_sprites(masters["focused"])
    for shape, sprite in sprites.items():
        write_ppm(sprite, RUNTIME / f"mouth-{shape}.ppm")
    manifest_path = CHARACTER / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    x, y, w, h = MOUTH_BOX
    manifest["mouthPlacement"] = {"x": x / RUNTIME_SIZE[0], "y": y / RUNTIME_SIZE[1],
                                  "width": w / RUNTIME_SIZE[0], "height": h / RUNTIME_SIZE[1]}
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    PREVIEWS.mkdir(parents=True, exist_ok=True)
    contact_sheet(portraits, portraits["focused"], sprites).save(
        PREVIEWS / "state-contact-sheet.png", optimize=True)
    digests = [hashlib.sha256((RUNTIME / f"{state}.ppm").read_bytes()).hexdigest()
               for state in STATES]
    if len(set(digests)) != len(STATES):
        raise SystemExit("two state portraits are byte-identical")
    print(json.dumps({"states": dict(zip(STATES, digests)),
                      "mouthPlacement": manifest["mouthPlacement"]}, indent=2))


def pairs(values: list[str], allowed: tuple[str, ...], flag: str) -> dict[str, Path]:
    result = {}
    for value in values:
        name, _, path = value.partition("=")
        if name not in allowed or not path:
            raise SystemExit(f"{flag} expects NAME=PATH with NAME in {allowed}: {value!r}")
        result[name] = Path(path)
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    importer = commands.add_parser("import", help="copy generator output into the masters")
    importer.add_argument("--state", action="append", default=[], metavar="STATE=PATH")
    importer.add_argument("--splash", action="append", default=[], metavar="MODE=PATH")
    commands.add_parser("build", help="derive the runtime assets from the masters")
    args = parser.parse_args(argv)
    if args.command == "import":
        import_art(pairs(args.state, STATES, "--state"), pairs(args.splash, MODES, "--splash"))
    build()
    return 0


if __name__ == "__main__":
    sys.exit(main())
