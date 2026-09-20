"""Export a captured local GAN checkpoint; does not authorize a singer or bundle.

Runs trusted pinned deployment source and decodes trusted local Torch checkpoints.
Use a separate process, not an application importing arbitrary upstream modules.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

from .__main__ import load_config, publish_new
from .gan_checkpoint_storage import load_local_checkpoint
from .check_vocoder_model import (TRAINING_REVISION, DEPLOYMENT_REVISION,
                                 trusted_checkout, export_checked_onnx, vocoder_configuration)


def export_identity(state, receipt, profile):
    metadata = state.get("metadata", {})
    run = metadata.get("run", {})
    if (receipt.get("formatId") != "com.project-seam.gan-checkpoint"
            or not isinstance(run, dict) or run.get("trainingRevision") != TRAINING_REVISION):
        raise ValueError("Require a reviewed local vocoder epoch checkpoint")
    configuration = run.get("configuration")
    supported = [vocoder_configuration(name) for name in
                 ("mini-nsf-32-smoke-v1", "mini-nsf-512-mrf-v1")]
    if configuration not in supported:
        raise ValueError("Unsupported vocoder architecture; no implicit configuration conversion")
    expected_profile = dict(profileId="seam-full-hop-slaney-v1", sampleRate=48000,
        fftSize=1024, windowSize=1024, hopSize=256, bins=80, minimumHz=20, maximumHz=24000,
        tailPadding="zero-to-whole-hop", boundaryPadding="reflect-fft-minus-hop",
        window="periodic-hann", spectrum="unnormalized-magnitude", melNormalization="slaney-area",
        melFrequencyScale="slaney", amplitudeScale="ln-amplitude", floor=1e-5,
        layout="TF", dtype="float32-le")
    if profile != expected_profile:
        raise ValueError("Unsupported vocoder acoustic profile")
    digest = hashlib.sha256(json.dumps(profile, sort_keys=True, separators=(",", ":"),
                                      allow_nan=False).encode()).hexdigest()
    epoch = state.get("epoch", {})
    from .unvoiced_periodicity import OBJECTIVE_ID as PERIODIC_OBJECTIVE
    objective = epoch.get('objectiveId')
    settings = run.get('settings', {})
    excitation_noise_id = settings.get('excitationNoiseId')
    if objective == PERIODIC_OBJECTIVE or excitation_noise_id is not None:
        from .train_vocoder import model_settings, training_objective
        expected_schema = 5 if excitation_noise_id is not None else 4
        if (settings.get('schemaVersion') != expected_schema or model_settings(settings) != configuration
                or training_objective(settings) != objective):
            raise ValueError('Variant export requires explicit matching versioned settings')
    if excitation_noise_id is not None:
        from .uv_noise_excitation import EXCITATION_IDS
        if excitation_noise_id not in EXCITATION_IDS:
            raise ValueError('Unsupported excitation noise identity')
        configuration = dict(configuration, excitationNoiseId=excitation_noise_id)
    if (digest != metadata.get("profileSha256") or digest != epoch.get("profileSha256")
            or epoch.get("formatId") != "com.project-seam.vocoder-epoch-result"
            or epoch.get("datasetSha256") != metadata.get("datasetSha256")
            or objective not in ("nsf-lsgan-logmel-48k80-v1", PERIODIC_OBJECTIVE)
            or metadata.get("objectiveId") != epoch.get("objectiveId")):
        raise ValueError("Vocoder checkpoint profile, dataset or objective identity differs")
    return configuration


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("checkpoint", "profile", "trusted-checkout", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--receipt-sha256", required=True)
    parser.add_argument("--profile-sha256", required=True)
    args = parser.parse_args()
    try:
        if args.output.exists() or args.output.is_symlink() or not args.output.parent.is_dir():
            raise ValueError("Export output must be new with an existing parent")
        profile = load_config(args.profile, args.profile_sha256)
        state, receipt = load_local_checkpoint(args.checkpoint, receipt_sha256=args.receipt_sha256)
        configuration = export_identity(state, receipt, profile)
        checkout = trusted_checkout(args.trusted_checkout, DEPLOYMENT_REVISION)
        import torch
        import onnxruntime as ort
        torch.set_num_threads(1)
        sys.path.insert(0, str(checkout))
        excitation_noise_id = configuration.get('excitationNoiseId')
        if excitation_noise_id is not None:
            from deployment.modules.nsf_hifigan import Generator as _BaseGenerator
            from .uv_noise_excitation import UvNoiseONNXAdapter, uv_noise_generator_class
            from modules.nsf_hifigan.env import AttrDict
            adapter = UvNoiseONNXAdapter.build(
                uv_noise_generator_class(_BaseGenerator), AttrDict(configuration))
        else:
            from deployment.modules.nsf_hifigan import NSFHiFiGANONNX
            adapter = NSFHiFiGANONNX(configuration)
        weights = {key.removeprefix("generator."): value for key, value in state["model"].items()
                   if key.startswith("generator.")}
        adapter.generator.load_state_dict(weights, strict=True)
        if any(not torch.isfinite(value).all() for value in weights.values()):
            raise ValueError("Nonfinite vocoder generator state")
        # The verified transport captures both GAN files before decoding. Export
        # needs only generator weights; release optimizer/discriminator memory.
        del state, weights
        graph, parity = export_checked_onnx(adapter, excitation_noise=excitation_noise_id is not None)
        args.output.mkdir(mode=0o700)
        with (args.output / "vocoder.onnx").open("xb") as stream:
            if stream.write(graph) != len(graph):
                raise OSError("Incomplete vocoder graph write")
            stream.flush()
            os.fsync(stream.fileno())
        report = dict(formatId="com.project-seam.vocoder-export", schemaVersion=1,
            vocoderPath="vocoder.onnx", vocoderSha256=hashlib.sha256(graph).hexdigest(),
            vocoderBytes=len(graph), checkpointReceiptSha256=args.receipt_sha256,
            architectureConfiguration=configuration,
            generatorParameterCount=sum(p.numel() for p in adapter.generator.parameters()),
            checkpointSha256=receipt["checkpointSha256"], trainingRevision=TRAINING_REVISION,
            deploymentRevision=DEPLOYMENT_REVISION, profile=profile,
            profileSha256=receipt["metadata"]["profileSha256"],
            datasetSha256=receipt["metadata"]["datasetSha256"],
            objectiveId=receipt["metadata"]["objectiveId"],
            parity=dict(parity, graphRetained=True), runtimeVersion=ort.__version__,
            sourceRightsRevalidated=False, modelBundleAdmitted=False,
            singerQualified=False, releaseEligible=False)
        publish_new(args.output / "export.json", report)
        print(json.dumps(report))
        return 0
    except KeyboardInterrupt:
        return 130
    except (ValueError, OSError, RuntimeError, ImportError, subprocess.SubprocessError) as error:
        print(str(error)[:256], file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
