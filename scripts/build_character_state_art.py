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
  build_character_state_art.py import --outfit scene --state neutral=/path/s.png ...
  build_character_state_art.py build

The shared set (source/states/, runtime/) is the EMO look. A design mode may supply its own set,
an outfit (source/states/<outfit>/, runtime/<outfit>/), declared under the manifest's
"outfits"; the runtime selects it by design mode and falls back to the shared set. An outfit
keeps the shared framing, so it inherits the shared per-state eye boxes, but its mouth sprites
are cut from its own singing face because a sprite carries that picture's pixels.

The masters are AI-generated development art made from the owner's reference sheet; see
assets/character-01/PROVENANCE.md. Nothing here decides whether the art is releasable.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys
import struct

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
OUTFITS = ("scene",)
POSES = {"empty": "neutral", "error": "error", "complete": "complete",
         "listening": "focused"}

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

# Each state's eyes in runtime pixels (left, top, right, bottom), in image order (viewer's left
# first), measured on the shared masters. The idle blink closes a lid inside these boxes; a state
# whose other eye is hidden (the error pose's hand and hair) lists only the visible one.
EYES = {
    "neutral": ((124, 82, 142, 94), (158, 72, 182, 85)),
    "focused": ((122, 69, 142, 80), (156, 62, 180, 73)),
    "rendering": ((120, 90, 142, 106), (154, 78, 180, 94)),
    "complete": ((122, 86, 142, 102), (154, 70, 180, 85)),
    "warning": ((122, 82, 142, 94), (156, 70, 180, 84)),
    "error": ((138, 96, 174, 114),),
}


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


def write_qoi(image: Image.Image, path: Path) -> None:
    """Encode straight RGBA/RGB pixels with the QOI chunk rules, without a codec dependency."""
    image = image.convert("RGBA")
    width, height = image.size
    pixels = image.tobytes()
    header = b"qoif" + struct.pack(">IIBB", width, height, 4, 0)
    index = [(0, 0, 0, 0)] * 64
    previous = (0, 0, 0, 255)
    chunks = bytearray()
    run = 0
    count = width * height
    for offset in range(count):
        pixel = tuple(pixels[offset * 4:offset * 4 + 4])
        if pixel == previous:
            run += 1
            if run == 62 or offset == count - 1:
                chunks.append(0xC0 | (run - 1))
                run = 0
            continue
        if run:
            chunks.append(0xC0 | (run - 1))
            run = 0
        slot = (pixel[0] * 3 + pixel[1] * 5 + pixel[2] * 7 + pixel[3] * 11) % 64
        if index[slot] == pixel:
            chunks.append(slot)
        else:
            index[slot] = pixel
            if pixel[3] != previous[3]:
                chunks.extend((0xFF, *pixel))
            else:
                dr, dg, db = (pixel[i] - previous[i] for i in range(3))
                if -2 <= dr <= 1 and -2 <= dg <= 1 and -2 <= db <= 1:
                    chunks.append(0x40 | ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2))
                elif -32 <= dg <= 31 and -8 <= dr - dg <= 7 and -8 <= db - dg <= 7:
                    chunks.extend((0x80 | (dg + 32), ((dr - dg + 8) << 4) | (db - dg + 8)))
                else:
                    chunks.extend((0xFE, *pixel[:3]))
        previous = pixel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + chunks + bytes((0, 0, 0, 0, 0, 0, 0, 1)))


def cover(image: Image.Image, size: tuple[int, int]) -> Image.Image:
    """Scale to cover size, then centre-crop; a 1586x992 generator frame loses under 1 px."""
    width, height = size
    scale = max(width / image.width, height / image.height)
    scaled = image.resize((round(image.width * scale), round(image.height * scale)),
                          Image.LANCZOS)
    left = (scaled.width - width) // 2
    top = (scaled.height - height) // 2
    return scaled.crop((left, top, left + width, top + height))


def masters_dir(outfit: str | None) -> Path:
    return MASTERS / outfit if outfit else MASTERS


def runtime_dir(outfit: str | None) -> Path:
    return RUNTIME / outfit if outfit else RUNTIME


