"""Run reviewed whole-phrase DiffSinger DDPM epochs in an isolated process.

This is a CPU training entry point, not a qualified singer/export command.
The supplied upstream checkout executes trusted code; it is not sandboxed.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess
import sys

from .__main__ import encode_report, load_config, load_dataset_inputs
from .diffsinger_objective import DiffSingerDDPMObjective
from .conditioning import ADDED_CONDITIONING_PARAMETERS, added_parameters
from .checkpoint import load_local_checkpoint
from .epochs import run_reviewed_epochs

REVISION = "336cf01b57f2ad44c6b37a79cf33993043291759"


def model_settings(value: dict) -> dict:
    fields = {"hiddenSize", "encoderLayers", "channels", "layers", "timesteps",
              "seed", "learningRate", "maximumUpdates", "maximumSeconds", "loss"}
    schema = value.get("schemaVersion") if isinstance(value, dict) else None
    expected = fields | {"formatId", "schemaVersion"} | ({"conditioningControls"} if schema == 2 else set())
    if (not isinstance(value, dict) or set(value) != expected
            or value["formatId"] != "com.project-seam.ddpm-training-config"
            or type(schema) is not int or schema not in (1, 2)):
        raise ValueError("Unsupported DDPM training configuration")
    controls = value.get("conditioningControls", [])
    if not isinstance(controls, list) or controls not in ([], ["breathiness"]):
        raise ValueError("Training conditioning controls must be empty or exactly breathiness")
    for name, low, high in (("hiddenSize", 16, 256), ("encoderLayers", 1, 8),
                            ("channels", 16, 256), ("layers", 1, 16), ("timesteps", 8, 1000),
                            ("seed", 0, 2**63 - 1), ("maximumUpdates", 1, 100000)):
        if type(value[name]) is not int or not low <= value[name] <= high:
            raise ValueError(f"Invalid bounded training setting: {name}")
    if value["hiddenSize"] % 2:
        raise ValueError("Hidden size must be divisible by two attention heads")
    for name, low, high in (("learningRate", 0, .1), ("maximumSeconds", 0, 86400)):
        if type(value[name]) not in (int, float) or not math.isfinite(value[name]) or not low < value[name] <= high:
            raise ValueError(f"Invalid bounded training setting: {name}")
    if value["loss"] not in ("l1", "l2"):
        raise ValueError("Training loss must be l1 or l2")
    return dict(hidden_size=value["hiddenSize"], enc_layers=value["encoderLayers"],
                enc_ffn_kernel_size=3, ffn_act="gelu", dropout=0., num_heads=2,
                use_pos_embed=True, use_spk_id=False, diffusion_type="ddpm",
                use_breathiness_embed=controls == ["breathiness"], use_variance_scaling=False,
                use_shallow_diffusion=False, timesteps=value["timesteps"], K_step=value["timesteps"],
                backbone_type="wavenet", backbone_args=dict(num_layers=value["layers"],
                    num_channels=value["channels"], dilation_cycle_length=2),
                spec_min=[-12], spec_max=[0], schedule_type="linear", max_beta=.02,
                diff_speedup=1, diff_accelerator="ddim", infer=False)


def load_targets(config: Path, digest: str) -> tuple[dict, str]:
    value = load_config(config, digest)
    if (not isinstance(value, dict) or set(value) != {"formatId", "schemaVersion", "profileSha256", "targets"}
            or value["formatId"] != "com.project-seam.training-target-inventory"
            or type(value["schemaVersion"]) is not int or value["schemaVersion"] != 1
            or not isinstance(value["targets"], list) or not 1 <= len(value["targets"]) <= 10000):
        raise ValueError("Unsupported target inventory")
    profile = value["profileSha256"]
    if not isinstance(profile, str) or len(profile) != 64 or any(c not in "0123456789abcdef" for c in profile):
        raise ValueError("Target inventory requires a captured acoustic profile hash")
    result, metadata_bytes = {}, 0
    for row in value["targets"]:
        if not isinstance(row, dict) or set(row) != {"sourceId", "record", "recordSha256", "binary"}:
            raise ValueError("Invalid target inventory entry")
        identity = row["sourceId"]
        if (not isinstance(identity, str) or not 1 <= len(identity.encode()) <= 256
                or any(ord(c) < 32 or ord(c) == 127 for c in identity) or identity in result):
            raise ValueError("Target source IDs must be distinct bounded strings")
        for name in (row["record"], row["binary"]):
            if (not isinstance(name, str) or not 1 <= len(name) <= 128 or name in (".", "..")
                    or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_." for c in name)):
                raise ValueError("Target paths must be flat ASCII names beside the inventory")
        record = load_config(config.parent / row["record"], row["recordSha256"])
        metadata_bytes += len(encode_report(record))
        if metadata_bytes > 32 * 1024 * 1024:
            raise ValueError("Target metadata exceeds the aggregate 32 MiB budget")
        if not isinstance(record, dict) or record.get("profileSha256") != profile:
            raise ValueError("Target record differs from the selected acoustic profile")
        result[identity] = (record, config.parent / row["binary"])
    return result, profile


def _digest(value):
    return isinstance(value, str) and len(value) == 64 and all(c in "0123456789abcdef" for c in value)


def _conditioning_additions(prior_settings, current_settings):
    """Return the parameters an approved warm-start conditioning addition adds.

    Only an enabled breathiness control may be added, because it is the single
    architectural extension this project has measured evidence for: the acoustic
    model cannot produce noise-like spectra without an aperiodicity channel.
    """
    return added_parameters(prior_settings.get("conditioningControls", []),
                            current_settings.get("conditioningControls", []))


def _warm_start_identity(value):
    hashes = {"sourceReceiptSha256", "sourceCheckpointSha256", "sourceTrainingConfigurationSha256"}
    fields = hashes | {"sourceCompletedEpochs", "optimizerReset", "rngReset", "epochNumbering"}
    added = value.get("addedParameters", []) if isinstance(value, dict) else None
    if (not isinstance(value, dict) or set(value) - {"addedParameters"} != fields
            or any(not _digest(value[key]) for key in hashes)
            or type(value["sourceCompletedEpochs"]) is not int
            or not 1 <= value["sourceCompletedEpochs"] < 100000
            or value["optimizerReset"] is not True or value["rngReset"] is not True
            or value["epochNumbering"] != "new-experiment-from-one"
            or not isinstance(added, list) or len(set(added)) != len(added)
            or any(name not in ADDED_CONDITIONING_PARAMETERS for name in added)):
        raise ValueError("Invalid captured warm-start identity")
    return dict(value)


def _load_warm_start_state(model, captured, additions):
    """Load captured weights and zero-initialize an added conditioning embedding.

    Zero initialization is what keeps the new run honest: the added embedding
    contributes nothing until it is trained, so an untrained warm start begins at
    exactly the captured model's behaviour instead of at a perturbed one.
    """
    import torch
    present = set(model.state_dict())
    if present - set(captured) != set(additions) or set(captured) - present:
        raise ValueError("Warm-start conditioning addition does not match the captured architecture")
    missing, unexpected = model.load_state_dict(captured, strict=False)
    if set(unexpected) or set(missing) != set(additions):
        raise ValueError("Warm start changed more than the declared conditioning addition")
    parameters = dict(model.named_parameters())
    with torch.no_grad():
        for name in additions:
            parameters[name].zero_()


def initialize_checkpoint(model, optimizer, state, receipt, *, metadata, profile,
                          receipt_sha256, warm_start=False):
    """Initialize only from a verified local checkpoint; never mutate its state.

    Warm start permits loss/learning-rate changes only, on the same admitted
    dataset, architecture, vocabulary and runtime, plus one approved exception:
    enabling the breathiness conditioning control, which adds a zero-initialized
    embedding and therefore reproduces the captured model's behaviour exactly at
    the start of the new run. It starts a new experiment; exact resume inherits
    its immutable origin and restores optimizer/RNG state. The caller must still
    re-admit the dataset before training/publication.
    """
    import torch
    previous = receipt["metadata"].get("run")
    dataset = receipt["metadata"].get("datasetSha256")
    completed = previous.get("completedEpochs") if isinstance(previous, dict) else None
    if (not isinstance(previous, dict) or not _digest(receipt_sha256)
            or not _digest(dataset) or type(completed) is not int or not 1 <= completed < 100000
            or receipt["metadata"].get("profileSha256") != profile
            or receipt["epoch"].get("epochComplete") is not True
            or receipt["epoch"].get("coverageVerified") is not True
            or receipt["epoch"].get("datasetSha256") != dataset):
        raise ValueError("Initialization requires a complete captured checkpoint lineage and matching profile")
    origin = previous.get("warmStart")
    if "warmStart" in previous:
        origin = _warm_start_identity(origin)
    captured = {key: value for key, value in previous.items()
                if key not in ("completedEpochs", "parentReceiptSha256", "warmStart")}
    result = dict(metadata)
    prior_settings = captured.get("settings")
    if (type(warm_start) is not bool
            or model_settings(metadata.get("settings")) != metadata.get("configuration")
            or model_settings(prior_settings) != captured.get("configuration")
            or receipt["epoch"].get("objectiveId") != DiffSingerDDPMObjective(prior_settings["loss"]).objective_id):
        raise ValueError("Checkpoint settings, architecture and objective disagree")
    if warm_start:
        allowed = {"loss", "learningRate"}
        current_settings = metadata["settings"]
        additions = _conditioning_additions(prior_settings, current_settings)
        # Enabling the control is exactly what promotes a schema-1 configuration to
        # schema 2, and `model_settings` already rejects any other pairing of
        # schema and declared controls. Pin the version to the addition so the
        # bookkeeping field cannot drift on its own.
        governed = {"conditioningControls", "schemaVersion"}
        prior_fields = set(prior_settings) - governed
        current_fields = set(current_settings) - governed
        if (prior_fields != current_fields
                or any(prior_settings[key] != current_settings[key]
                       for key in prior_fields if key not in allowed)
                or current_settings["schemaVersion"] != (2 if additions else prior_settings["schemaVersion"])
                # `configuration` is excluded because it is a pure function of
                # `settings` and must differ when the control is added; both sides
                # are already required to match their own settings above.
                or {k: v for k, v in captured.items()
                    if k not in ("settings", "trainingConfigurationSha256", "configuration")}
                    != {k: v for k, v in metadata.items()
                        if k not in ("settings", "trainingConfigurationSha256", "configuration")}
                or not _digest(captured.get("trainingConfigurationSha256"))
                or not _digest(metadata.get("trainingConfigurationSha256"))
                or (prior_settings != current_settings
                    and captured["trainingConfigurationSha256"] == metadata["trainingConfigurationSha256"])
                or not _digest(receipt.get("checkpointSha256"))):
            raise ValueError("Warm start permits only explicit loss/learning-rate changes on identical training inputs")
        if (optimizer.state or not optimizer.param_groups
                or any(group["lr"] != current_settings["learningRate"] for group in optimizer.param_groups)):
            raise ValueError("Warm start requires a fresh optimizer at the captured learning rate")
        result["warmStart"] = dict(sourceReceiptSha256=receipt_sha256,
            sourceCheckpointSha256=receipt["checkpointSha256"], sourceCompletedEpochs=completed,
            sourceTrainingConfigurationSha256=captured["trainingConfigurationSha256"],
            optimizerReset=True, rngReset=True, epochNumbering="new-experiment-from-one")
        if additions:
            result["warmStart"]["addedParameters"] = sorted(additions)
    elif captured != metadata:
        raise ValueError("Resume requires identical captured training inputs and environment")
    elif origin is not None:
        result["warmStart"] = origin
    if warm_start:
        _load_warm_start_state(model, state["model"], additions)
    else:
        model.load_state_dict(state["model"], strict=True)
    if warm_start:
        torch.manual_seed(metadata["settings"]["seed"])
    else:
        optimizer.load_state_dict(state["optimizer"])
        torch.set_rng_state(state["rng"])
    return result, dataset, 0 if warm_start else completed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("training_config", "dataset_config", "targets", "source_root", "conditioning", "trusted_checkout", "output"):
        parser.add_argument("--" + name.replace("_", "-"), type=Path, required=True)
    for name in ("training_sha256", "dataset_sha256", "targets_sha256", "rights_policy_sha256", "label_policy_sha256"):
        parser.add_argument("--" + name.replace("_", "-"), required=True)
    start = parser.add_mutually_exclusive_group()
    start.add_argument("--resume", type=Path, help="Trusted locally produced checkpoint directory")
    start.add_argument("--warm-start", type=Path, help="Reuse weights; reset optimizer/RNG; allow only loss/LR changes")
    parser.add_argument("--resume-receipt-sha256", help="Independently captured checkpoint.json file digest")
    parser.add_argument("--warm-start-receipt-sha256", help="Captured checkpoint.json digest for a new experiment")
    parser.add_argument("--epochs", type=int, default=1)
    parser.add_argument("--maximum-run-seconds", type=float, default=3600)
    parser.add_argument("--maximum-total-checkpoint-bytes", type=int, default=2 * 1024**3)
    parser.add_argument("--retain-checkpoints", type=int,
                        help="Keep newest N binaries from this new run; keep all receipts and need N+1 space")
    parser.add_argument("--minimum-free-bytes", type=int, default=0)
    args = parser.parse_args()
    try:
        if (args.resume is None) != (args.resume_receipt_sha256 is None):
            raise ValueError("Resume requires both a local checkpoint and its captured receipt digest")
        if (args.warm_start is None) != (args.warm_start_receipt_sha256 is None):
            raise ValueError("Warm start requires both a local checkpoint and its captured receipt digest")
        if args.output.exists() or args.output.is_symlink() or not args.output.parent.is_dir():
            raise ValueError("Checkpoint output must be new with an existing parent")
        settings = load_config(args.training_config, args.training_sha256)
        hparams_value = model_settings(settings)
        inputs = load_dataset_inputs(args.dataset_config, args.dataset_sha256, args.source_root,
                                     rights_anchor=args.rights_policy_sha256, label_anchor=args.label_policy_sha256)
        targets, profile = load_targets(args.targets, args.targets_sha256)
        labels = load_config(inputs["label_config"], inputs["label_hash"])
        vocabulary = labels.get("vocabulary") if isinstance(labels, dict) else None
        if not isinstance(vocabulary, list) or not 1 <= len(vocabulary) <= 4096:
            raise ValueError("Training requires a bounded captured vocabulary")
        dimensions = [record.get("profile", {}).get("bins") if isinstance(record.get("profile"), dict)
                      else None for record, _ in targets.values()]
        if any(type(b) is not int or not 1 <= b <= 512 for b in dimensions) or len(set(dimensions)) != 1:
            raise ValueError("Acoustic targets must share a bounded mel dimension")
        checkout = args.trusted_checkout.resolve(strict=True)
        def git(*arguments):
            return subprocess.check_output(["git", "-C", str(checkout), *arguments], text=True, timeout=10).strip()
        if git("rev-parse", "HEAD") != REVISION or git("status", "--porcelain"):
            raise ValueError("Training requires a clean trusted checkout at the pinned DiffSinger revision")
        import torch
        import numpy as np
        # Import only in this standalone process: upstream has global hparams.
        sys.path.insert(0, str(checkout))
        from utils.hparams import hparams
        hparams.clear()
        hparams.update(hparams_value)
        from modules.toplevel import DiffSingerAcoustic
        torch.set_num_threads(1)
        torch.manual_seed(settings["seed"])
        model = DiffSingerAcoustic(vocab_size=len(vocabulary) + 1, out_dims=dimensions[0])
        optimizer = torch.optim.AdamW(model.parameters(), lr=settings["learningRate"])
        objective = DiffSingerDDPMObjective(settings["loss"])
        metadata = dict(trainingConfigurationSha256=args.training_sha256,
                        assemblyConfigurationSha256=args.dataset_sha256, targetInventorySha256=args.targets_sha256,
                        configuration=hparams_value, settings=settings, revision=REVISION,
                        torchVersion=str(torch.__version__), numpyVersion=np.__version__, vocabulary=vocabulary,
                        singerQualified=False)
        expected_dataset, completed_epochs = None, 0
        parent_digest = args.resume_receipt_sha256 or args.warm_start_receipt_sha256
        if args.resume is not None or args.warm_start is not None:
            state, previous = load_local_checkpoint(args.resume or args.warm_start, receipt_sha256=parent_digest)
            metadata, expected_dataset, completed_epochs = initialize_checkpoint(
                model, optimizer, state, previous, metadata=metadata, profile=profile,
                receipt_sha256=parent_digest, warm_start=args.warm_start is not None)
        result = run_reviewed_epochs(model, optimizer, output=args.output, epochs=args.epochs,
            completed_epochs=completed_epochs, parent_receipt_sha256=parent_digest,
            metadata=metadata, maximum_run_seconds=args.maximum_run_seconds,
            maximum_total_checkpoint_bytes=args.maximum_total_checkpoint_bytes,
            retain_checkpoints=args.retain_checkpoints, minimum_free_bytes=args.minimum_free_bytes,
            epoch_options=dict(dataset_inputs=inputs,
            conditioning_directory=args.conditioning, targets=targets, expected_profile_sha256=profile,
            maximum_updates=settings["maximumUpdates"], maximum_seconds=settings["maximumSeconds"],
            objective=objective, objective_id=objective.objective_id,
            expected_dataset_sha256=expected_dataset))
        print(json.dumps(result, sort_keys=True))
        return 0
    except KeyboardInterrupt:
        print("Training interrupted; discard the incomplete attempt", file=sys.stderr)
        return 130
    except (ValueError, OSError, RuntimeError, ImportError, RecursionError, subprocess.SubprocessError) as error:
        print(str(error)[:256], file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
