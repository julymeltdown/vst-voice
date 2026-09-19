"""Verify and prune only binary files of completed checkpoints owned by this run."""
import hashlib
import os
from pathlib import Path
import stat

from .__main__ import load_config, publish_new
from .gan_checkpoint_storage import LIMIT


def verified_checkpoint_files(directory, receipt_sha256):
    directory = Path(directory)
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError("Retention requires a real checkpoint directory")
    receipt = load_config(directory / "checkpoint.json", receipt_sha256)
    if not isinstance(receipt, dict):
        raise ValueError("Retention requires a checkpoint receipt object")
    records = receipt.get("files")
    if (receipt.get("formatId") != "com.project-seam.gan-checkpoint"
            or not isinstance(receipt.get("epoch"), dict)
            or receipt.get("epoch", {}).get("epochComplete") is not True
            or receipt.get("epoch", {}).get("coverageVerified") is not True
            or not isinstance(records, list) or len(records) != 2):
        raise ValueError("Retention requires a complete two-file GAN checkpoint")
    paths = []
    for record, name in zip(records, ("models.pt", "training.pt")):
        if (not isinstance(record, dict) or record.get("path") != name
                or type(record.get("bytes")) is not int or not 1 <= record["bytes"] <= LIMIT):
            raise ValueError("Invalid retention file record")
        path = directory / name
        if path.is_symlink():
            raise ValueError("Retention cannot follow a checkpoint symlink")
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
        with os.fdopen(os.open(path, flags), "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size != record["bytes"]:
                raise ValueError("Retention checkpoint file type or size differs")
            digest = hashlib.sha256()
            remaining = record["bytes"]
            while remaining:
                chunk = stream.read(min(remaining, 1024 * 1024))
                if not chunk:
                    raise ValueError("Retention checkpoint file is truncated")
                digest.update(chunk)
                remaining -= len(chunk)
            if stream.read(1) or digest.hexdigest() != record.get("sha256"):
                raise ValueError("Retention checkpoint file bytes differ")
        paths.append(path)
    if receipt.get("checkpointBytes") != sum(record["bytes"] for record in records):
        raise ValueError("Retention checkpoint total differs")
    return receipt, paths


def prune_checkpoint_binaries(directory, receipt_sha256):
    """Caller verifies a newer checkpoint first; preserve original receipt and audio."""
    receipt, paths = verified_checkpoint_files(directory, receipt_sha256)
    # Check both files before removing either. No recursive removal or paths from
    # the receipt: the only deletion targets are the two fixed binary filenames.
    for path in paths:
        path.unlink()
    publish_new(Path(directory) / "pruned-binaries.json", dict(
        formatId="com.project-seam.vocoder-pruned-binaries", schemaVersion=1,
        receiptSha256=receipt_sha256, removedBytes=receipt["checkpointBytes"],
        resumable=False))
    return receipt["checkpointBytes"]
