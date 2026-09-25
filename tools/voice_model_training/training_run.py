"""Connect fresh reviewed dataset admission to a complete CPU training epoch.

This service consumes caller-selected, captured policies and target metadata.
It is not a public checkpoint importer or a release-quality approval mechanism.
Any exception invalidates the in-memory training attempt: discard it, do not
continue optimizing partially updated parameters as if the epoch succeeded.
"""
from copy import deepcopy
from pathlib import Path
import time

from .__main__ import assemble_dataset
from .batches import iter_supervised_batches
from .checkpoint import publish_checkpoint
from .conditioning import validate_conditioning_bindings
from .optimization import run_acoustic_epoch


def train_reviewed_epoch(model, optimizer, *, dataset_inputs: dict,
                         conditioning_directory: Path, targets: dict,
                         expected_profile_sha256: str, output: Path,
                         run_metadata: dict, maximum_updates: int,
                         maximum_seconds: float = 600, cancelled=None,
                         objective=None, objective_id="mel-l1",
                         expected_dataset_sha256: str | None = None,
                         expected_conditioning_bindings: dict | None = None,
                         maximum_checkpoint_bytes: int = 512 * 1024 * 1024,
                         on_step=None) -> dict:
    """Train whole phrases (at most 4096 frames) and publish only after rechecks.

    The caller must own stable model/optimizer state, trusted architecture code,
    independently selected current policy anchors and acoustic target metadata.
    Cancellation is cooperative. No directory-fsync/power-loss guarantee or
    hostile-parent-directory race protection is asserted by this service.
    """
    required = {"permission_config", "permission_hash", "label_config", "label_hash",
                "root", "rights_review", "rights_policy", "rights_anchor",
                "label_review", "label_policy", "label_anchor", "seed", "held_out_songs"}
    optional = {"derived_segments", "fresh_pitch_extractor", "fresh_pitch_extractor_sha256"}
    if (not isinstance(dataset_inputs, dict) or not required <= set(dataset_inputs)
            or set(dataset_inputs) - required - optional
            or (("fresh_pitch_extractor" in dataset_inputs)
                != ("fresh_pitch_extractor_sha256" in dataset_inputs))):
        raise ValueError("Training requires the complete captured dataset admission inputs")
    if (not isinstance(run_metadata, dict) or (cancelled is not None and not callable(cancelled))
            or (on_step is not None and not callable(on_step))):
        raise ValueError("Invalid training metadata or cancellation callback")
    if expected_conditioning_bindings is not None and (expected_dataset_sha256 is None
                                                      or not isinstance(expected_conditioning_bindings, dict)):
        raise ValueError("Conditioning bindings require a captured dataset identity to compare against")
    if type(maximum_checkpoint_bytes) is not int or not 1 <= maximum_checkpoint_bytes <= 512 * 1024 * 1024:
        raise ValueError("Invalid checkpoint byte budget")
    output, conditioning_directory = Path(output), Path(conditioning_directory)
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError("Training checkpoint requires a new output directory in an existing parent")
    inputs, metadata, target_inventory = deepcopy(dataset_inputs), deepcopy(run_metadata), deepcopy(targets)

    def check_cancelled():
        if cancelled is not None and cancelled():
            raise RuntimeError("Reviewed training cancelled; discard the attempt")

    def refresh():
        check_cancelled()
        snapshot = assemble_dataset(**inputs, now=int(time.time()),
                                    conditioning_directory=conditioning_directory, reuse_conditioning=True)
        if (snapshot.get("schemaVersion") != 3 or snapshot.get("preparationIssues") != []
                or snapshot.get("sourcePermissionsAdmitted") is not True
                or snapshot.get("labelsAdmitted") is not True):
            raise ValueError("Reviewed training requires admitted sources, labels and complete partitions")
        if time.time() >= snapshot["expiresAt"]:
            raise ValueError("Dataset review expired during training admission")
        check_cancelled()
        return snapshot

    snapshot = refresh()
    configuration = run_metadata.get("configuration", {})
    breathiness_enabled = configuration.get("use_breathiness_embed", False) if isinstance(configuration, dict) else False
    if type(breathiness_enabled) is not bool:
        raise ValueError("Training run breathiness embedding declaration must be boolean")
    controls = [row.get("conditioningControls") for row in snapshot["conditioning"]]
    revisions = [row.get("conditioningRevision") for row in snapshot["conditioning"]]
    if breathiness_enabled:
        if any(value != ["breathiness"] for value in controls) or any(value != 2 for value in revisions):
            raise ValueError("Breathiness-enabled training requires revision-2 supervision for every phrase")
    elif any(value not in (None, []) for value in controls):
        raise ValueError("Training configuration cannot silently discard captured breathiness supervision")
    if expected_dataset_sha256 is not None:
        # A warm start that adds conditioning supervision necessarily changes the
        # dataset digest, so that case is verified binding-by-binding instead of by
        # asserting an equality the approved repair can never satisfy.
        if expected_conditioning_bindings is not None:
            validate_conditioning_bindings(expected_conditioning_bindings, snapshot["bindings"])
        elif snapshot["datasetSha256"] != expected_dataset_sha256:
            raise ValueError("Resumed checkpoint dataset differs from fresh admission")
    selected = {source for group in snapshot["bindings"]["split"]["groups"]
                if group["partition"] == "train" for source in group["sourceIds"]}
    coverage = {row["sourceId"]: row["frameCount"] for row in snapshot["conditioning"]
                if row["sourceId"] in selected}
    if (not coverage or set(coverage) != selected
            or any(type(count) is not int or not 1 <= count <= 4096 for count in coverage.values())):
        raise ValueError("Whole-phrase training requires nonempty phrases of at most 4096 frames")

    def check_lifetime():
        check_cancelled()
        if time.time() >= snapshot["expiresAt"]:
            raise ValueError("Dataset review expired; discard the training attempt")
        return False

    batches = iter_supervised_batches(snapshot, conditioning_directory, target_inventory,
                                     expected_profile_sha256=expected_profile_sha256,
                                     partition="train", batch_frames=4096, context_frames=0)
    epoch = run_acoustic_epoch(model, optimizer, batches,
                              expected_dataset_sha256=snapshot["datasetSha256"],
                              expected_profile_sha256=expected_profile_sha256,
                              vocabulary_size=len(snapshot["vocabulary"]),
                              maximum_updates=maximum_updates, maximum_seconds=maximum_seconds,
                              cancelled=check_lifetime, on_step=on_step,
                              objective=objective, objective_id=objective_id,
                              expected_source_frames=coverage)

    def revalidate():
        current = refresh()
        if current["datasetSha256"] != snapshot["datasetSha256"]:
            raise ValueError("Dataset identity changed during training; discard the attempt")

    revalidate()
    return publish_checkpoint(model, optimizer, output,
                              metadata=dict(run=metadata, datasetBindings=snapshot["bindings"],
                                            datasetSha256=snapshot["datasetSha256"],
                                            profileSha256=expected_profile_sha256),
                              epoch=epoch, before_publish=revalidate, maximum_bytes=maximum_checkpoint_bytes)