def import_art(states: dict[str, Path], splashes: dict[str, Path], outfit: str | None) -> None:
    sources_path = MASTERS / "sources.json"
    sources = json.loads(sources_path.read_text(encoding="utf-8")) if sources_path.is_file() else {}
    directory = masters_dir(outfit)
    directory.mkdir(parents=True, exist_ok=True)
    for state, source in states.items():
        image = cover(Image.open(source).convert("RGB"), MASTER_SIZE)
        target = directory / f"{state}.png"
        image.save(target, optimize=True)
        key = f"{outfit}/{state}" if outfit else state
        sources[key] = {"generatorSha256": sha256(source), "masterSha256": sha256(target)}
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
    # Only the lips' ellipse (plus a two-pixel margin) is replaced in the master, so every master
    # pixel outside it is untouched and the runtime pixels away from the lips stay bit-identical.
    lips = Image.new("L", patch.size, 0)
    cx, cy = (MOUTH_CENTER[0] - x) * 2.0, (MOUTH_CENTER[1] - y) * 2.0
    rx, ry = MOUTH_ERASE_RADII[0] * 2.0 + 2.0, MOUTH_ERASE_RADII[1] * 2.0 + 2.0
    ImageDraw.Draw(lips).ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=255)
    sprites = {}
    for shape in MOUTHS:
        canvas = patch.resize((patch.width * SUPERSAMPLE, patch.height * SUPERSAMPLE),
                              Image.BICUBIC)
        draw_mouth(ImageDraw.Draw(canvas), shape, (MOUTH_CENTER[0] - x) * scale,
                   (MOUTH_CENTER[1] - y) * scale, scale)
        composed = focused_master.convert("RGB").copy()
        composed.paste(canvas.resize(patch.size, Image.LANCZOS), (left, top), lips)
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


def normalized(box: tuple[int, int, int, int]) -> dict[str, float]:
    left, top, right, bottom = box
    width, height = RUNTIME_SIZE
    return {"x": left / width, "y": top / height, "width": (right - left) / width,
            "height": (bottom - top) / height}


def eye_sheet(sets: dict[str, dict[str, Image.Image]]) -> Image.Image:
    """Each set's six faces (rows) with the declared eye boxes outlined, 2x, for review."""
    crop = (96, 40, 224, 136)
    scale = 2
    w, h = (crop[2] - crop[0]) * scale, (crop[3] - crop[1]) * scale
    gap = 8
    sheet = Image.new("RGB", (gap + len(STATES) * (w + gap), gap + len(sets) * (h + gap)),
                      (58, 58, 64))
    for row, portraits in enumerate(sets.values()):
        for column, state in enumerate(STATES):
            face = portraits[state].crop(crop).resize((w, h), Image.NEAREST)
            draw = ImageDraw.Draw(face)
            for left, top, right, bottom in EYES[state]:
                draw.rectangle(((left - crop[0]) * scale, (top - crop[1]) * scale,
                                (right - crop[0]) * scale - 1, (bottom - crop[1]) * scale - 1),
                               outline=(0, 255, 120))
            sheet.paste(face, (gap + column * (w + gap), gap + row * (h + gap)))
    return sheet


def build_set(outfit: str | None) -> tuple[dict[str, Image.Image], dict]:
    """Writes one set's runtime PPMs and returns its portraits and manifest fragment."""
    source = masters_dir(outfit)
    target = runtime_dir(outfit)
    prefix = f"runtime/{outfit}/" if outfit else "runtime/"
    masters = {state: Image.open(source / f"{state}.png").convert("RGB") for state in STATES}
    portraits = {}
    for state, master in masters.items():
        if master.size != MASTER_SIZE:
            raise SystemExit(f"{outfit or 'shared'} {state} master is {master.size}")
        portraits[state] = master.resize(RUNTIME_SIZE, Image.LANCZOS)
        write_ppm(portraits[state], target / f"{state}.ppm")
    sprites = mouth_sprites(masters["focused"])
    for shape, sprite in sprites.items():
        write_ppm(sprite, target / f"mouth-{shape}.ppm")
    x, y, w, h = MOUTH_BOX
    fragment = {
        "states": {state: f"{prefix}{state}.ppm" for state in STATES},
        "mouths": {shape: f"{prefix}mouth-{shape}.ppm" for shape in MOUTHS},
        "mouthPlacement": normalized((x, y, x + w, y + h)),
    }
    name = f"{outfit}-" if outfit else ""
    PREVIEWS.mkdir(parents=True, exist_ok=True)
    contact_sheet(portraits, portraits["focused"], sprites).save(
        PREVIEWS / f"{name}state-contact-sheet.png", optimize=True)
    digests = [hashlib.sha256((RUNTIME / f"{state}.ppm").read_bytes()).hexdigest()
               for state in STATES]
    if len(set(digests)) != len(STATES):
        raise SystemExit("two state portraits are byte-identical")
    return portraits, fragment


