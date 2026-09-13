"""Synthetic integration diagnostic, never a source of production review keys.

The deliberately public signing seed authenticates only temporary engineering
fixtures under a fixture-local policy. It conveys no real-person approval.
"""
import base64
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import wave

from tools.phase13a.update_contract import ed25519_public_key, ed25519_sign
from tools.public_release.crypto_validation import canonical_signing_payload
from tools.public_release.contracts import sha256_json
from .__main__ import assemble_dataset, encode_report, publish_new, load_dataset_inputs
from .acoustics import wav_log_mel_targets
from .batches import iter_supervised_batches
from .checkpoint import load_local_checkpoint
from .optimization import acoustic_evaluation_step
from .permissions import TRAINING_PERMISSIONS
from .split import split_sources
from .training_run import train_reviewed_epoch


def check_reviewed_run(model, optimizer, *, objective, model_metadata: dict,
                       trusted_checkout: Path | None = None, check_export: bool = False,
                       native_probe: Path | None = None) -> dict:
    """Exercise real file admission, optimization, publication and held-out I/O."""
    import numpy as np
    import torch
    fixture_seed = bytes(range(32))

    def review_for(kind, configuration_hash):
        label = kind == "label"
        role = "training-label-reviewer" if label else "training-rights-reviewer"
        policy = dict(policyVersion="seam-training-label-review-1" if label else "seam-training-review-1",
                      algorithm="Ed25519", requiredRoles=[role], trustedKeys=[dict(
                          keyId="synthetic-fixture-only", role=role, signerId="synthetic-fixture-only",
                          publicKey=base64.b64encode(ed25519_public_key(fixture_seed)).decode())])
        anchor = sha256_json(policy)
        now = int(time.time())
        review = dict(formatId=f"com.project-seam.training-{kind}-review", schemaVersion=1,
                      policyVersion=policy["policyVersion"], policySha256=anchor, algorithm="Ed25519",
                      keyId="synthetic-fixture-only", signerId="synthetic-fixture-only",
                      configurationSha256=configuration_hash,
                      decision="APPROVE_LABELS" if label else "APPROVE_TRAINING_SCOPES",
                      issuedAt=now - 1, expiresAt=now + 600)
        review["signature"] = base64.b64encode(ed25519_sign(
            canonical_signing_payload(review, "recordSha256"), fixture_seed)).decode()
        review["recordSha256"] = sha256_json(review)
        return review, policy, anchor

    with tempfile.TemporaryDirectory(prefix="seam-reviewed-training-fixture-") as temporary:
        root = Path(temporary)
        evidence = b"Synthetic oscillator engineering fixture; no human voice or actual lyric supervision."
        (root / "fixture-evidence.txt").write_bytes(evidence)
        sources, permissions, labels, targets, split_rows = [], [], [], {}, []
        for index, frequency in enumerate((220., 330., 440.)):
            identity = f"fixture-{index}"
            buffer = io.BytesIO()
            samples = .2 * np.sin(2 * np.pi * frequency * np.arange(4096) / 48000)
            with wave.open(buffer, "wb") as writer:
                writer.setnchannels(1)
                writer.setsampwidth(2)
                writer.setframerate(48000)
                writer.writeframes(np.rint(samples * 32767).astype("<i2").tobytes())
            payload = buffer.getvalue()
            digest = hashlib.sha256(payload).hexdigest()
            (root / f"{identity}.wav").write_bytes(payload)
            record, matrix = wav_log_mel_targets(payload, expected_sha256=digest, sample_rate=48000)
            target_path = root / f"{identity}.f32le"
            target_path.write_bytes(matrix.astype("<f4").tobytes())
            targets[identity] = (record, target_path)
            source = dict(sourceId=identity, sourceSha256=digest, path=f"{identity}.wav",
                          songId=identity, sessionId=identity, lineageId=identity)
            sources.append(source)
            split_rows.append({key: source[key] for key in ("sourceId", "songId", "sessionId", "lineageId")} |
                              dict(audioSha256=record["audioSha256"]))
            permissions.append(dict(sourceId=identity, sourceSha256=digest, identityId="oscillator-fixture",
                                    kind="PROCEDURAL_SYNTHESIS", evidenceId="fixture",
                                    evidenceSha256=hashlib.sha256(evidence).hexdigest(), reviewRevision="test-only",
                                    permissions=dict.fromkeys(TRAINING_PERMISSIONS, True)))
            labels.append(dict(sourceSha256=digest, audioSha256=record["audioSha256"],
                               label=dict(sourceId=identity, frameCount=4096, hopSize=256,
                                          f0Hz=[frequency] * 16, voiced=[True] * 16, reviewRevision=None,
                                          phonemes=[dict(symbol="a", startFrame=0, endFrame=4096, confidence=1)]),
                               score=dict(language="en", silencePhones=[],
                                          syllables=[dict(lyric="fixture", phoneStart=0, phoneEnd=1)],
                                          notes=[dict(startFrame=0, endFrame=4096, midi=(57, 64, 69)[index],
                                                      syllable=0, slur=False)])))
        # Select a fixture seed solely to exercise all three partitions. Never
        # select a real study split by model performance or held-out outcomes.
        for index in range(1000):
            split_seed = f"engineering-fixture-{index}"
            split = split_sources(split_rows, seed=split_seed, held_out_songs=["fixture-2"])
            if not split["missingPartitions"]:
                break
        else:
            raise ValueError("Could not construct three fixture partitions")
        permission_config = dict(formatId="com.project-seam.training-permission-config", schemaVersion=1,
                                 sampleRate=48000, sources=sources, evidence={"fixture": "fixture-evidence.txt"},
                                 manifest=dict(formatId="com.project-seam.training-permission-manifest",
                                               schemaVersion=1, sources=permissions))
        label_config = dict(formatId="com.project-seam.voice-training-label-config", schemaVersion=3,
                            sampleRate=48000, sources=sources, vocabulary=["a", "i", "u"],
                            minimumConfidence=.8, labels=labels)
        publish_new(root / "permissions.json", permission_config)
        publish_new(root / "labels.json", label_config)
        permission_hash = hashlib.sha256(encode_report(permission_config)).hexdigest()
        label_hash = hashlib.sha256(encode_report(label_config)).hexdigest()
        rights_review, rights_policy, rights_anchor = review_for("rights", permission_hash)
        label_review, label_policy, label_anchor = review_for("label", label_hash)
        references = dict(permissionConfig=dict(path="permissions.json", sha256=permission_hash),
                          labelConfig=dict(path="labels.json", sha256=label_hash))
        for key, value in (("rightsReview", rights_review), ("rightsPolicy", rights_policy),
                           ("labelReview", label_review), ("labelPolicy", label_policy)):
            name = f"{key}.json"
            publish_new(root / name, value)
            references[key] = dict(path=name, sha256=hashlib.sha256(encode_report(value)).hexdigest())
        configuration = dict(formatId="com.project-seam.training-dataset-config", schemaVersion=1,
                             seed=split_seed, heldOutSongs=["fixture-2"], **references)
        publish_new(root / "dataset.json", configuration)
        configuration_hash = hashlib.sha256(encode_report(configuration)).hexdigest()
        inputs = load_dataset_inputs(root / "dataset.json", configuration_hash, root,
                                     rights_anchor=rights_anchor, label_anchor=label_anchor)
        shards = root / "conditioning"
        snapshot = assemble_dataset(**inputs, now=int(time.time()), conditioning_directory=shards)
        profile = record["profileSha256"]
        before = {key: value.detach().clone() for key, value in model.named_parameters()}
        receipt = train_reviewed_epoch(model, optimizer, dataset_inputs=inputs,
                                      conditioning_directory=shards, targets=targets,
                                      expected_profile_sha256=profile, output=root / "checkpoint",
                                      run_metadata=dict(model=model_metadata, syntheticInputs=True,
                                                        assemblyConfigurationSha256=configuration_hash,
                                                        fixturePolicyOnly=True, singerQualified=False),
                                      maximum_updates=3, objective=objective, objective_id=objective.objective_id)
        changed = sum(not torch.equal(before[key], value.detach()) for key, value in model.named_parameters())
        receipt_hash = hashlib.sha256((root / "checkpoint" / "checkpoint.json").read_bytes()).hexdigest()
        restored, loaded = load_local_checkpoint(root / "checkpoint", receipt_sha256=receipt_hash)
        exact = loaded == receipt and all(torch.equal(value, restored["model"][key])
                                         for key, value in model.state_dict().items())
        validation = [acoustic_evaluation_step(model, batch, vocabulary_size=3, seed=501,
                      objective=objective, objective_id=objective.objective_id)
                      for batch in iter_supervised_batches(snapshot, shards, targets,
                          expected_profile_sha256=profile, partition="validation", batch_frames=4096)]
        command_result = None
        if trusted_checkout is not None:
            target_rows = []
            for identity, (record, path) in targets.items():
                name = f"{identity}-target.json"
                publish_new(root / name, record)
                target_rows.append(dict(sourceId=identity, record=name,
                                        recordSha256=hashlib.sha256(encode_report(record)).hexdigest(), binary=path.name))
            inventory = dict(formatId="com.project-seam.training-target-inventory", schemaVersion=1,
                             profileSha256=profile, targets=target_rows)
            settings = dict(formatId="com.project-seam.ddpm-training-config", schemaVersion=1,
                            hiddenSize=32, encoderLayers=1, channels=32, layers=2, timesteps=8,
                            seed=17, learningRate=.001, maximumUpdates=3, maximumSeconds=60, loss="l2")
            publish_new(root / "targets.json", inventory)
            publish_new(root / "training.json", settings)
            command = [sys.executable, "-m", "tools.voice_model_training.train",
                       "--training-config", str(root / "training.json"),
                       "--training-sha256", hashlib.sha256(encode_report(settings)).hexdigest(),
                       "--dataset-config", str(root / "dataset.json"), "--dataset-sha256", configuration_hash,
                       "--targets", str(root / "targets.json"),
                       "--targets-sha256", hashlib.sha256(encode_report(inventory)).hexdigest(),
                       "--source-root", str(root), "--conditioning", str(shards),
                       "--trusted-checkout", str(trusted_checkout), "--output", str(root / "command-checkpoint"),
                       "--rights-policy-sha256", rights_anchor, "--label-policy-sha256", label_anchor]
            completed = subprocess.run(command, capture_output=True, text=True, timeout=90)
            if completed.returncode:
                raise ValueError(f"Training command failed: {completed.stderr[-512:]}")
            command_result = json.loads(completed.stdout)
            command_hash = hashlib.sha256((root / "command-checkpoint" / "checkpoint.json").read_bytes()).hexdigest()
            _, command_receipt = load_local_checkpoint(root / "command-checkpoint", receipt_sha256=command_hash)
            if (command_result["checkpointSha256"] != command_receipt["checkpointSha256"]
                    or command_receipt["epoch"]["coveredSourceFrames"] != receipt["epoch"]["coveredSourceFrames"]):
                raise ValueError("Training command checkpoint does not match its reported epoch")
            continuations = []
            for name in ("resume-a", "resume-b"):
                resumed_command = list(command)
                resumed_command[resumed_command.index("--output") + 1] = str(root / name)
                resumed_command += ["--resume", str(root / "command-checkpoint"),
                                    "--resume-receipt-sha256", command_hash]
                resumed = subprocess.run(resumed_command, capture_output=True, text=True, timeout=90)
                if resumed.returncode:
                    raise ValueError(f"Training resume failed: {resumed.stderr[-512:]}")
                resumed_hash = hashlib.sha256((root / name / "checkpoint.json").read_bytes()).hexdigest()
                resumed_state, resumed_receipt = load_local_checkpoint(root / name, receipt_sha256=resumed_hash)
                if (resumed_receipt["metadata"]["run"]["completedEpochs"] != 2
                        or resumed_receipt["metadata"]["run"]["parentReceiptSha256"] != command_hash):
                    raise ValueError("Training resume lost checkpoint lineage")
                continuations.append((resumed_state, json.loads(resumed.stdout)))
            left, right = continuations
            if (left[1]["epoch"] != right[1]["epoch"]
                    or not torch.equal(left[0]["rng"], right[0]["rng"])
                    or any(not torch.equal(value, right[0]["model"][key]) for key, value in left[0]["model"].items())):
                raise ValueError("Resumed training is not reproducible from the same checkpoint")
            command_result["repeatedResumeExact"] = True
            command_result["resumedCompletedEpochs"] = 2
            continuous_command = list(command)
            continuous_command[continuous_command.index("--output") + 1] = str(root / "continuous")
            continuous_command += ["--epochs", "2"]
            continuous = subprocess.run(continuous_command, capture_output=True, text=True, timeout=90)
            if continuous.returncode:
                raise ValueError(f"Multi-epoch training failed: {continuous.stderr[-512:]}")
            continuous_result = json.loads(continuous.stdout)
            final_directory = root / "continuous" / continuous_result["checkpoints"][-1]["path"]
            continuous_state, _ = load_local_checkpoint(final_directory,
                receipt_sha256=continuous_result["checkpoints"][-1]["receiptSha256"])
            if (continuous_result["completedEpochs"] != 2
                    or continuous_result["epoch"] != left[1]["epoch"]
                    or not torch.equal(continuous_state["rng"], left[0]["rng"])
                    or any(not torch.equal(value, left[0]["model"][key])
                           for key, value in continuous_state["model"].items())):
                raise ValueError("Continuous epochs differ from separate resumed epochs")
            if json.loads((root / "continuous" / "run.json").read_bytes()) != continuous_result:
                raise ValueError("Multi-epoch completion record differs from command result")
            command_result["continuousVersusResumedExact"] = True
            if check_export:
                acoustic_profile = next(iter(targets.values()))[0]["profile"]
                publish_new(root / "profile.json", acoustic_profile)
                exported = subprocess.run([sys.executable, "-m", "tools.voice_model_training.export",
                    "--checkpoint", str(root / "command-checkpoint"), "--receipt-sha256", command_hash,
                    "--profile", str(root / "profile.json"),
                    "--profile-sha256", hashlib.sha256(encode_report(acoustic_profile)).hexdigest(),
                    "--trusted-checkout", str(trusted_checkout), "--output", str(root / "export")],
                    capture_output=True, text=True, timeout=90)
                if exported.returncode:
                    raise ValueError(f"Acoustic export command failed: {exported.stderr[-512:]}")
                export_report = json.loads((root / "export" / "export.json").read_bytes())
                graph_hash = hashlib.sha256((root / "export" / "acoustic.onnx").read_bytes()).hexdigest()
                if (graph_hash != export_report["acousticSha256"]
                        or graph_hash != json.loads(exported.stdout)["acousticSha256"]
                        or export_report["checkpointReceiptSha256"] != command_hash
                        or export_report["releaseEligible"] is not False):
                    raise ValueError("Published export lost graph identity or checkpoint provenance")
                command_result["checkpointExportVerified"] = True
                if native_probe is not None:
                    native_command = [str(native_probe.resolve(strict=True)), "--acoustic-export",
                                      str(root / "export" / "acoustic.onnx"), graph_hash]
                    native = subprocess.run(native_command, capture_output=True, text=True, timeout=60)
                    if native.returncode:
                        raise ValueError(f"Native learned acoustic inference failed: {native.stderr[-512:]}")
                    native_result = json.loads(native.stdout)
                    if native_result.get("status") != "ACOUSTIC_EXPORT_NATIVE_SMOKE" or native_result.get("cases") != 3:
                        raise ValueError("Unexpected native acoustic smoke result")
                    rejected = subprocess.run(native_command[:-1] + ["0" * 64],
                                              capture_output=True, text=True, timeout=15)
                    if rejected.returncode == 0 or "captured digest" not in rejected.stderr:
                        raise ValueError("Native acoustic intake did not reject a wrong hash")
                    command_result["nativeAcousticExport"] = native_result
        passed = changed > 0 and exact and len(validation) == 1 and receipt["epoch"]["coverageVerified"]
        return dict(passed=passed, changedParameterTensors=changed, checkpointRestoredExact=exact,
                    epoch=receipt["epoch"], partitionCounts=split["counts"], validation=validation,
                    trainingCommand=command_result,
                    syntheticInputs=True, fixturePolicyOnly=True, singerQualified=False,
                    checkpointRetained=False, releaseEligible=False)
