"""Bounded, recoverable multi-epoch execution for an owned CPU training model."""
import hashlib
import math
import os
from pathlib import Path
import shutil
import stat
import time

from .__main__ import encode_report, publish_new, verify_exact_file
from .training_run import train_reviewed_epoch


def verify_checkpoint_binary(directory, receipt):
    path = directory / "checkpoint.pt"
    if directory.is_symlink() or path.is_symlink():
        raise ValueError("Retention cannot follow checkpoint symlinks")
    size = receipt["checkpointBytes"]
    if type(size) is not int or not 1 <= size <= 512 * 1024**2:
        raise ValueError("Retention requires a bounded checkpoint size")
    digest = hashlib.sha256()
    with os.fdopen(os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
                         | getattr(os, "O_NONBLOCK", 0)), "rb") as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size != size:
            raise ValueError("Retention checkpoint size or type differs")
        remaining = size
        while remaining:
            chunk = stream.read(min(remaining, 1024**2))
            if not chunk:
                raise ValueError("Retention checkpoint truncated")
            digest.update(chunk)
            remaining -= len(chunk)
        if stream.read(1) or digest.hexdigest() != receipt["checkpointSha256"]:
            raise ValueError("Retention checkpoint bytes differ")


def run_reviewed_epochs(model, optimizer, *, output: Path, epochs: int,
                        completed_epochs: int, parent_receipt_sha256: str | None,
                        metadata: dict, epoch_options: dict,
                        maximum_run_seconds: float = 3600,
                        maximum_total_checkpoint_bytes: int = 2 * 1024**3,
                        retain_checkpoints: int | None = None,
                        minimum_free_bytes: int = 0) -> dict:
    """Retain each completed checkpoint; never turn an incomplete run into success.

    Binary output budget excludes bounded JSON receipts. Deadline is cooperative
    and starts here, after model initialization. Existing output is never reused.
    """
    if (type(epochs) is not int or not 1 <= epochs <= 1000
            or type(completed_epochs) is not int or not 0 <= completed_epochs < 100000
            or completed_epochs + epochs > 100000
            or type(maximum_run_seconds) not in (int, float) or not math.isfinite(maximum_run_seconds)
            or not 0 < maximum_run_seconds <= 86400
            or type(maximum_total_checkpoint_bytes) is not int
            or not 1 <= maximum_total_checkpoint_bytes <= 8 * 1024**3
            or retain_checkpoints is not None and (type(retain_checkpoints) is not int or not 1 <= retain_checkpoints <= 1000)
            or type(minimum_free_bytes) is not int or not 0 <= minimum_free_bytes <= 1024**4):
        raise ValueError("Invalid training run limits")
    output = Path(output)
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError("Training run output must be new with an existing parent")
    deadline = time.monotonic() + maximum_run_seconds
    def expired():
        return time.monotonic() >= deadline
    if epochs > 1:
        output.mkdir(mode=0o700)
    options = dict(epoch_options)
    total_bytes, written_bytes, summaries, retained = 0, 0, [], []
    for index in range(epochs):
        remaining = maximum_total_checkpoint_bytes - total_bytes
        if remaining <= 0 or expired():
            raise RuntimeError("Training run budget exhausted; earlier checkpoints retained")
        if minimum_free_bytes and shutil.disk_usage(output.parent).free < minimum_free_bytes + min(remaining, 512 * 1024**2):
            raise OSError("Acoustic checkpoint budget would cross the requested disk headroom")
        epoch_output = output if epochs == 1 else output / f"epoch-{completed_epochs + index + 1:06d}"
        receipt = train_reviewed_epoch(model, optimizer, output=epoch_output,
            run_metadata=dict(metadata, completedEpochs=completed_epochs + index + 1,
                              parentReceiptSha256=parent_receipt_sha256),
            **(options | dict(maximum_seconds=min(options["maximum_seconds"], deadline - time.monotonic()),
                            cancelled=expired, maximum_checkpoint_bytes=min(remaining, 512 * 1024 * 1024))))
        payload = encode_report(receipt)
        verify_exact_file(epoch_output / "checkpoint.json", payload)
        parent_receipt_sha256 = hashlib.sha256(payload).hexdigest()
        total_bytes += receipt["checkpointBytes"]
        written_bytes += receipt["checkpointBytes"]
        options["expected_dataset_sha256"] = receipt["metadata"]["datasetSha256"]
        summaries.append(dict(path=epoch_output.name if epochs > 1 else ".",
                              receiptSha256=parent_receipt_sha256,
                              checkpointSha256=receipt["checkpointSha256"],
                              updates=receipt["epoch"]["updates"], meanLoss=receipt["epoch"]["meanLoss"]))
        if retain_checkpoints is not None:
            # Only checkpoints produced in this newly created run are eligible.
            # Verify the successor before dropping any earlier binary; keep all receipts.
            verify_checkpoint_binary(epoch_output, receipt)
            retained.append((epoch_output, receipt, payload))
            summaries[-1]["binaryRetained"] = True
            if len(retained) > retain_checkpoints:
                old_output, old_receipt, old_payload = retained[0]
                verify_exact_file(old_output / "checkpoint.json", old_payload)
                verify_checkpoint_binary(old_output, old_receipt)
                publish_new(old_output / "retention.json", dict(
                    formatId="com.project-seam.checkpoint-retention", schemaVersion=1,
                    checkpointSha256=old_receipt["checkpointSha256"],
                    successorReceiptSha256=parent_receipt_sha256,
                    action="remove-binary-keep-receipt"))
                (old_output / "checkpoint.pt").unlink()
                total_bytes -= old_receipt["checkpointBytes"]
                summaries[index - retain_checkpoints]["binaryRetained"] = False
                retained.pop(0)
    if expired():
        raise RuntimeError("Training run deadline exceeded; completed checkpoints retained")
    result = dict(formatId="com.project-seam.training-run", schemaVersion=1,
                  requestedEpochs=epochs, completedEpochs=completed_epochs + epochs,
                  checkpointBytes=total_bytes, checkpoints=summaries,
                  checkpointSha256=receipt["checkpointSha256"], epoch=receipt["epoch"],
                  singerQualified=False, releaseEligible=False)
    if retain_checkpoints is not None:
        result.update(schemaVersion=2, retainCheckpoints=retain_checkpoints,
                      writtenCheckpointBytes=written_bytes)
    if epochs > 1:
        publish_new(output / "run.json", result)
    return result
