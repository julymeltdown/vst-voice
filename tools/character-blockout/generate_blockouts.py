#!/usr/bin/env python3
"""Generate deliberately low-detail Phase 1 silhouette blockouts.

The output is not production character art. It is a topology and silhouette
fixture used to test 128px recognition before a contracted 3D artist starts.
The generator uses only the Python standard library; matplotlib is optional for
preview PNGs and is never part of the shipped application.
"""

from __future__ import annotations

import argparse
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

Vec3 = tuple[float, float, float]
Face = tuple[int, int, int]


@dataclass
class Part:
    name: str
    material: str
    vertices: list[Vec3]
    faces: list[Face]


def rotate(v: Vec3, rx: float = 0.0, ry: float = 0.0, rz: float = 0.0) -> Vec3:
    x, y, z = v
    cx, sx = math.cos(rx), math.sin(rx)
    cy, sy = math.cos(ry), math.sin(ry)
    cz, sz = math.cos(rz), math.sin(rz)
    y, z = y * cx - z * sx, y * sx + z * cx
    x, z = x * cy + z * sy, -x * sy + z * cy
    x, y = x * cz - y * sz, x * sz + y * cz
    return x, y, z


def box(name: str, material: str, center: Vec3, size: Vec3,
        rotation: Vec3 = (0.0, 0.0, 0.0)) -> Part:
    sx, sy, sz = (value / 2.0 for value in size)
    raw = [
        (-sx, -sy, -sz), (sx, -sy, -sz), (sx, sy, -sz), (-sx, sy, -sz),
        (-sx, -sy, sz), (sx, -sy, sz), (sx, sy, sz), (-sx, sy, sz),
    ]
    vertices = []
    for vertex in raw:
        x, y, z = rotate(vertex, *rotation)
        vertices.append((x + center[0], y + center[1], z + center[2]))
    faces = [
        (0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7),
        (0, 1, 5), (0, 5, 4), (1, 2, 6), (1, 6, 5),
        (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7),
    ]
    return Part(name, material, vertices, faces)


def tapered_box(name: str, material: str, center: Vec3, height: float,
                bottom: tuple[float, float], top: tuple[float, float]) -> Part:
    bx, bz = bottom[0] / 2.0, bottom[1] / 2.0
    tx, tz = top[0] / 2.0, top[1] / 2.0
    h = height / 2.0
    vertices = [
        (-bx, -h, -bz), (bx, -h, -bz), (bx, -h, bz), (-bx, -h, bz),
        (-tx, h, -tz), (tx, h, -tz), (tx, h, tz), (-tx, h, tz),
    ]
    vertices = [(x + center[0], y + center[1], z + center[2]) for x, y, z in vertices]
    faces = [
        (0, 1, 2), (0, 2, 3), (4, 6, 5), (4, 7, 6),
        (0, 4, 5), (0, 5, 1), (1, 5, 6), (1, 6, 2),
        (2, 6, 7), (2, 7, 3), (3, 7, 4), (3, 4, 0),
    ]
    return Part(name, material, vertices, faces)


