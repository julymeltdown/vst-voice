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


if __name__ == "__main__":
    unittest.main()
