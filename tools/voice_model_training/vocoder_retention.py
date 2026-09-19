"""Verify and prune only binary files of completed checkpoints owned by this run."""
import hashlib
import os
from pathlib import Path
import stat

from .__main__ import encode_report, load_config, publish_new
from .gan_checkpoint_storage import LIMIT
from .vocoder_recovery_cursor import verify_partial_cursor


def verified_checkpoint_files(directory, receipt_sha256, *, _recovery_plan=None):
    directory = Path(directory)
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError("Retention requires a real checkpoint directory")
    receipt = load_config(directory / "checkpoint.json", receipt_sha256)
    if not isinstance(receipt, dict):
        raise ValueError("Retention requires a checkpoint receipt object")
    records = receipt.get("files")
    if _recovery_plan is not None:
        if receipt.get("formatId") != "com.project-seam.gan-partial-checkpoint":
            raise ValueError("Partial retention requires a partial checkpoint")
        verify_partial_cursor(receipt.get("epoch"), _recovery_plan)
        metadata = receipt.get("metadata", {})
        if (not isinstance(metadata, dict)
                or metadata.get("datasetSha256") != _recovery_plan["datasetSha256"]
                or metadata.get("profileSha256") != _recovery_plan["profileSha256"]
                or hashlib.sha256(encode_report(metadata.get("run"))).hexdigest() != _recovery_plan["runSha256"]):
            raise ValueError("Partial retention run identity differs")
    elif (receipt.get("formatId") != "com.project-seam.gan-checkpoint"
            or not isinstance(receipt.get("epoch"), dict)
            or receipt.get("epoch", {}).get("epochComplete") is not True
            or receipt.get("epoch", {}).get("coverageVerified") is not True
            or not isinstance(records, list) or len(records) != 2):
        raise ValueError("Retention requires a complete two-file GAN checkpoint")
    if not isinstance(records, list) or len(records) != 2:
        raise ValueError("Retention requires exactly two binary records")
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


def prune_superseded_partial(root, older, older_sha256, newer, newer_sha256, *, recovery_plan):
    """Only call with a recovery root created by the current epoch invocation.

    Verify the durable successor and both predecessor binaries before deleting
    fixed filenames. External resume directories must never be passed as root.
    """
    root, older, newer = Path(root), Path(older), Path(newer)
    if root.is_symlink() or not root.is_dir() or older.parent != root or newer.parent != root or older == newer:
        raise ValueError("Partial retention requires distinct children of the owned recovery root")
    successor, _ = verified_checkpoint_files(newer, newer_sha256, _recovery_plan=recovery_plan)
    previous, paths = verified_checkpoint_files(older, older_sha256, _recovery_plan=recovery_plan)
    for directory, receipt in ((older, previous), (newer, successor)):
        if directory.name != f"update-{receipt['epoch']['completedUpdates']:06d}":
            raise ValueError("Partial retention directory differs from its cursor")
    if (successor["epoch"]["completedUpdates"] <= previous["epoch"]["completedUpdates"]
            or successor["metadata"] != previous["metadata"]):
        raise ValueError("Partial successor must advance the same epoch state lineage")
    marker = older / "pruned-binaries.json"
    if marker.exists() or marker.is_symlink():
        raise ValueError("Partial retention marker already exists")
    for path in paths:
        path.unlink()
    publish_new(marker, dict(formatId="com.project-seam.vocoder-pruned-binaries", schemaVersion=1,
        receiptSha256=older_sha256, successorReceiptSha256=newer_sha256,
        removedBytes=previous["checkpointBytes"], resumable=False))
    return previous["checkpointBytes"]
