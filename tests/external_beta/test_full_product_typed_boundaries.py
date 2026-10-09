"""Regression probes for the current U45 adapter's admission boundaries."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock
import zlib

from tools.external_beta import full_product_gate as gate
from tools.external_beta import soak_session_validation as soak
from tests.external_beta.full_product_typed_fixture import png
from tests.external_beta import test_full_product_typed_gate as typed_tests


def reference(name, data):
    return {"locator": name, "sha256": hashlib.sha256(data).hexdigest()}


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


class TypedEvidenceBoundaryTests(unittest.TestCase):
    def test_float_negative_zero_is_silence_and_opposite_finite_values_are_valid(self):
        encode = typed_tests.ArtifactParserTests._float_wav
        self.assertIn("digital silence", gate.audio_errors(encode([-0.0, 0.0]), "audio")[0])
        self.assertEqual([], gate.audio_errors(encode([0.25, -0.25]), "audio"))
        self.assertIn("non-finite", gate.audio_errors(encode([float("inf"), 0.25]), "audio")[0])

    def test_extensible_audio_requires_entire_guid_and_valid_bit_contract(self):
        base = struct.pack("<HHIIHH", 0xfffe, 1, 48000, 96000, 2, 16)
        extension = struct.pack("<HHI", 22, 16, 0) + struct.pack("<I", 1) + bytes.fromhex("00001000800000aa00389b71")
        def wav(ext):
            fmt = base + ext
            body = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt + b"data\x02\x00\x00\x00\x01\x00"
            return b"RIFF" + struct.pack("<I", len(body)) + body
        self.assertEqual([], gate.audio_errors(wav(extension), "audio"))
        self.assertTrue(gate.audio_errors(wav(extension[:-1] + b"\0"), "audio"))
        self.assertTrue(gate.audio_errors(wav(extension[:2] + b"\x0f\0" + extension[4:]), "audio"))

    def test_png_requires_complete_crc_checked_scanlines(self):
        prefix, end = png()[:33], chunk(b"IEND", b"")
        for data in (
            prefix + end,
            prefix + chunk(b"IDAT", b"garbage") + end,
            prefix + chunk(b"IDAT", zlib.compress(b"\0")) + end,
            prefix + chunk(b"IDAT", zlib.compress(b"\x05" + b"\0" * 6 + b"\0" * 7)) + end,
            prefix + chunk(b"IDAT", zlib.compress(b"\0" * 14) + b"trailing") + end,
            prefix + chunk(b"IDAT", zlib.compress(b"\0" * 15)) + end,
            png() + b"trailing",
            png()[:-5] + b"X" + png()[-4:],
        ):
            with self.subTest(data=data):
                self.assertTrue(gate.ui_capture_errors(data, "capture"))
        self.assertEqual([], gate.ui_capture_errors(png(), "capture"))

    def test_png_decoded_size_is_bounded_before_decompression(self):
        header = chunk(b"IHDR", struct.pack(">IIBBBBB", 32768, 32768, 8, 6, 0, 0, 0))
        with mock.patch.object(gate.zlib, "decompressobj", side_effect=AssertionError("must reject before inflate")):
            self.assertIn("64 MiB", gate.ui_capture_errors(gate.PNG_SIGNATURE + header + gate.PNG_END, "capture")[0])

    def test_guarded_snapshot_precedes_generic_cache_and_no_reopen(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data = b'{"recordType":"example"}'
            ref = reference("record.json", data)
            (root / ref["locator"]).write_bytes(data)
            context = soak.SoakReplayContext()
            evidence = gate._Evidence(root, context)
            self.assertEqual((data, None), evidence.read(ref, "first"))
            # Once the soak reader has announced this target, its snapshot wins.
            self.assertEqual(data, soak.read_reference(ref, evidence_root=root, maximum_bytes=4096, replay_context=context))
            (root / ref["locator"]).write_bytes(b"replaced")
            with mock.patch.object(gate, "_read_regular_reference", side_effect=AssertionError("must not reopen")):
                self.assertEqual((data, None), evidence.read(ref, "second"))
                bad = {**ref, "locator": str(root / ref["locator"])}
                self.assertIn("safe relative locator", evidence.read(bad, "alias")[1])

    def test_guarded_failure_overrides_an_earlier_generic_success(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ref = reference("record.json", b"payload")
            (root / ref["locator"]).write_bytes(b"payload")
            context = soak.SoakReplayContext()
            evidence = gate._Evidence(root, context)
            self.assertEqual((b"payload", None), evidence.read(ref, "first"))
            with mock.patch.object(soak, "_opened", side_effect=PermissionError("controlled denial")):
                with self.assertRaises(ValueError):
                    soak.read_reference(ref, evidence_root=root, maximum_bytes=4096, replay_context=context)
            with mock.patch.object(gate, "_read_regular_reference", side_effect=AssertionError("must not reopen")):
                self.assertIn("controlled denial", evidence.read(ref, "second")[1])

    def test_ordinary_failed_read_is_memoized_and_aggregate_cache_is_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = gate._Evidence(root)
            ref = reference("missing.json", b"x")
            self.assertIsNotNone(evidence.read(ref, "first")[1])
            (root / "missing.json").write_bytes(b"x")
            self.assertIsNotNone(evidence.read(ref, "retry")[1])
            evidence._cached_bytes = 256 * 1024 * 1024
            (root / "another.json").write_bytes(b"x")
            self.assertIn("byte limit", evidence.read(reference("another.json", b"x"), "budget")[1])

    def test_malformed_locator_cannot_reuse_a_stringified_cache_key(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ref = reference("['record']", b"payload")
            (root / ref["locator"]).write_bytes(b"payload")
            evidence = gate._Evidence(root)
            self.assertEqual((b"payload", None), evidence.read(ref, "valid"))
            self.assertIsNotNone(evidence.read({**ref, "locator": ["record"]}, "malformed")[1])

    def test_report_snapshot_refuses_links_and_size_before_open(self):
        from tools.external_beta.full_product_report import _read_regular_file, FullProductReportError
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "report.json"
            target.write_bytes(b"{}")
            link = root / "link.json"
            link.symlink_to(target)
            with mock.patch("tools.external_beta.full_product_report.os.open", side_effect=AssertionError("must not open")):
                with self.assertRaisesRegex(FullProductReportError, "regular file"):
                    _read_regular_file(link, label="report", maximum_bytes=64)
                with self.assertRaisesRegex(FullProductReportError, "byte limit"):
                    _read_regular_file(target, label="report", maximum_bytes=1)

    def test_malformed_resource_and_incompatibility_types_refuse_without_crashing(self):
        from tests.external_beta.full_product_typed_fixture import released_resource
        entry = released_resource("fixture.bank", "sample-real", ["ja"], ["classical"])
        for key, value in (("resourceKind", []), ("bindings", [{"kind": []}]), ("dependencies", [{"id": []}])):
            with self.subTest(key=key):
                self.assertTrue(gate.released_resources({"scope": {"releasedResources": [entry | {key: value}]}})[1])
        contract = {"cases": [{"id": "case"}], "scope": {"declaredIncompatibilities": [
            {"caseId": [], "dimension": [], "value": {}, "reason": "invalid"}]}}
        self.assertTrue(gate.declared_incompatibilities(contract)[1])
        self.assertFalse(gate._number(10 ** 1000))


if __name__ == "__main__":
    unittest.main()
