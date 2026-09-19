import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

from tools.voice_model_training.checkpoint import publish_checkpoint, load_local_checkpoint


@unittest.skipUnless(importlib.util.find_spec("torch"), "Optional Torch environment not installed")
class CheckpointTests(unittest.TestCase):
    def test_publish_no_overwrite_and_incomplete_attempt(self):
        import torch
        model = torch.nn.Linear(2, 1)
        optimizer = torch.optim.AdamW(model.parameters())
        epoch = dict(epochComplete=True, coverageVerified=True)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            def publish(name, **kwargs):
                return publish_checkpoint(model, optimizer, root / name, metadata=dict(syntheticInputs=True), epoch=epoch, **kwargs)
            receipt = publish("complete")
            raw = (root / "complete/checkpoint.pt").read_bytes()
            self.assertEqual(receipt["checkpointBytes"], len(raw))
            self.assertEqual(receipt["checkpointSha256"], hashlib.sha256(raw).hexdigest())
            receipt_hash = hashlib.sha256((root / "complete/checkpoint.json").read_bytes()).hexdigest()
            restored, loaded_receipt = load_local_checkpoint(root / "complete", receipt_sha256=receipt_hash)
            self.assertEqual(loaded_receipt, receipt)
            self.assertTrue(restored["metadata"]["syntheticInputs"])
            self.assertTrue(torch.equal(restored["model"]["weight"], model.weight))
            with self.assertRaises(ValueError):
                load_local_checkpoint(root / "complete", receipt_sha256="0" * 64)
            from unittest.mock import patch
            binary = root / "complete/checkpoint.pt"
            binary.write_bytes(bytes([raw[0] ^ 1]) + raw[1:])
            with patch("torch.load") as load:
                with self.assertRaises(ValueError):
                    load_local_checkpoint(root / "complete", receipt_sha256=receipt_hash)
                load.assert_not_called()
            binary.write_bytes(raw)
            with self.assertRaises(FileExistsError): publish("complete")
            self.assertEqual((root / "complete/checkpoint.pt").read_bytes(), raw)
            with self.assertRaises((ValueError, RuntimeError)): publish("too-small", maximum_bytes=1)
            self.assertFalse((root / "too-small/checkpoint.json").exists())
            def expired():
                raise ValueError("Review expired")
            with self.assertRaises(ValueError): publish("expired", before_publish=expired)
            self.assertTrue((root / "expired/checkpoint.pt").exists())
            self.assertFalse((root / "expired/checkpoint.json").exists())
            epoch["coverageVerified"] = False
            with self.assertRaises(ValueError): publish("missing-coverage")
            self.assertFalse((root / "missing-coverage").exists())

    def test_an_unreadable_write_is_refused_before_a_receipt_is_published(self):
        import torch
        model = torch.nn.Linear(2, 1)
        optimizer = torch.optim.AdamW(model.parameters())
        epoch = dict(epochComplete=True, coverageVerified=True)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            from unittest.mock import patch
            real_save = torch.save
            # Two real long runs produced archives whose central directory Torch
            # could not read, and the failure reached a later resume instead of the
            # write. Simulate the same corruption and require the write to refuse.
            def corrupting_save(state, stream):
                payload = __import__("io").BytesIO()
                real_save(state, payload)
                blob = payload.getvalue()
                stream.write(blob[:len(blob) - 108] + b"\0" * 108)
            with patch("torch.save", side_effect=corrupting_save):
                with self.assertRaises(ValueError):
                    publish_checkpoint(model, optimizer, root / "corrupt",
                                       metadata=dict(syntheticInputs=True), epoch=epoch)
            self.assertFalse((root / "corrupt/checkpoint.json").exists())
            self.assertTrue((root / "corrupt/checkpoint.pt").exists())
            # Trailing bytes are tolerated by Torch itself, so they are not a defect to
            # refuse here; the receipt simply reports the length actually written.
            def trailing_save(state, stream):
                payload = __import__("io").BytesIO()
                real_save(state, payload)
                stream.write(payload.getvalue() + b"\0" * 108)
            with patch("torch.save", side_effect=trailing_save):
                receipt = publish_checkpoint(model, optimizer, root / "trailing",
                                             metadata=dict(syntheticInputs=True), epoch=epoch)
            self.assertEqual(receipt["checkpointBytes"], (root / "trailing/checkpoint.pt").stat().st_size)
            load_local_checkpoint(root / "trailing",
                receipt_sha256=hashlib.sha256((root / "trailing/checkpoint.json").read_bytes()).hexdigest())
            # A short write is the case that actually corrupts the archive.
            def truncated_save(state, stream):
                payload = __import__("io").BytesIO()
                real_save(state, payload)
                blob = payload.getvalue()
                stream.write(blob[:len(blob) // 2])
            with patch("torch.save", side_effect=truncated_save):
                with self.assertRaises(ValueError):
                    publish_checkpoint(model, optimizer, root / "short",
                                       metadata=dict(syntheticInputs=True), epoch=epoch)
            self.assertFalse((root / "short/checkpoint.json").exists())
            # A healthy write still publishes, so the guard is not simply refusing.
            receipt = publish_checkpoint(model, optimizer, root / "healthy",
                                         metadata=dict(syntheticInputs=True), epoch=epoch)
            self.assertTrue((root / "healthy/checkpoint.json").is_file())
            load_local_checkpoint(root / "healthy",
                receipt_sha256=hashlib.sha256((root / "healthy/checkpoint.json").read_bytes()).hexdigest())
            self.assertEqual(receipt["checkpointBytes"], (root / "healthy/checkpoint.pt").stat().st_size)


if __name__ == "__main__":
    unittest.main()