def build_v4(manifest: dict) -> None:
    """Derive v4's bounded QOI assets from the reviewed development masters.

    Existing state poses remain their authored pictures. The two full-body stage sources were
    generated as edits of the corresponding neutral masters and are recorded in PROVENANCE.md.
    Layer splitting, crops, mouths and eye variants are deterministic image operations.
    """
    manifest["schemaVersion"] = 4
    manifest["version"] = "0.4.0-dev"
    manifest["defaultOutfit"] = "emo"
    rings, stages, poses = {}, {}, {}
    for mode in MODES:
        source = masters_dir(None if mode == "emo" else mode)
        base = CHARACTER / "runtime/v4" / mode
        prefix = f"runtime/v4/{mode}"
        state_paths, avatar_paths = {}, {}
        for state in STATES:
            master = Image.open(source / f"{state}.png").convert("RGBA")
            ring = master.crop((0, 0, 640, 640)).resize((512, 512), Image.Resampling.LANCZOS)
            ring_mask = Image.new("L", (512, 512))
            ImageDraw.Draw(ring_mask).ellipse((0, 0, 511, 511), fill=255)
            ring.putalpha(ring_mask)
            avatar = ring.resize((64, 64), Image.Resampling.LANCZOS)
            write_qoi(ring, base / "ring" / f"{state}.qoi")
            write_qoi(avatar, base / "avatar" / f"{state}.qoi")
            state_paths[state] = f"{prefix}/ring/{state}.qoi"
            avatar_paths[state] = f"{prefix}/avatar/{state}.qoi"
        # The original keyed 24px sprites are reused: only the magenta key becomes transparent.
        mouth_paths = {}
        for shape in MOUTHS:
            original = Image.open(runtime_dir(None if mode == "emo" else mode) /
                                  f"mouth-{shape}.ppm").convert("RGB")
            rgb = np.asarray(original)
            alpha = np.where(np.all(rgb == MOUTH_KEY, axis=2), 0, 255).astype(np.uint8)
            rgba = np.dstack((rgb, alpha))
            mouth = Image.fromarray(rgba, "RGBA").resize((38, 38), Image.Resampling.NEAREST)
            write_qoi(mouth, base / "mouth" / f"{shape}.qoi")
            mouth_paths[shape] = f"{prefix}/mouth/{shape}.qoi"
        rings[mode] = {
            "states": state_paths, "avatars": avatar_paths, "mouths": mouth_paths,
            "mouthPlacement": {"x": 237 / 512, "y": 144 / 512,
                               "width": 38 / 512, "height": 38 / 512},
            "eyes": {state: [{"x": l / 320, "y": t / 320,
                               "width": (r-l) / 320, "height": (b-t) / 320}
                              for l, t, r, b in EYES[state]] for state in STATES},
        }
        full = Image.open(CHARACTER / "source/stage" / f"{mode}-full-body.png").convert("RGBA")
        # Preserve the full figure's aspect and transparency within the promised 900x1600 canvas.
        fitted = Image.new("RGBA", (900, 1600))
        contained = full.copy()
        contained.thumbnail((900, 1600), Image.Resampling.LANCZOS)
        at = ((900 - contained.width) // 2, (1600 - contained.height) // 2)
        fitted.alpha_composite(contained, at)
        # The head and body are independent alpha layers. The fringe is isolated for a future
        # over-eye pass; at rest the three layers compose to the approved full-body picture.
        body = fitted.copy()
        head = Image.new("RGBA", fitted.size)
        head.paste(fitted.crop((0, 0, 900, 510)), (0, 0))
        ImageDraw.Draw(body).rectangle((0, 0, 899, 509), fill=(0, 0, 0, 0))
        eye_box = (338, 232, 225, 78)
        ImageDraw.Draw(head).rectangle((eye_box[0], eye_box[1],
                                        eye_box[0] + eye_box[2] - 1,
                                        eye_box[1] + eye_box[3] - 1), fill=(0, 0, 0, 0))
        fringe = Image.new("RGBA", fitted.size)
        fringe.paste(fitted.crop((300, 0, 620, 210)), (300, 0))
        ImageDraw.Draw(head).rectangle((300, 0, 619, 209), fill=(0, 0, 0, 0))
        stage_layers = []
        for name, layer in (("body", body), ("head", head), ("hair-front", fringe)):
            write_qoi(layer, base / "stage" / f"{name}.qoi")
            stage_layers.append(f"{prefix}/stage/{name}.qoi")
        eye_image = fitted.crop((eye_box[0], eye_box[1], eye_box[0] + eye_box[2],
                                 eye_box[1] + eye_box[3]))
        stage_eyes = {"box": {"x": eye_box[0], "y": eye_box[1],
                              "width": eye_box[2], "height": eye_box[3]}}
        for name, closedness in (("open", 0.0), ("half", 0.35), ("closed", 0.68)):
            sprite = eye_image.copy()
            if closedness:
                veil = Image.new("RGBA", sprite.size, (151, 141, 143, round(220 * closedness)))
                sprite = Image.alpha_composite(sprite, veil)
                draw = ImageDraw.Draw(sprite)
                y = round(sprite.height * (0.45 + closedness * 0.25))
                draw.line((28, y, 88, y + 3, 125, y - 2, 198, y), fill=(29, 23, 28, 230),
                          width=round(2 + closedness * 4))
            write_qoi(sprite, base / "stage" / f"eyes-{name}.qoi")
            stage_eyes[name] = f"{prefix}/stage/eyes-{name}.qoi"
        stages[mode] = {"size": [900, 1600], "layers": stage_layers, "eyes": stage_eyes}
        poses[mode] = {}
        pose_previews = {}
        for pose, state in POSES.items():
            if pose == "empty":
                splash = Image.open(UI_DESIGN / mode / "splash.png").convert("RGBA")
                crop = splash.crop((600, 0, 1600, 1000)).resize((800, 800), Image.Resampling.LANCZOS)
            elif pose == "listening":
                crop = Image.open(CHARACTER / "source/poses" /
                                  f"{mode}-listening.png").convert("RGBA").resize(
                                      (800, 800), Image.Resampling.LANCZOS)
            else:
                master = Image.open(source / f"{state}.png").convert("RGBA")
                crop = master.crop((0, 0, 640, 640)).resize((800, 800), Image.Resampling.LANCZOS)
            write_qoi(crop, base / "pose" / f"{pose}.qoi")
            poses[mode][pose] = f"{prefix}/pose/{pose}.qoi"
            pose_previews[pose] = crop
        sheet = Image.new("RGB", (6 * 256, 512), (20, 20, 24))
        for col, state in enumerate(STATES):
            ring = Image.open(source / f"{state}.png").convert("RGB")
            sheet.paste(ring.crop((0, 0, 640, 640)).resize((256, 256)), (col * 256, 0))
        sheet.paste(fitted.convert("RGB").resize((144, 256)), (0, 256))
        for col, pose in enumerate(POSES):
            sheet.paste(pose_previews[pose].convert("RGB").resize((256, 256)),
                        (144 + col * 256, 256))
        PREVIEWS.mkdir(parents=True, exist_ok=True)
        sheet.save(PREVIEWS / f"v4-{mode}.png", optimize=True)
    # A byte-exact RGB twin of an existing PPM verifies the decoder independent of resizing.
    write_qoi(Image.open(RUNTIME / "neutral.ppm"), CHARACTER / "runtime/v4/ppm-equivalence-neutral.qoi")
    manifest.pop("ringPortraits", None)
    manifest.pop("stages", None)
    manifest["portraits"] = rings
    manifest["stage"] = stages
    manifest["poses"] = poses


def build() -> None:
    manifest_path = CHARACTER / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    shared, fragment = build_set(None)
    manifest["states"] = fragment["states"]
    manifest["mouths"] = fragment["mouths"]
    manifest["mouthPlacement"] = fragment["mouthPlacement"]
    manifest["eyes"] = {state: [normalized(box) for box in EYES[state]] for state in STATES}
    sets = {"shared": shared}
    outfits = {}
    for outfit in OUTFITS:
        if not (masters_dir(outfit) / "neutral.png").is_file():
            continue
        sets[outfit], outfits[outfit] = build_set(outfit)
    if outfits:
        manifest["outfits"] = outfits
    else:
        manifest.pop("outfits", None)
    build_v4(manifest)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    eye_sheet(sets).save(PREVIEWS / "eye-boxes.png", optimize=True)
    print(json.dumps({"sets": list(sets), "eyes": manifest["eyes"]["neutral"]}, indent=2))


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
    importer.add_argument("--outfit", choices=OUTFITS, help="import into this mode's own set")
    importer.add_argument("--state", action="append", default=[], metavar="STATE=PATH")
    importer.add_argument("--splash", action="append", default=[], metavar="MODE=PATH")
    commands.add_parser("build", help="derive the runtime assets from the masters")
    args = parser.parse_args(argv)
    if args.command == "import":
        import_art(pairs(args.state, STATES, "--state"), pairs(args.splash, MODES, "--splash"),
                   args.outfit)
    build()
    return 0


if __name__ == "__main__":
    sys.exit(main())
