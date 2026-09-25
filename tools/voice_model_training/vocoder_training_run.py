"""Freshly reviewed whole-phrase vocoder GAN epoch and final checkpoint publication.

Exceptions invalidate the entire in-memory attempt, including a partially updated
GAN. Discard those objects; resume only a verified complete or explicit partial
checkpoint after fresh dataset admission and exact prefix validation.
"""
from copy import deepcopy
import hashlib
import math
from pathlib import Path
import time

from .__main__ import assemble_dataset, encode_report, load_config
from .vocoder_batches import iter_vocoder_batches, segment_frame_ranges
from .vocoder_checkpoint import (publish_vocoder_checkpoint, publish_vocoder_partial_checkpoint,
                                restore_vocoder_partial_checkpoint, _schedulers)
from .vocoder_recovery_cursor import (advance_excitation_digest, build_recovery_plan,
                                      excitation_digest_seed, partial_cursor, verify_partial_cursor,
                                      EXCITATION_DIGEST_ALGORITHM)
from .vocoder_optimization import vocoder_gan_step
from .vocoder_reconstruction import evaluate_held_out_reconstruction
from .gan_checkpoint_storage import require_disk_headroom


def train_reviewed_vocoder_epoch(generator, discriminators, generator_optimizer, discriminator_optimizer,
        *, dataset_inputs, conditioning_directory, targets, pcm_sources, expected_profile_sha256,
        output, run_metadata, reconstruction_loss, objective_id, maximum_updates,
        maximum_seconds=600, cancelled=None, schedulers=None, expected_dataset_sha256=None,
        maximum_checkpoint_file_bytes=512 * 1024 * 1024, held_out_items=None,
        label_origin=None, reconstruction_directory=None, evaluation_seed=0,
        pitch_executable=None, on_progress=None, training_segment_frames=None,
        maximum_checkpoint_total_bytes=1024 * 1024 * 1024,
        recovery_directory=None, checkpoint_interval_updates=None,
        maximum_recovery_bytes=2 * 1024**3, retain_partial_checkpoints=None,
        resume_partial=None, resume_partial_sha256=None, excitation_noise_id=None):
    """Admit complete sources (<=4096 hops), optionally train balanced owned segments.

The reconstruction callable and model/configuration provenance are caller-owned.
Schedulers, if supplied, step once after a complete epoch and are checkpointed.
Deadline/cancellation is cooperative between model updates and publication phases.
held_out_items selects source IDs, never caller-supplied audio or conditioning.
Selected validation/test phrases are loaded through the same byte-bound batch
reader as training. An optional existing reconstruction_directory retains WAVs
and item receipts; the complete measurement receipt is also checkpointed.
Held-out inference remains whole-source even when training_segment_frames is set.
Segments introduce independent training boundaries; numerical equivalence to a
whole-phrase optimizer update is neither expected nor claimed.

Opt-in recovery writes only after both optimizers finish an owned segment. It
uses a separate new directory and aggregate byte budget. Optional retention keeps
the newest N partial binaries with space for the N+1 peak. Resume rereads the verified prefix without optimizing
it, then restores state immediately before the next update. Schedulers still step
only once at complete-epoch coverage. Saved snapshots are not admission authority.
"""
    required = {"permission_config", "permission_hash", "label_config", "label_hash", "root",
                "rights_review", "rights_policy", "rights_anchor", "label_review", "label_policy",
                "label_anchor", "seed", "held_out_songs"}
    optional = {"derived_segments", "fresh_pitch_extractor", "fresh_pitch_extractor_sha256"}
    if (not isinstance(dataset_inputs, dict) or not required <= set(dataset_inputs)
            or set(dataset_inputs) - required - optional
            or (("fresh_pitch_extractor" in dataset_inputs)
                != ("fresh_pitch_extractor_sha256" in dataset_inputs))):
        raise ValueError("Vocoder epoch requires complete captured admission inputs")
    if (not isinstance(run_metadata, dict) or not callable(reconstruction_loss)
            or not isinstance(objective_id, str) or not 1 <= len(objective_id) <= 128
            or type(maximum_updates) is not int or not 1 <= maximum_updates <= 100000
            or type(maximum_seconds) not in (int, float) or not math.isfinite(maximum_seconds)
            or not 0 < maximum_seconds <= 86400 or cancelled is not None and not callable(cancelled)
            or on_progress is not None and not callable(on_progress)
            or type(maximum_checkpoint_file_bytes) is not int
            or not 1 <= maximum_checkpoint_file_bytes <= 512 * 1024 * 1024
            or type(maximum_checkpoint_total_bytes) is not int
            or not 1 <= maximum_checkpoint_total_bytes <= 1024 * 1024 * 1024):
        raise ValueError("Invalid vocoder epoch objective or resource bounds")
    output, conditioning_directory = Path(output), Path(conditioning_directory)
    if training_segment_frames is not None and (type(training_segment_frames) is not int or
                                                not 16 <= training_segment_frames <= 4096):
        raise ValueError("Training segment budget must be 16..4096 hops")
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError("Vocoder epoch needs a new checkpoint directory")
    if ((recovery_directory is None) != (checkpoint_interval_updates is None)
            or (resume_partial is None) != (resume_partial_sha256 is None)
            or type(maximum_recovery_bytes) is not int or not 1 <= maximum_recovery_bytes <= 8 * 1024**3):
        raise ValueError("Recovery requires paired explicit paths/intervals and bounded storage")
    recovery_enabled = recovery_directory is not None or resume_partial is not None
    if retain_partial_checkpoints is not None and (recovery_directory is None
            or type(retain_partial_checkpoints) is not int or not 1 <= retain_partial_checkpoints <= 1000):
        raise ValueError("Partial retention requires periodic recovery and a bounded count")
    if recovery_enabled and training_segment_frames is None:
        raise ValueError("Partial recovery currently requires explicit balanced training segments")
    if excitation_noise_id is not None:
        from .uv_noise_excitation import EXCITATION_IDS, build_excitation_noise
        if excitation_noise_id not in EXCITATION_IDS:
            raise ValueError('Unsupported excitation noise identity')
    else:
        build_excitation_noise = None
    if recovery_directory is not None:
        recovery_directory = Path(recovery_directory)
        if (type(checkpoint_interval_updates) is not int or not 1 <= checkpoint_interval_updates <= 100000
                or recovery_directory.exists() or recovery_directory.is_symlink()
                or not recovery_directory.parent.is_dir()
                or recovery_directory.resolve().is_relative_to(output.resolve())
                or output.resolve().is_relative_to(recovery_directory.resolve())):
            raise ValueError("Recovery output must be new, separate and have an existing parent")
    inputs, metadata = deepcopy(dataset_inputs), deepcopy(run_metadata)
    schedulers = _schedulers(schedulers, generator_optimizer, discriminator_optimizer)
    target_inventory, source_inventory = deepcopy(targets), deepcopy(pcm_sources)
    started = time.monotonic()
    deadline = started + maximum_seconds

    def report(stage, **values):
        if on_progress is not None:
            on_progress(dict(stage=stage, elapsedSeconds=time.monotonic() - started, **values))

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

    report("admission-started")
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
    ranges = {source: (list(segment_frame_ranges(frames, training_segment_frames))
                      if training_segment_frames is not None else [(0, frames)])
              for source, frames in phrases.items()}
    planned_updates = sum(len(parts) for parts in ranges.values())
    if planned_updates > maximum_updates:
        raise ValueError("Vocoder update budget cannot cover all training segments")
    held_ids = set()
    partitions = {source: group["partition"] for group in snapshot["bindings"]["split"]["groups"]
                  for source in group["sourceIds"]}
    source_rows = {row["sourceId"]: row for row in snapshot["sources"]}
    from .unvoiced_periodicity import (OBJECTIVE_ID as PERIODIC_OBJECTIVE,
                                      MULTILAG_OBJECTIVE_ID, MULTILAGS, phone_mask)
    periodicity_lags = ((256,) if objective_id == PERIODIC_OBJECTIVE
                        else MULTILAGS if objective_id == MULTILAG_OBJECTIVE_ID
                        else None)
    periodic_labels = None
    if periodicity_lags is not None:
        periodic_labels = {entry['label']['sourceId']: entry for entry in snapshot['labels']}
        if any(source not in periodic_labels or periodic_labels[source]['score']['language'] != 'ja'
               for source in selected):
            raise ValueError('Periodicity objective requires admitted Japanese training labels')
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

    checkpoint_budget = min(maximum_checkpoint_total_bytes, 2 * maximum_checkpoint_file_bytes)
    # Retained float WAVs plus a conservative metadata allowance per item.
    evaluation_budget = (sum(source_rows[source]["frameCount"] * 4 + 1024 * 1024
                             for source in held_ids) if reconstruction_directory is not None else 0)

    def check_storage():
        require_disk_headroom(output.parent, checkpoint_budget + evaluation_budget)
        if reconstruction_directory is not None:
            # It may reside on another volume. Checking the aggregate on both
            # paths is intentionally conservative, not an actual reservation.
            require_disk_headroom(Path(reconstruction_directory), checkpoint_budget + evaluation_budget)

    def check_lifetime():
        check_running()
        check_storage()
        if time.time() >= snapshot["expiresAt"]:
            raise ValueError("Source review expired during vocoder epoch")

    def revalidate():
        current = refresh()
        if current["datasetSha256"] != snapshot["datasetSha256"]:
            raise ValueError("Dataset identity changed during vocoder epoch")

    effective_label_origin = label_origin or snapshot.get("labelOrigin") or metadata.get("labelOrigin") or "unspecified"
    state_metadata = dict(run=metadata, datasetBindings=snapshot["bindings"],
        datasetSha256=snapshot["datasetSha256"], profileSha256=expected_profile_sha256,
        objectiveId=objective_id, labelOrigin=effective_label_origin)
    plan, resume_cursor = None, None
    if recovery_enabled or excitation_noise_id is not None:
        profiles = [target_inventory[source][0]["profile"] for source in sorted(selected)]
        if any(profile != profiles[0] for profile in profiles):
            raise ValueError("Recovery sources must share one acoustic profile")
        plan = build_recovery_plan([dict(sourceId=source, analysisFrames=phrases[source],
                                        sourceSamples=expected[source]) for source in sorted(selected)],
            dataset_sha256=snapshot["datasetSha256"], profile_sha256=expected_profile_sha256,
            run_sha256=hashlib.sha256(encode_report(metadata)).hexdigest(),
            segment_frames=training_segment_frames or 4096, hop_size=profiles[0]["hopSize"])
        if len(plan["segments"]) != planned_updates:
            raise ValueError("Recovery plan update count differs")
        if resume_partial is not None:
            resume_partial = Path(resume_partial)
            saved = load_config(resume_partial / "checkpoint.json", resume_partial_sha256)
            if saved.get("formatId") != "com.project-seam.gan-partial-checkpoint":
                raise ValueError("Partial resume requires a partial-state receipt")
            resume_cursor = verify_partial_cursor(saved["epoch"], plan)
            if resume_cursor.get("excitationNoiseId") != excitation_noise_id:
                raise ValueError("Partial resume excitation identity differs from the captured run")
    recovery_bytes, restored = 0, resume_cursor is None
    recovery_written, retained_partials = 0, []
    if recovery_directory is not None:
        require_disk_headroom(recovery_directory.parent,
                              min(maximum_recovery_bytes, checkpoint_budget *
                                  (retain_partial_checkpoints + 1 if retain_partial_checkpoints is not None else 1))
                              + checkpoint_budget + evaluation_budget)
    check_lifetime()
    report("updates-started", totalUpdates=planned_updates)
    covered, total, gl, dl = {}, 0, 0.0, 0.0
    source_updates, updates = {}, 0
    excitation_raw_digest = (excitation_digest_seed(plan, excitation_noise_id, "raw")
                             if excitation_noise_id is not None else None)
    excitation_realized_digest = (excitation_digest_seed(plan, excitation_noise_id, "realized")
                                  if excitation_noise_id is not None else None)
    if resume_cursor is not None and excitation_noise_id is not None:
        excitation_raw_digest = resume_cursor["excitationRawChainSha256"]
        excitation_realized_digest = resume_cursor["excitationRealizedChainSha256"]
    segment_options = {} if training_segment_frames is None else dict(training_segment_frames=training_segment_frames)
    if excitation_noise_id is not None:
        segment_options['include_unvoiced_frames'] = True
    for batch in iter_vocoder_batches(snapshot, conditioning_directory, target_inventory, source_inventory,
            expected_profile_sha256=expected_profile_sha256, partition="train", batch_frames=4096, **segment_options):
        check_lifetime()
        identity = batch["sourceId"]
        part = source_updates.get(identity, 0)
        if identity not in selected or part >= len(ranges[identity]):
            raise ValueError("Vocoder segment source or ownership is duplicated")
        begin, end = ranges[identity][part]
        hop = batch["hopSize"]
        owned = min((end - begin) * hop, expected[identity] - begin * hop)
        if (batch["partition"] != "train"
                or batch["datasetSha256"] != snapshot["datasetSha256"]
                or batch["profileSha256"] != expected_profile_sha256 or batch["frameOffset"] != begin
                or batch["mel"].shape[2] != end - begin or owned <= 0
                or begin * hop != covered.get(identity, 0)
                or batch["validSamples"] != owned):
            raise ValueError("Vocoder batch identity or whole-phrase coverage differs")
        if plan is not None:
            actual = dict(sourceId=identity, frameOffset=begin, frameCount=end-begin, validSamples=owned)
            if updates >= len(plan["segments"]) or actual != plan["segments"][updates]:
                raise ValueError("Training batches differ from canonical recovery order")
        if resume_cursor is not None and updates < resume_cursor["completedUpdates"]:
            # Still read/verify every skipped source and segment, but do not optimize it again.
            covered[identity] = covered.get(identity, 0) + owned
            source_updates[identity] = part + 1
            updates += 1
            total += owned
            continue
        if not restored:
            if covered != resume_cursor["coveredSourceSamples"] or total != resume_cursor["validSamples"]:
                raise ValueError("Replayed prefix differs from retained recovery cursor")
            revalidate()
            restore_vocoder_partial_checkpoint(generator, discriminators, generator_optimizer, discriminator_optimizer,
                resume_partial, receipt_sha256=resume_partial_sha256, expected_metadata=state_metadata,
                recovery_plan=plan, schedulers=schedulers, maximum_bytes=maximum_checkpoint_file_bytes)
            gl, dl = resume_cursor["generatorLossSum"], resume_cursor["discriminatorLossSum"]
            restored = True
            report("partial-restored", completedUpdates=updates, totalUpdates=planned_updates)
        auxiliary = {}
        if periodic_labels is not None:
            auxiliary['periodicity_mask'] = phone_mask(periodic_labels[identity],
                sample_offset=begin * hop, sample_count=batch['pcm'].shape[2], valid_samples=owned)
        if build_excitation_noise is not None:
            raw, noise = build_excitation_noise(
                batch['unvoicedFrames'][0].numpy(), excitation_noise_id)
            auxiliary['excitation_noise'] = noise
        result = vocoder_gan_step(generator, discriminators, generator_optimizer, discriminator_optimizer,
            mel=batch["mel"], f0=batch["f0"], pcm=batch["pcm"], hop_size=batch["hopSize"],
            partition="train", reconstruction_loss=reconstruction_loss,
            periodicity_lags=periodicity_lags or (256,), **auxiliary)
        if build_excitation_noise is not None:
            segment = plan["segments"][updates]
            excitation_raw_digest = advance_excitation_digest(
                excitation_raw_digest, segment, raw.numpy().tobytes())
            excitation_realized_digest = advance_excitation_digest(
                excitation_realized_digest, segment, noise.numpy().tobytes())
        check_lifetime()
        count = batch["validSamples"]
        covered[identity] = covered.get(identity, 0) + count
        source_updates[identity] = part + 1
        updates += 1
        total += count
        gl += result["generatorLoss"] * count
        dl += result["discriminatorLoss"] * count
        if recovery_directory is not None and updates < planned_updates and updates % checkpoint_interval_updates == 0:
            remaining_recovery = maximum_recovery_bytes - recovery_bytes
            if remaining_recovery <= 0:
                raise RuntimeError("Recovery checkpoint budget exhausted; saved partial checkpoints retained")
            revalidate()
            allowed = min(checkpoint_budget, remaining_recovery)
            require_disk_headroom(recovery_directory.parent, allowed + checkpoint_budget + evaluation_budget)
            if not recovery_directory.exists():
                recovery_directory.mkdir(mode=0o700)
            child = recovery_directory / f"update-{updates:06d}"
            cursor = partial_cursor(plan, completed_updates=updates, generator_loss_sum=gl,
                                    discriminator_loss_sum=dl, excitation_noise_id=excitation_noise_id,
                                    raw_digest=excitation_raw_digest,
                                    realized_digest=excitation_realized_digest)
            saved = publish_vocoder_partial_checkpoint(generator, discriminators, generator_optimizer, discriminator_optimizer,
                child, metadata=state_metadata, recovery_plan=plan, cursor=cursor, schedulers=schedulers,
                maximum_bytes=maximum_checkpoint_file_bytes, maximum_total_bytes=allowed, before_publish=revalidate)
            recovery_bytes += saved["checkpointBytes"]
            recovery_written += saved["checkpointBytes"]
            digest = hashlib.sha256(encode_report(saved)).hexdigest()
            retained_partials.append((child, digest))
            if retain_partial_checkpoints is not None:
                from .vocoder_retention import prune_superseded_partial
                while len(retained_partials) > retain_partial_checkpoints:
                    old, old_digest = retained_partials[0]
                    recovery_bytes -= prune_superseded_partial(recovery_directory, old, old_digest,
                        child, digest, recovery_plan=plan)
                    retained_partials.pop(0)
            report("partial-checkpoint", completedUpdates=updates, checkpointDirectory=str(child),
                   receiptSha256=digest, recoveryBytes=recovery_bytes, recoveryWrittenBytes=recovery_written)
        if updates == 1 or updates % 25 == 0 or updates == planned_updates:
            report("updates-progress", completedUpdates=updates, totalUpdates=planned_updates,
                   validSamples=total, meanGeneratorLoss=gl / total, meanDiscriminatorLoss=dl / total)
    if covered != expected or updates != planned_updates or not restored:
        raise ValueError("Vocoder epoch incomplete; no checkpoint may be published")

    report("readmission-started")
    revalidate()
    for scheduler in (schedulers or {}).values():
        scheduler.step()
    epoch = dict(formatId="com.project-seam.vocoder-epoch-result", schemaVersion=1,
        datasetSha256=snapshot["datasetSha256"], profileSha256=expected_profile_sha256,
        objectiveId=objective_id, updates=updates, sourceCount=len(covered), validSamples=total,
        meanGeneratorLoss=gl / total, meanDiscriminatorLoss=dl / total,
        coveredSourceSamples=covered, epochComplete=True, coverageVerified=True,
        labelOrigin=effective_label_origin, trainingAdmitted=False, releaseEligible=False)
    if excitation_noise_id is not None:
        epoch.update(schemaVersion=2, excitationNoiseId=excitation_noise_id,
                     excitationDigestAlgorithm=EXCITATION_DIGEST_ALGORITHM,
                     excitationRawDrawSha256=excitation_raw_digest,
                     excitationRealizedSha256=excitation_realized_digest)
    if training_segment_frames is not None:
        epoch.update(trainingSegmentFrames=training_segment_frames, sourceUpdates=source_updates,
                     trainingGeometry="balanced-contiguous-complete-coverage-v1")
    if held_ids:
        def held_out_batches():
            seen = set()
            for partition in sorted({partitions[source] for source in held_ids}):
                check_lifetime()
                for batch in iter_vocoder_batches(snapshot, conditioning_directory, target_inventory, source_inventory,
                        expected_profile_sha256=expected_profile_sha256, partition=partition, batch_frames=4096,
                        include_unvoiced_frames=excitation_noise_id is not None):
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
        report("reconstruction-started", heldOutItems=len(held_ids), pitchEnabled=pitch_executable is not None)
        reconstruction_receipt = evaluate_held_out_reconstruction(
            generator_fn=generator,
            items=held_out_batches(),
            dataset_sha256=snapshot["datasetSha256"],
            profile_sha256=expected_profile_sha256,
            label_origin=effective_label_origin,
            profile=profiles[0], output_directory=reconstruction_directory,
            seed=evaluation_seed, check_running=check_lifetime,
            pitch_executable=pitch_executable,
            excitation_noise_id=excitation_noise_id,
        )
        epoch["reconstructionSummary"] = reconstruction_receipt.get("summary")
        epoch["reconstruction"] = reconstruction_receipt
        revalidate()
    report("checkpoint-started", completedUpdates=updates)
    # before_publish runs after writing binaries; checking there would require
    # headroom for a second copy. Check immediately before serialization instead.
    check_storage()
    completed = publish_vocoder_checkpoint(generator, discriminators, generator_optimizer, discriminator_optimizer,
        output, metadata=state_metadata, epoch=epoch, schedulers=schedulers,
        maximum_bytes=maximum_checkpoint_file_bytes, maximum_total_bytes=maximum_checkpoint_total_bytes,
        before_publish=revalidate)
    if retain_partial_checkpoints is not None and retained_partials:
        from .vocoder_retention import prune_completed_partial
        complete_digest = hashlib.sha256(encode_report(completed)).hexdigest()
        for old, digest in retained_partials:
            recovery_bytes -= prune_completed_partial(recovery_directory, old, digest,
                output, complete_digest, recovery_plan=plan)
        report("partial-retention-completed", recoveryBytes=recovery_bytes,
               recoveryWrittenBytes=recovery_written, successorReceiptSha256=complete_digest)
    return completed
