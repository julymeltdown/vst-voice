"""Copy-on-write captures preserve bytes without sharing subsequent mutations."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools.voice_model_training.clone_capture import clone_verified_file


class CloneCaptureTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.source = self.root / "source"
        self.source.write_bytes(b"original capture")
        self.destination = self.root / "capture"
        self.digest = hashlib.sha256(self.source.read_bytes()).hexdigest()

    @unittest.skipUnless(sys.platform == "darwin", "macOS clonefile required")
    def test_clone_is_independent_and_never_overwrites(self):
        clone_verified_file(self.source, self.destination, self.digest)
        self.assertEqual(self.destination.read_bytes(), self.source.read_bytes())
        self.assertNotEqual(self.destination.stat().st_ino, self.source.stat().st_ino)
        self.destination.write_bytes(b"changed destination")
        self.assertEqual(self.source.read_bytes(), b"original capture")
        self.source.write_bytes(b"changed source")
        self.assertEqual(self.destination.read_bytes(), b"changed destination")
        with self.assertRaises(ValueError):
            clone_verified_file(self.source, self.destination, self.digest)
        self.assertEqual(self.destination.read_bytes(), b"changed destination")

    @unittest.skipUnless(sys.platform == "darwin", "macOS clonefile required")
    def test_wrong_digest_retains_incomplete_capture(self):
        with self.assertRaisesRegex(ValueError, "identity differs"):
            clone_verified_file(self.source, self.destination, "0" * 64)
        self.assertEqual(self.destination.read_bytes(), self.source.read_bytes())

    @unittest.skipUnless(sys.platform == "darwin", "macOS clonefile required")
    def test_symlink_and_oversized_source_refuse_before_publication(self):
        alias = self.root / "alias"
        alias.symlink_to(self.source)
        with self.assertRaises(ValueError):
            clone_verified_file(alias, self.destination, self.digest)
        with self.assertRaises(ValueError):
            clone_verified_file(self.source, self.destination, self.digest, maximum_bytes=1)
        self.assertFalse(self.destination.exists())

    def test_platform_and_invalid_arguments_have_no_copy_fallback(self):
        with patch("tools.voice_model_training.clone_capture.sys.platform", "linux"):
            with self.assertRaisesRegex(ValueError, "macOS"):
                clone_verified_file(self.source, self.destination, self.digest)
        for limit in (0, -1, True, 64 * 1024**2 + 1):
            with self.assertRaises(ValueError):
                clone_verified_file(self.source, self.destination, self.digest, limit)
        with self.assertRaises(ValueError):
            clone_verified_file(self.source, self.destination, "invalid")
        self.assertFalse(self.destination.exists())
