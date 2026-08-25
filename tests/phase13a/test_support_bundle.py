import io
import tempfile
import unittest
import zipfile
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))

import support_bundle  # noqa: E402


class SupportBundleTests(unittest.TestCase):
    def event(self):
        return {
            "code": "RENDER_TIMEOUT",
            "severity": "warning",
            "fields": {
                "buildId": "external-beta.20260821.1",
                "sourceCommit": "a" * 40,
                "artifactId": "ProjectSEAMEditor",
                "artifactSha256": "b" * 64,
                "bankId": "beta.voicebank",
                "bankVersion": "1.0.0",
                "osFamily": "macOS",
                "osMajor": 15,
                "hostFamily": "standalone",
                "hostMajor": 1,
                "deviceFamily": "coreaudio",
                "sampleRate": 48000,
                "bufferFrames": 256,
                "channels": 2,
                "xrunCount": 0,
                "sanitizedStackSymbols": ["RenderCoordinator::submit"],
            },
        }

    def test_default_bundle_excludes_sensitive_fields_and_preview_matches_export(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "support.zip"
            preview = support_bundle.preview_export_bundle([self.event()])
            actual = support_bundle.write_export_bundle(destination, [self.event()])
            self.assertEqual(preview["archiveSha256"], actual["archiveSha256"])
            with zipfile.ZipFile(destination) as archive:
                names = archive.namelist()
                self.assertEqual(["manifest.json", "diagnostics.json"], names)
                payload = b"".join(archive.read(name) for name in names)
                self.assertNotIn(b"/Users/", payload)
                self.assertNotIn(b"lyrics", payload.lower())
                self.assertNotIn(b"rawLog", payload)

    def test_forbidden_event_field_and_unconsented_attachment_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(ValueError):
                support_bundle.build_export_bundle([{**self.event(), "rawLog": "secret"}])
            attachment = root / "sentinel.txt"
            attachment.write_text("user-selected", encoding="utf-8")
            with self.assertRaises(ValueError):
                support_bundle.build_export_bundle([self.event()], attachments=[attachment])
            archive, preview = support_bundle.build_export_bundle([self.event()], attachments=[attachment], consent=True)
            self.assertEqual(preview["archiveSha256"], __import__("hashlib").sha256(archive).hexdigest())
            with zipfile.ZipFile(io.BytesIO(archive)) as zip_file:
                self.assertEqual(b"user-selected", zip_file.read("attachments/sentinel.txt"))

    def test_private_report_delete_cannot_escape_owned_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "private"
            report = support_bundle.write_private_report(root, {"reportId": "r1", "codes": ["E"]})
            self.assertTrue(report.exists())
            support_bundle.delete_owned_report(report, root)
            self.assertFalse(report.exists())
            outside = Path(directory) / "outside.json"
            outside.write_text("keep", encoding="utf-8")
            with self.assertRaises(ValueError):
                support_bundle.delete_owned_report(outside, root)


if __name__ == "__main__":
    unittest.main()
