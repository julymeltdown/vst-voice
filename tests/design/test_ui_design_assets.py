"""Mutation tests for the shipped EMO/SCENE PNG and provenance gate."""

from __future__ import annotations

import json
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
import zlib

from scripts import verify_ui_design_assets as gate


def rgba_png(width: int, height: int, alpha: int) -> bytes:
    def chunk(name: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + name + payload
                + struct.pack(">I", zlib.crc32(name + payload)))

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    pixels = (b"\0" + bytes((255, 30, 30, alpha)) * width) * height
    return (gate.PNG_SIGNATURE + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))


class UiDesignAssetTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.assets = Path(self.temporary.name) / "ui-design"
        shutil.copytree(gate.DEFAULT_ASSETS, self.assets)

    def manifest(self) -> dict:
        return json.loads((self.assets / "manifest.json").read_text(encoding="utf-8"))

    def save_manifest(self, manifest: dict) -> None:
        (self.assets / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")

    def test_shipped_assets_pass_with_declared_limitations(self):
        self.assertEqual([], gate.verify(gate.DEFAULT_ASSETS))

    def test_changed_bytes_fail_hash_gate(self):
        image = self.assets / "emo" / "portrait.png"
        image.write_bytes(rgba_png(768, 768, 255))
        self.assertTrue(any("file SHA-256 mismatch" in e for e in gate.verify(self.assets)))

    def test_corrupt_pixel_stream_fails_even_with_new_file_hash(self):
        import hashlib

        image = self.assets / "scene" / "portrait.png"
        encoded = bytearray(image.read_bytes())
        encoded[100] ^= 1
        image.write_bytes(encoded)
        manifest = self.manifest()
        entry = next(e for e in manifest["assets"] if e["path"] == "scene/portrait.png")
        entry["sha256"] = hashlib.sha256(encoded).hexdigest()
        self.save_manifest(manifest)
        self.assertTrue(any("CRC mismatch" in e for e in gate.verify(self.assets)))

    def test_opaque_stage_fails_even_with_matching_manifest(self):
        import hashlib

        image = self.assets / "emo" / "stage.png"
        encoded = rgba_png(2, 2, 255)
        image.write_bytes(encoded)
        manifest = self.manifest()
        entry = next(e for e in manifest["assets"] if e["path"] == "emo/stage.png")
        entry["sha256"] = hashlib.sha256(encoded).hexdigest()
        entry["size"] = [2, 2]
        self.save_manifest(manifest)
        self.assertTrue(any("fully transparent and opaque" in e for e in gate.verify(self.assets)))

    def test_manifest_cannot_redirect_runtime_filename(self):
        manifest = self.manifest()
        manifest["assets"][0]["path"] = "../outside/portrait.png"
        self.save_manifest(manifest)
        errors = gate.verify(self.assets)
        self.assertTrue(any("unexpected runtime asset path" in e for e in errors))
        self.assertTrue(any("mode/role parity mismatch" in e for e in errors))

    def test_release_status_requires_separate_clearance(self):
        manifest = self.manifest()
        manifest["developmentOnly"] = False
        self.save_manifest(manifest)
        self.assertTrue(any("developmentOnly" in e for e in gate.verify(self.assets)))

    def test_source_provenance_hash_must_be_well_formed(self):
        manifest = self.manifest()
        manifest["assets"][0]["sourceSha256"] = "unknown"
        self.save_manifest(manifest)
        self.assertTrue(any("invalid sourceSha256" in e for e in gate.verify(self.assets)))

    def test_undeclared_runtime_label_is_rejected(self):
        manifest = self.manifest()
        manifest["assets"][0]["displayName"] = "external brand"
        self.save_manifest(manifest)
        self.assertTrue(any("undeclared metadata" in e for e in gate.verify(self.assets)))

    def test_unlisted_png_and_missing_provenance_fail(self):
        (self.assets / "scene" / "extra.png").write_bytes(rgba_png(1, 1, 0))
        (self.assets / "PROVENANCE.md").write_text("development only\n", encoding="utf-8")
        errors = gate.verify(self.assets)
        self.assertTrue(any("unmanifested" in e for e in errors))
        self.assertTrue(any("provenance omits runtime asset" in e for e in errors))


if __name__ == "__main__":
    unittest.main()
