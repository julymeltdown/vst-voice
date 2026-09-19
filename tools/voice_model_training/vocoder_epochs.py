"""Bounded multi-epoch GAN runs retaining only genuine epoch completion receipts.

Failure or cancellation invalidates the in-memory attempt. Resume from a completed
child or an explicitly selected partial checkpoint after fresh admission; this wrapper does not
grant source permission, certify model quality, or import checkpoint state.
"""
from copy import deepcopy
import hashlib
import math
from pathlib import Path
import re
import time

from .__main__ import encode_report, publish_new, verify_exact_file
from .gan_checkpoint_storage import LIMIT
from .vocoder_training_run import train_reviewed_vocoder_epoch
from .vocoder_retention import prune_checkpoint_binaries, verified_checkpoint_files


def run_reviewed_vocoder_epochs(generator, discriminators, generator_optimizer, discriminator_optimizer,
        *, output: Path, epochs: int, completed_epochs: int = 0,
        parent_receipt_sha256: str | None = None, metadata: dict, epoch_options: dict,
        maximum_run_seconds: float = 3600,
        maximum_total_checkpoint_bytes: int = 2 * 1024**3, cancelled=None,
        retain_checkpoints: int | None = None, on_progress=None,
        checkpoint_interval_updates=None, maximum_recovery_bytes=2 * 1024**3, retain_partial_checkpoints=None,
        resume_partial=None, resume_partial_sha256=None) -> dict:
    """Publish each complete epoch into a new child of a new run directory.

Checkpoint binary bytes, including partial files from a failed final attempt,
are bounded in aggregate. JSON receipts and held-out WAVs are excluded from that
budget. Deadline and cancellation are cooperative between epoch operations and
publication phases, starting here after model initialization. A run.json is
published only after every requested epoch completes. Reconstruction outputs,
when requested by held_out_items, use separate reconstruction-NNNNNN siblings.

Opt-in retain_checkpoints keeps the newest N binary checkpoints from this new
run, retaining every original receipt and reconstruction. Pruning occurs only
after a newer checkpoint is published and its binary hashes are verified. The
byte budget includes this temporary N+1 peak. External resume inputs are never
pruned. Without retention the original cumulative budget behavior is unchanged.

Partial retention keeps N recovery checkpoints while each epoch runs, then retires
those owned binaries after verified complete publication. Receipts remain; written
bytes and currently retained bytes are reported separately across the run.

epoch_options is forwarded to train_reviewed_vocoder_epoch, except output,
run_metadata and reconstruction_directory are owned here. Its cancellation
callback is combined with the run callback and deadline; its per-epoch checkpoint
total and time limits are capped by the remaining run budgets.
"""
    valid_digest = lambda value: isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None
    if retain_partial_checkpoints is not None and (checkpoint_interval_updates is None
            or type(retain_partial_checkpoints) is not int or not 1 <= retain_partial_checkpoints <= 1000):
        raise ValueError("Partial retention requires periodic recovery and a bounded count")
    if (type(epochs) is not int or not 1 <= epochs <= 1000
            or type(completed_epochs) is not int or not 0 <= completed_epochs < 100000
            or completed_epochs + epochs > 100000
            or (parent_receipt_sha256 is not None if completed_epochs == 0 else not valid_digest(parent_receipt_sha256))
            or type(maximum_run_seconds) not in (int, float) or not math.isfinite(maximum_run_seconds)
            or not 0 < maximum_run_seconds <= 86400
            or type(maximum_total_checkpoint_bytes) is not int
            or not 1 <= maximum_total_checkpoint_bytes <= 8 * 1024**3
            or retain_checkpoints is not None and
               (type(retain_checkpoints) is not int or not 1 <= retain_checkpoints <= 1000)
            or not isinstance(metadata, dict) or not isinstance(epoch_options, dict)
            or on_progress is not None and not callable(on_progress)
            or checkpoint_interval_updates is not None and (type(checkpoint_interval_updates) is not int
                                                             or not 1 <= checkpoint_interval_updates <= 100000)
            or type(maximum_recovery_bytes) is not int or not 1 <= maximum_recovery_bytes <= 8 * 1024**3
            or (resume_partial is None) != (resume_partial_sha256 is None)
            or resume_partial is not None and not valid_digest(resume_partial_sha256)
            or cancelled is not None and not callable(cancelled)):
        raise ValueError("Invalid vocoder training run limits or lineage")
    if {"output", "run_metadata", "reconstruction_directory", "on_progress", "recovery_directory",
        "checkpoint_interval_updates", "maximum_recovery_bytes", "retain_partial_checkpoints", "resume_partial", "resume_partial_sha256"} & set(epoch_options):
        raise ValueError("Vocoder epoch output and lineage are owned by the run")
    options, metadata = dict(epoch_options), deepcopy(metadata)
    epoch_seconds = options.get("maximum_seconds", 600)
    file_limit = options.get("maximum_checkpoint_file_bytes", LIMIT)
    total_limit = options.get("maximum_checkpoint_total_bytes", 2 * LIMIT)
    epoch_cancelled = options.get("cancelled")
    if (type(epoch_seconds) not in (int, float) or not math.isfinite(epoch_seconds)
            or not 0 < epoch_seconds <= 86400
            or type(file_limit) is not int or not 1 <= file_limit <= LIMIT
            or type(total_limit) is not int or not 1 <= total_limit <= 2 * LIMIT
            or epoch_cancelled is not None and not callable(epoch_cancelled)):
        raise ValueError("Invalid vocoder epoch resource bounds")
    output = Path(output)
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError("Vocoder training run output must be new with an existing parent")
    started = time.monotonic()
    deadline = started + maximum_run_seconds

    def report(stage, **values):
        if on_progress is not None:
            on_progress(dict(stage=stage, runElapsedSeconds=time.monotonic() - started, **values))

    def stopped():
        return (time.monotonic() >= deadline
                or cancelled is not None and cancelled()
                or epoch_cancelled is not None and epoch_cancelled())

    def check_running():
        if stopped():
            raise RuntimeError("Vocoder training run cancelled or deadline exceeded; completed checkpoints retained")

    check_running()
    output.mkdir(mode=0o700)
    total_bytes, summaries = 0, []
    written_bytes, retained = 0, []
    recovery_bytes = 0
    recovery_written = 0
    initial_parent = parent_receipt_sha256
    report("run-started", startingCompletedEpochs=completed_epochs, requestedEpochs=epochs)
    for index in range(epochs):
        check_running()
        remaining = maximum_total_checkpoint_bytes - total_bytes
        if remaining <= 0:
            raise RuntimeError("Vocoder training run checkpoint budget exhausted; completed checkpoints retained")
        number = completed_epochs + index + 1
        epoch_output = output / f"epoch-{number:06d}"
        reconstruction = None
        if options.get("held_out_items"):
            reconstruction = output / f"reconstruction-{number:06d}"
            reconstruction.mkdir(mode=0o700)
        seconds = min(epoch_seconds, deadline - time.monotonic())
        if seconds <= 0:
            raise RuntimeError("Vocoder training run deadline exceeded; completed checkpoints retained")
        epoch_metadata = dict(metadata, completedEpochs=number, parentReceiptSha256=parent_receipt_sha256)
        allowed_bytes = min(remaining, total_limit)
        report("epoch-started", epoch=number, remainingCheckpointBytes=remaining)
        epoch_recovery_bytes = 0
        epoch_recovery_written = 0
        def epoch_progress(event):
            nonlocal epoch_recovery_bytes, epoch_recovery_written
            if event["stage"] in ("partial-checkpoint", "partial-retention-completed"):
                count = event.get("recoveryBytes")
                floor = 0 if retain_partial_checkpoints is not None else epoch_recovery_bytes
                written = event.get("recoveryWrittenBytes", count)
                if (type(count) is not int or not floor <= count <= maximum_recovery_bytes - recovery_bytes
                        or type(written) is not int or written < max(count, epoch_recovery_written)):
                    raise ValueError("Recovery progress exceeds run budget")
                epoch_recovery_bytes = count
                epoch_recovery_written = written
            report(event["stage"], epoch=number, **{key: value for key, value in event.items() if key != "stage"})
        recovery_options = {}
        if checkpoint_interval_updates is not None:
            if maximum_recovery_bytes - recovery_bytes <= 0:
                raise RuntimeError("Vocoder run recovery budget exhausted; existing checkpoints retained")
            recovery_options.update(recovery_directory=output / f"recovery-{number:06d}",
                checkpoint_interval_updates=checkpoint_interval_updates,
                maximum_recovery_bytes=maximum_recovery_bytes - recovery_bytes)
            if retain_partial_checkpoints is not None:
                recovery_options["retain_partial_checkpoints"] = retain_partial_checkpoints
        if index == 0 and resume_partial is not None:
            recovery_options.update(resume_partial=resume_partial, resume_partial_sha256=resume_partial_sha256)
        try:
            receipt = train_reviewed_vocoder_epoch(generator, discriminators, generator_optimizer, discriminator_optimizer,
                output=epoch_output, run_metadata=epoch_metadata, reconstruction_directory=reconstruction,
                on_progress=epoch_progress if on_progress is not None or checkpoint_interval_updates is not None else None,
                **recovery_options,
                **(options | dict(maximum_seconds=seconds, cancelled=stopped,
                                maximum_checkpoint_file_bytes=file_limit, maximum_checkpoint_total_bytes=allowed_bytes)))
        except (Exception, KeyboardInterrupt) as error:
            report("epoch-failed", epoch=number, errorType=type(error).__name__)
            raise
        recovery_bytes += epoch_recovery_bytes
        recovery_written += epoch_recovery_written
        payload = encode_report(receipt)
        verify_exact_file(epoch_output / "checkpoint.json", payload)
        if (type(receipt.get("checkpointBytes")) is not int or not 1 <= receipt["checkpointBytes"] <= allowed_bytes
                or not valid_digest(receipt.get("checkpointSha256"))
                or not isinstance(receipt.get("metadata"), dict)
                or receipt["metadata"].get("run") != epoch_metadata
                or not valid_digest(receipt["metadata"].get("datasetSha256"))
                or options.get("expected_dataset_sha256") is not None
                   and receipt["metadata"]["datasetSha256"] != options["expected_dataset_sha256"]
                or not isinstance(receipt.get("epoch"), dict)
                or receipt["epoch"].get("epochComplete") is not True
                or receipt["epoch"].get("coverageVerified") is not True):
            raise ValueError("Vocoder epoch completion receipt differs from run identity or budget")
        parent_receipt_sha256 = hashlib.sha256(payload).hexdigest()
        total_bytes += receipt["checkpointBytes"]
        written_bytes += receipt["checkpointBytes"]
        options["expected_dataset_sha256"] = receipt["metadata"]["datasetSha256"]
        summaries.append(dict(path=epoch_output.name,
            reconstructionDirectory=reconstruction.name if reconstruction is not None else None,
            receiptSha256=parent_receipt_sha256, checkpointSha256=receipt["checkpointSha256"],
            checkpointBytes=receipt["checkpointBytes"], completedEpochs=number,
            updates=receipt["epoch"]["updates"], meanGeneratorLoss=receipt["epoch"]["meanGeneratorLoss"],
            meanDiscriminatorLoss=receipt["epoch"]["meanDiscriminatorLoss"]))
        if retain_checkpoints is not None:
            verified_checkpoint_files(epoch_output, parent_receipt_sha256)
            summaries[-1]["binariesRetained"] = True
            retained.append(summaries[-1])
            if len(retained) > retain_checkpoints:
                old = retained[0]
                total_bytes -= prune_checkpoint_binaries(output / old["path"], old["receiptSha256"])
                old["binariesRetained"] = False
                retained.pop(0)
        report("epoch-completed", epoch=number, receiptSha256=parent_receipt_sha256,
               retainedCheckpointBytes=total_bytes, writtenCheckpointBytes=written_bytes)
    check_running()
    result = dict(formatId="com.project-seam.vocoder-training-run", schemaVersion=1,
        requestedEpochs=epochs, startingCompletedEpochs=completed_epochs, completedEpochs=completed_epochs + epochs,
        parentReceiptSha256=initial_parent, checkpointBytes=total_bytes, checkpoints=summaries,
        checkpointSha256=receipt["checkpointSha256"], receiptSha256=parent_receipt_sha256, epoch=receipt["epoch"],
        trainingAdmitted=False, singerQualified=False, releaseEligible=False)
    if retain_checkpoints is not None:
        result.update(schemaVersion=2, retainCheckpoints=retain_checkpoints,
                      writtenCheckpointBytes=written_bytes)
    if checkpoint_interval_updates is not None or resume_partial is not None:
        result.update(schemaVersion=3, recoveryCheckpointBytes=recovery_bytes,
                      resumedPartialReceiptSha256=resume_partial_sha256)
    if retain_partial_checkpoints is not None:
        result.update(schemaVersion=4, retainPartialCheckpoints=retain_partial_checkpoints,
                      recoveryWrittenBytes=recovery_written)
    publish_new(output / "run.json", result)
    report("run-completed", completedEpochs=completed_epochs + epochs)
    return result
