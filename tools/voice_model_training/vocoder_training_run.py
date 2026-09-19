"""Freshly reviewed whole-phrase vocoder GAN epoch and final checkpoint publication.

Exceptions invalidate the entire in-memory attempt, including a partially updated
GAN. Discard those objects; only a previously completed checkpoint may be resumed.
"""
from copy import deepcopy
import math
from pathlib import Path
import time

from .__main__ import assemble_dataset
from .vocoder_batches import iter_vocoder_batches
from .vocoder_checkpoint import publish_vocoder_checkpoint, _schedulers
from .vocoder_optimization import vocoder_gan_step
from .vocoder_reconstruction import evaluate_held_out_reconstruction


def train_reviewed_vocoder_epoch(generator, discriminators, generator_optimizer, discriminator_optimizer,
        *, dataset_inputs, conditioning_directory, targets, pcm_sources, expected_profile_sha256,
        output, run_metadata, reconstruction_loss, objective_id, maximum_updates,
        maximum_seconds=600, cancelled=None, schedulers=None, expected_dataset_sha256=None,
        maximum_checkpoint_file_bytes=512 * 1024 * 1024, held_out_items=None,
        label_origin=None, reconstruction_directory=None, evaluation_seed=0,
        pitch_executable=None,
        maximum_checkpoint_total_bytes=1024 * 1024 * 1024):
    """Use the same admitted phrase segmentation as acoustic training (<=4096 hops).

The reconstruction callable and model/configuration provenance are caller-owned.
Schedulers, if supplied, step once after a complete epoch and are checkpointed.
Deadline/cancellation is cooperative between model updates and publication phases.
held_out_items selects source IDs, never caller-supplied audio or conditioning.
Selected validation/test phrases are loaded through the same byte-bound batch
reader as training. An optional existing reconstruction_directory retains WAVs
and item receipts; the complete measurement receipt is also checkpointed.
"""
    required = {"permission_config", "permission_hash", "label_config", "label_hash", "root",
                "rights_review", "rights_policy", "rights_anchor", "label_review", "label_policy",
                "label_anchor", "seed", "held_out_songs"}
    if not isinstance(dataset_inputs, dict) or set(dataset_inputs) != required:
        raise ValueError("Vocoder epoch requires complete captured admission inputs")
    if (not isinstance(run_metadata, dict) or not callable(reconstruction_loss)
            or not isinstance(objective_id, str) or not 1 <= len(objective_id) <= 128
            or type(maximum_updates) is not int or not 1 <= maximum_updates <= 100000
            or type(maximum_seconds) not in (int, float) or not math.isfinite(maximum_seconds)
            or not 0 < maximum_seconds <= 86400 or cancelled is not None and not callable(cancelled)
            or type(maximum_checkpoint_file_bytes) is not int
            or not 1 <= maximum_checkpoint_file_bytes <= 512 * 1024 * 1024
            or type(maximum_checkpoint_total_bytes) is not int
            or not 1 <= maximum_checkpoint_total_bytes <= 1024 * 1024 * 1024):
        raise ValueError("Invalid vocoder epoch objective or resource bounds")
    output, conditioning_directory = Path(output), Path(conditioning_directory)
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError("Vocoder epoch needs a new checkpoint directory")
    inputs, metadata = deepcopy(dataset_inputs), deepcopy(run_metadata)
    schedulers = _schedulers(schedulers, generator_optimizer, discriminator_optimizer)
    target_inventory, source_inventory = deepcopy(targets), deepcopy(pcm_sources)
    deadline = time.monotonic() + maximum_seconds

    def check_running():
        if cancelled is not None and cancelled():
            raise RuntimeError("Vocoder epoch cancelled; discard attempt")
        if time.monotonic() >= deadline:
            raise RuntimeError("Vocoder epoch deadline exceeded; discard attempt")

    def refresh():
        check_running()
        snapshot = assemble_dataset(**inputs, now=int(time.time()),
            conditioning_directory=conditioning_directory, reuse_conditioning=True)
        if (snapshot.get("schemaVersion") != 3 or snapshot.get("preparationIssues") != []
                or snapshot.get("sourcePermissionsAdmitted") is not True or snapshot.get("labelsAdmitted") is not True
                or time.time() >= snapshot["expiresAt"]):
            raise ValueError("Vocoder epoch requires current admitted sources and labels")
        check_running()
        return snapshot

    snapshot = refresh()
    if expected_dataset_sha256 is not None and snapshot["datasetSha256"] != expected_dataset_sha256:
        raise ValueError("Resumed vocoder dataset differs from fresh admission")
    selected = {source for group in snapshot["bindings"]["split"]["groups"]
                if group["partition"] == "train" for source in group["sourceIds"]}
    expected = {row["sourceId"]: row["frameCount"] for row in snapshot["sources"] if row["sourceId"] in selected}
    phrases = {row["sourceId"]: row["frameCount"] for row in snapshot["conditioning"] if row["sourceId"] in selected}
    if (not selected or set(expected) != selected or set(phrases) != selected or len(selected) > maximum_updates
            or any(type(n) is not int or not 1 <= n <= 4096 for n in phrases.values())
            or any(type(n) is not int or not 1 <= n <= 1048576 for n in expected.values())):
        raise ValueError("Vocoder epoch requires complete bounded whole phrases and enough updates")
    held_ids = set()
    partitions = {source: group["partition"] for group in snapshot["bindings"]["split"]["groups"]
                  for source in group["sourceIds"]}
    source_rows = {row["sourceId"]: row for row in snapshot["sources"]}
    phrase_rows = {row["sourceId"]: row for row in snapshot["conditioning"]}
    if held_out_items is not None:
        if (not isinstance(held_out_items, (list, tuple)) or not 1 <= len(held_out_items) <= 256
                or any(not isinstance(source, str) for source in held_out_items)
                or len(set(held_out_items)) != len(held_out_items)):
            raise ValueError("Held-out items must select distinct admitted source IDs, not supplied tensors")
        held_ids = set(held_out_items)
        for source in held_ids:
            if (source in selected or partitions.get(source) not in ("validation", "test")
                    or source not in source_rows or source not in phrase_rows or source not in target_inventory):
                raise ValueError("Held-out source must belong to an admitted non-training partition")
            count, frames = source_rows[source]["frameCount"], phrase_rows[source]["frameCount"]
            if (type(count) is not int or not 512 <= count <= 1048576
                    or type(frames) is not int or not 2 <= frames <= 4096):
                raise ValueError("Held-out reconstruction requires bounded complete phrases")
    if reconstruction_directory is not None:
        directory = Path(reconstruction_directory)
        if (not held_ids or directory.is_symlink() or not directory.is_dir()
                or directory.resolve().is_relative_to(output.resolve())):
            raise ValueError("Reconstruction directory must exist outside the new checkpoint")
    if type(evaluation_seed) is not int or not 0 <= evaluation_seed < 2**63:
        raise ValueError("Vocoder evaluation requires a nonnegative 63-bit seed")

    def check_lifetime():
        check_running()
        if time.time() >= snapshot["expiresAt"]:
            raise ValueError("Source review expired during vocoder epoch")

    covered, total, gl, dl = {}, 0, 0.0, 0.0
    for batch in iter_vocoder_batches(snapshot, conditioning_directory, target_inventory, source_inventory,
            expected_profile_sha256=expected_profile_sha256, partition="train", batch_frames=4096):
        check_lifetime()
        identity = batch["sourceId"]
        if (identity not in selected or identity in covered or batch["partition"] != "train"
                or batch["datasetSha256"] != snapshot["datasetSha256"]
                or batch["profileSha256"] != expected_profile_sha256 or batch["frameOffset"] != 0
                or batch["mel"].shape[2] != phrases[identity]
                or batch["validSamples"] != expected[identity]):
            raise ValueError("Vocoder batch identity or whole-phrase coverage differs")
        result = vocoder_gan_step(generator, discriminators, generator_optimizer, discriminator_optimizer,
            mel=batch["mel"], f0=batch["f0"], pcm=batch["pcm"], hop_size=batch["hopSize"],
            partition="train", reconstruction_loss=reconstruction_loss)
        check_lifetime()
        count = batch["validSamples"]
        covered[identity] = count
        total += count
        gl += result["generatorLoss"] * count
        dl += result["discriminatorLoss"] * count
    if covered != expected:
        raise ValueError("Vocoder epoch incomplete; no checkpoint may be published")

    def revalidate():
        current = refresh()
        if current["datasetSha256"] != snapshot["datasetSha256"]:
            raise ValueError("Dataset identity changed during vocoder epoch")

    revalidate()
    for scheduler in (schedulers or {}).values():
        scheduler.step()
    effective_label_origin = label_origin or snapshot.get("labelOrigin") or metadata.get("labelOrigin") or "unspecified"
    epoch = dict(formatId="com.project-seam.vocoder-epoch-result", schemaVersion=1,
        datasetSha256=snapshot["datasetSha256"], profileSha256=expected_profile_sha256,
        objectiveId=objective_id, updates=len(covered), sourceCount=len(covered), validSamples=total,
        meanGeneratorLoss=gl / total, meanDiscriminatorLoss=dl / total,
        coveredSourceSamples=covered, epochComplete=True, coverageVerified=True,
        labelOrigin=effective_label_origin, trainingAdmitted=False, releaseEligible=False)
    if held_ids:
        def held_out_batches():
            seen = set()
            for partition in sorted({partitions[source] for source in held_ids}):
                check_lifetime()
                for batch in iter_vocoder_batches(snapshot, conditioning_directory, target_inventory, source_inventory,
                        expected_profile_sha256=expected_profile_sha256, partition=partition, batch_frames=4096):
                    check_lifetime()
                    source = batch["sourceId"]
                    if source not in held_ids:
                        continue
                    row = source_rows[source]
                    if (source in seen or batch["partition"] != partitions[source]
                            or batch["datasetSha256"] != snapshot["datasetSha256"]
                            or batch["profileSha256"] != expected_profile_sha256
                            or batch["sourceSha256"] != row["sourceSha256"]
                            or batch["audioSha256"] != row["audioSha256"]
                            or batch["frameOffset"] != 0 or batch["validSamples"] != row["frameCount"]
                            or batch["phraseAnalysisFrames"] != phrase_rows[source]["frameCount"]
                            or batch["mel"].shape[2] != phrase_rows[source]["frameCount"]):
                        raise ValueError("Held-out batch identity or whole-phrase coverage differs")
                    seen.add(source)
                    yield batch
            if seen != held_ids:
                raise ValueError("Held-out evaluation did not cover every selected source")
        profiles = [target_inventory[source][0]["profile"] for source in sorted(held_ids)]
        if any(profile != profiles[0] for profile in profiles):
            raise ValueError("Held-out acoustic profiles differ")
        reconstruction_receipt = evaluate_held_out_reconstruction(
            generator_fn=generator,
            items=held_out_batches(),
            dataset_sha256=snapshot["datasetSha256"],
            profile_sha256=expected_profile_sha256,
            label_origin=effective_label_origin,
            profile=profiles[0], output_directory=reconstruction_directory,
            seed=evaluation_seed, check_running=check_lifetime,
            pitch_executable=pitch_executable,
        )
        epoch["reconstructionSummary"] = reconstruction_receipt.get("summary")
        epoch["reconstruction"] = reconstruction_receipt
        revalidate()
    return publish_vocoder_checkpoint(generator, discriminators, generator_optimizer, discriminator_optimizer,
        output, metadata=dict(run=metadata, datasetBindings=snapshot["bindings"],
            datasetSha256=snapshot["datasetSha256"], profileSha256=expected_profile_sha256,
            objectiveId=objective_id, labelOrigin=effective_label_origin), epoch=epoch, schedulers=schedulers,
        maximum_bytes=maximum_checkpoint_file_bytes, maximum_total_bytes=maximum_checkpoint_total_bytes,
        before_publish=revalidate)
