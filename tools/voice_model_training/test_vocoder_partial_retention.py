"""Retention verifies serialized bytes without deserializing Torch state."""
import hashlib
from pathlib import Path
import tempfile
import unittest

from .__main__ import encode_report, publish_new
from .vocoder_recovery_cursor import build_recovery_plan, partial_cursor
from .vocoder_retention import prune_superseded_partial, verified_checkpoint_files


class PartialRetentionTests(unittest.TestCase):
    def fixture(self, root):
        run = dict(completedEpochs=1, parentReceiptSha256=None)
        plan = build_recovery_plan([dict(sourceId="song", analysisFrames=48, sourceSamples=96)],
            dataset_sha256="a"*64, profile_sha256="b"*64,
            run_sha256=hashlib.sha256(encode_report(run)).hexdigest(), segment_frames=16, hop_size=2)
        outputs = []
        for update in (1, 2):
            directory = root / f"update-{update:06d}"
            directory.mkdir()
            records = []
            for name in ("models.pt", "training.pt"):
                data = f"{update}-{name}".encode()
                (directory / name).write_bytes(data)
                records.append(dict(path=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
            receipt = dict(formatId="com.project-seam.gan-partial-checkpoint",
                metadata=dict(run=run, datasetSha256="a"*64, profileSha256="b"*64),
                epoch=partial_cursor(plan, completed_updates=update, generator_loss_sum=1., discriminator_loss_sum=2.),
                files=records, checkpointBytes=sum(r["bytes"] for r in records))
            publish_new(directory / "checkpoint.json", receipt)
            outputs.append((directory, hashlib.sha256(encode_report(receipt)).hexdigest()))
        return plan, outputs

    def test_verified_successor_prunes_only_older_binaries(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            plan, ((old, oh), (new, nh)) = self.fixture(root)
            with self.assertRaises(ValueError):
                verified_checkpoint_files(old, oh)
            removed = prune_superseded_partial(root, old, oh, new, nh, recovery_plan=plan)
            self.assertGreater(removed, 0)
            self.assertEqual({p.name for p in old.iterdir()}, {"checkpoint.json", "pruned-binaries.json"})
            self.assertTrue((new / "models.pt").is_file())
            self.assertTrue((new / "training.pt").is_file())

    def test_corrupt_successor_preserves_predecessor(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            plan, ((old, oh), (new, nh)) = self.fixture(root)
            (new / "training.pt").write_bytes(b"corrupt")
            with self.assertRaises(ValueError):
                prune_superseded_partial(root, old, oh, new, nh, recovery_plan=plan)
            self.assertTrue((old / "models.pt").is_file())
            self.assertTrue((old / "training.pt").is_file())

    def test_reverse_and_external_root_refuse_without_deletion(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            plan, ((old, oh), (new, nh)) = self.fixture(root)
            for owner, first, fh, second, sh in ((root, new, nh, old, oh), (root.parent, old, oh, new, nh)):
                with self.assertRaises(ValueError):
                    prune_superseded_partial(owner, first, fh, second, sh, recovery_plan=plan)
            self.assertTrue((old / "models.pt").is_file())
            self.assertTrue((new / "models.pt").is_file())
