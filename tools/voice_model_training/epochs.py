"""Bounded, recoverable multi-epoch execution for an owned CPU training model."""
import hashlib
import math
from pathlib import Path
import time

from .__main__ import encode_report, publish_new, verify_exact_file
from .training_run import train_reviewed_epoch


def run_reviewed_epochs(model, optimizer, *, output: Path, epochs: int,
                        completed_epochs: int, parent_receipt_sha256: str | None,
                        metadata: dict, epoch_options: dict,
                        maximum_run_seconds: float = 3600,
                        maximum_total_checkpoint_bytes: int = 2 * 1024**3) -> dict:
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
            or not 1 <= maximum_total_checkpoint_bytes <= 8 * 1024**3):
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
    total_bytes, summaries = 0, []
    for index in range(epochs):
        remaining = maximum_total_checkpoint_bytes - total_bytes
        if remaining <= 0 or expired():
            raise RuntimeError("Training run budget exhausted; earlier checkpoints retained")
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
        options["expected_dataset_sha256"] = receipt["metadata"]["datasetSha256"]
        summaries.append(dict(path=epoch_output.name if epochs > 1 else ".",
                              receiptSha256=parent_receipt_sha256,
                              checkpointSha256=receipt["checkpointSha256"],
                              updates=receipt["epoch"]["updates"], meanLoss=receipt["epoch"]["meanLoss"]))
    if expired():
        raise RuntimeError("Training run deadline exceeded; completed checkpoints retained")
    result = dict(formatId="com.project-seam.training-run", schemaVersion=1,
                  requestedEpochs=epochs, completedEpochs=completed_epochs + epochs,
                  checkpointBytes=total_bytes, checkpoints=summaries,
                  checkpointSha256=receipt["checkpointSha256"], epoch=receipt["epoch"],
                  singerQualified=False, releaseEligible=False)
    if epochs > 1:
        publish_new(output / "run.json", result)
    return result
