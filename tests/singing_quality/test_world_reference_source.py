"""Source-only reference admission; no network calls in these tests."""
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools.singing_quality import prepare_world_reference as source


class WorldReferenceSourceTests(unittest.TestCase):
    def test_guard_changes_only_d4c_and_retains_exact_patch_and_license(self):
        lock, _ = source.load_lock()
        upstream = {item["path"]: item for item in source.expected_files(lock, "upstream")}
        guarded = {item["path"]: item for item in source.expected_files(lock, "openutau-d4c-guard")}
        self.assertEqual(28, len(guarded))
        self.assertEqual(["src/d4c.cpp"], [name for name in upstream if upstream[name] != guarded[name]])
        self.assertEqual("daba7824bfe790455a2d37770254fbcea535fae67b5d32b1d739298f5ec7fd89",
                         guarded["OPENUTAU_WORLD_FULL_REFERENCE.patch"]["sha256"])
        self.assertEqual("e3c30ca24cf523f339108db3b62b181e7bff48e46f14e4abd8d49aa3233a581a",
                         guarded["OPENUTAU_D4C_GUARD.patch"]["sha256"])
        with self.assertRaisesRegex(ValueError, "Unknown"):
            source.expected_files(lock, "silently-patched")

    def test_guard_refuses_unpinned_patch_before_creating_destination(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(source, "verify", return_value={}):
            root = Path(directory)
            patch = root / "patch"
            notice = root / "notice"
            patch.write_bytes(b"unexpected patch")
            notice.write_bytes(b"notice")
            with self.assertRaisesRegex(ValueError, "size mismatch"):
                source.prepare_guard(root, root / "new", patch, notice)
            self.assertFalse((root / "new").exists())

    def test_committed_lock_has_only_pinned_source_and_license(self):
        lock, digest = source.load_lock()
        self.assertEqual(25, len(lock["files"]))
        self.assertEqual(64, len(digest))
        self.assertEqual(source.REVISION, lock["revision"])
        self.assertTrue(all(item["path"] == "LICENSE.txt" or
                            item["path"].endswith((".cpp", ".h")) for item in lock["files"]))

    def test_size_and_git_blob_are_both_required(self):
        raw = b"reference source\n"
        item = {"path": "src/test.cpp", "size": len(raw),
                "gitBlobSha1": hashlib.sha1(b"blob 17\0" + raw).hexdigest()}
        self.assertEqual(hashlib.sha256(raw).hexdigest(), source.validate_bytes(raw, item))
        with self.assertRaisesRegex(ValueError, "size mismatch"):
            source.validate_bytes(raw + b"x", item)
        with self.assertRaisesRegex(ValueError, "blob mismatch"):
            source.validate_bytes(b"Reference source\n", item)
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            source.validate_bytes(raw, dict(item, sha256="0" * 64))
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         source.validate_bytes(raw, dict(item, sha256=hashlib.sha256(raw).hexdigest())))

    def test_fetch_refuses_existing_directory_before_network(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(source.urllib.request, "urlopen") as request:
            with self.assertRaises(FileExistsError):
                source.fetch(Path(directory))
            request.assert_not_called()

    def test_partial_and_symlink_source_do_not_verify(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, "missing"):
                source.verify(root)
            real = root / "notice"
            real.write_bytes(b"not the license")
            (root / "LICENSE.txt").symlink_to(real)
            with self.assertRaisesRegex(ValueError, "symlinks"):
                source.verify(root)

    def test_modified_source_cannot_inherit_a_manifest_identity(self):
        raw = b"a"
        lock = {"files": [{"path": "LICENSE.txt", "size": 1,
                            "gitBlobSha1": hashlib.sha1(b"blob 1\0a").hexdigest()}]}
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(source, "load_lock", return_value=(lock, "b" * 64)):
            target = Path(directory) / "LICENSE.txt"
            target.write_bytes(raw)
            self.assertEqual(1, len(source.verify(Path(directory))["files"]))
            target.write_bytes(b"c")
            with self.assertRaisesRegex(ValueError, "blob mismatch"):
                source.verify(Path(directory))


if __name__ == "__main__":
    unittest.main()