def icosahedron(name: str, material: str, center: Vec3, scale: Vec3) -> Part:
    phi = (1.0 + math.sqrt(5.0)) / 2.0
    raw = [
        (-1, phi, 0), (1, phi, 0), (-1, -phi, 0), (1, -phi, 0),
        (0, -1, phi), (0, 1, phi), (0, -1, -phi), (0, 1, -phi),
        (phi, 0, -1), (phi, 0, 1), (-phi, 0, -1), (-phi, 0, 1),
    ]
    length = math.sqrt(1 + phi * phi)
    vertices = [
        (center[0] + x / length * scale[0],
         center[1] + y / length * scale[1],
         center[2] + z / length * scale[2])
        for x, y, z in raw
    ]
    faces = [
        (0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11),
        (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
        (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9),
        (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1),
    ]
    return Part(name, material, vertices, faces)


def fringe(name: str, material: str, x: float, y: float, z: float,
           length: float, side: float) -> Part:
    # Narrow triangular prism used for the intentionally chunky hair clumps.
    width = 0.22
    depth = 0.16
    vertices = [
        (x - width, y, z - depth), (x + width, y, z - depth),
        (x, y - length, z - depth),
        (x - width, y, z + depth), (x + width, y, z + depth),
        (x + side * 0.08, y - length, z + depth),
    ]
    faces = [
        (0, 1, 2), (3, 5, 4), (0, 3, 4), (0, 4, 1),
        (1, 4, 5), (1, 5, 2), (2, 5, 3), (2, 3, 0),
    ]
    return Part(name, material, vertices, faces)


def build_concept(accent: str) -> list[Part]:
    parts: list[Part] = []
    # Feet and legs
    parts += [
        box("left_shoe", "shoe", (-0.27, 0.14, 0.05), (0.34, 0.24, 0.62)),
        box("right_shoe", "shoe", (0.27, 0.14, 0.05), (0.34, 0.24, 0.62)),
        tapered_box("left_lower_leg", "jeans", (-0.25, 0.95, 0), 1.45, (0.22, 0.24), (0.25, 0.27)),
        tapered_box("right_lower_leg", "jeans", (0.25, 0.95, 0), 1.45, (0.22, 0.24), (0.25, 0.27)),
        tapered_box("left_upper_leg", "jeans", (-0.24, 2.15, 0), 1.15, (0.26, 0.28), (0.34, 0.33)),
        tapered_box("right_upper_leg", "jeans", (0.24, 2.15, 0), 1.15, (0.26, 0.28), (0.34, 0.33)),
    ]
    # Torso, hoodie, arms
    torso_width = 0.86 if accent != "teal" else 0.92
    hoodie_length = 1.38 if accent == "burgundy" else (1.5 if accent == "violet" else 1.46)
    parts += [
        tapered_box("hips", "jeans", (0, 2.83, 0), 0.46, (0.72, 0.38), (0.65, 0.34)),
        tapered_box("shirt", "shirt", (0, 3.55, 0), 1.22, (0.68, 0.30), (0.74, 0.34)),
        tapered_box("hoodie", "hoodie", (0, 3.57, 0.03), hoodie_length,
                    (torso_width, 0.44), (torso_width + 0.05, 0.48)),
        box("left_arm", "hoodie", (-0.62, 3.45, 0), (0.26, 1.45, 0.32), rotation=(0, 0, -0.08)),
        box("right_arm", "hoodie", (0.62, 3.45, 0), (0.26, 1.45, 0.32), rotation=(0, 0, 0.08)),
        box("left_hand", "skin", (-0.67, 2.68, 0), (0.20, 0.30, 0.22)),
        box("right_hand", "skin", (0.67, 2.68, 0), (0.20, 0.30, 0.22)),
        box("neck", "skin", (0, 4.43, 0), (0.25, 0.28, 0.23)),
        icosahedron("head", "skin", (0, 4.91, 0), (0.49, 0.57, 0.43)),
        icosahedron("hair_cap", "hair", (0, 5.05, -0.02), (0.54, 0.56, 0.47)),
        box("hood", "hoodie", (0, 4.39, -0.17), (0.78, 0.38, 0.28), rotation=(0.15, 0, 0)),
    ]
    # Asymmetrical hair clumps, deliberately coarse.
    if accent == "burgundy":
        parts += [
            fringe("fringe_left", "hair", -0.20, 5.20, 0.34, 0.90, -1),
            fringe("fringe_center", "hair", 0.02, 5.18, 0.36, 0.67, 1),
            fringe("side_lock", "accent", -0.34, 4.95, 0.16, 0.82, -1),
        ]
    elif accent == "violet":
        parts += [
            fringe("fringe_left", "hair", -0.17, 5.22, 0.34, 1.05, -1),
            fringe("fringe_center", "hair", 0.08, 5.15, 0.36, 0.76, 1),
            fringe("side_lock", "accent", 0.31, 4.94, 0.14, 0.74, 1),
        ]
    else:
        parts += [
            fringe("fringe_left", "hair", -0.18, 5.18, 0.34, 0.82, -1),
            fringe("fringe_center", "hair", 0.09, 5.17, 0.36, 0.60, 1),
            fringe("side_lock", "accent", -0.30, 4.96, 0.16, 0.62, -1),
        ]
    # Minimal product splice markers; geometry, not floating accessories.
    parts += [
        box("left_splice", "accent", (-0.255, 1.42, 0.135), (0.27, 0.035, 0.025)),
        box("hoodie_splice", "accent", (0.43, 3.40, 0.255), (0.20, 0.035, 0.025)),
    ]
    return parts


MATERIALS = {
    "skin": (0.72, 0.52, 0.42),
    "hair": (0.025, 0.025, 0.035),
    "hoodie": (0.055, 0.055, 0.070),
    "shirt": (0.09, 0.08, 0.10),
    "jeans": (0.035, 0.035, 0.045),
    "shoe": (0.06, 0.055, 0.06),
}
ACCENTS = {
    "burgundy": (0.30, 0.08, 0.13),
    "violet": (0.26, 0.12, 0.38),
    "teal": (0.02, 0.30, 0.31),
}


def write_obj(parts: Iterable[Part], obj_path: Path, accent: str) -> tuple[int, int]:
    mtl_path = obj_path.with_suffix(".mtl")
    with mtl_path.open("w", encoding="utf-8") as mtl:
        colors = dict(MATERIALS)
        colors["accent"] = ACCENTS[accent]
        for name, (r, g, b) in colors.items():
            mtl.write(f"newmtl {name}\nKd {r:.4f} {g:.4f} {b:.4f}\nKa 0.02 0.02 0.02\nNs 4\n\n")

    vertex_offset = 1
    total_vertices = 0
    total_faces = 0
    with obj_path.open("w", encoding="utf-8") as obj:
        obj.write(f"# Project SEAM Phase 1 {accent} low-poly blockout\n")
        obj.write(f"mtllib {mtl_path.name}\n")
        for part in parts:
            obj.write(f"o {part.name}\nusemtl {part.material}\n")
            for x, y, z in part.vertices:
                obj.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
            for a, b, c in part.faces:
                obj.write(f"f {a + vertex_offset} {b + vertex_offset} {c + vertex_offset}\n")
            vertex_offset += len(part.vertices)
            total_vertices += len(part.vertices)
            total_faces += len(part.faces)
    return total_vertices, total_faces


def render_preview(parts: list[Part], path: Path, accent: str) -> None:
    try:
        import matplotlib.pyplot as plt
        from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    except ImportError:
        return

    fig = plt.figure(figsize=(5, 7), dpi=160)
    axis = fig.add_subplot(111, projection="3d")
    colors = dict(MATERIALS)
    colors["accent"] = ACCENTS[accent]
    for part in parts:
        polygons = [[(part.vertices[i][0], part.vertices[i][2], part.vertices[i][1]) for i in (a, b, c)] for a, b, c in part.faces]
        collection = Poly3DCollection(polygons, linewidths=0.15, edgecolors=(0.08, 0.08, 0.09, 0.5))
        collection.set_facecolor((*colors[part.material], 1.0))
        axis.add_collection3d(collection)
    axis.set_xlim(-1.1, 1.1)
    axis.set_ylim(-0.8, 0.8)
    axis.set_zlim(0, 5.8)
    axis.view_init(elev=8, azim=78)
    axis.set_box_aspect((2.2, 1.6, 5.8))
    axis.set_axis_off()
    fig.patch.set_facecolor("#efedef")
    axis.set_facecolor("#efedef")
    plt.tight_layout(pad=0)
    fig.savefig(path, bbox_inches="tight", pad_inches=0.04)
    plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = []
    for accent in ("burgundy", "violet", "teal"):
        parts = build_concept(accent)
        obj = args.output / f"phase1-{accent}-blockout.obj"
        vertices, faces = write_obj(parts, obj, accent)
        render_preview(parts, args.output / f"phase1-{accent}-blockout.png", accent)
        manifest.append((accent, vertices, faces))
    with (args.output / "README.md").open("w", encoding="utf-8") as readme:
        readme.write("# Phase 1 low-poly blockouts\n\n")
        readme.write("These meshes test silhouette and topology constraints only. They are not final character assets.\n\n")
        for accent, vertices, faces in manifest:
            readme.write(f"- `{accent}`: {vertices} vertices, {faces} triangles\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
