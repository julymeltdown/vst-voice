"""Publish an unqualified acoustic ONNX artifact from a trusted local training checkpoint."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

from .__main__ import load_config, publish_new
from .checkpoint import load_local_checkpoint
from .train import REVISION, model_settings


def export_identity(state: dict, profile: dict) -> tuple[dict, list]:
    metadata = state.get("metadata", {})
    run = metadata.get("run", {})
    if not isinstance(run, dict) or run.get("revision") != REVISION:
        raise ValueError("Export requires a supported local training checkpoint")
    configuration = model_settings(run.get("settings"))
    if configuration != run.get("configuration"):
        raise ValueError("Checkpoint architecture differs from captured training settings")
    digest = hashlib.sha256(json.dumps(profile, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()
    if digest != metadata.get("profileSha256") or state.get("epoch", {}).get("profileSha256") != digest:
        raise ValueError("Export acoustic profile differs from the training checkpoint")
    vocabulary = run.get("vocabulary")
    if (not isinstance(vocabulary, list) or not 1 <= len(vocabulary) <= 4096
            or any(not isinstance(token, str) or not 1 <= len(token.encode()) <= 256
                   or any(ord(c) < 32 or ord(c) == 127 for c in token) for token in vocabulary)
            or len(set(vocabulary)) != len(vocabulary)):
        raise ValueError("Export requires an ordered, unique captured vocabulary")
    if (not isinstance(profile, dict) or type(profile.get("bins")) is not int
            or not 1 <= profile["bins"] <= 512):
        raise ValueError("Export requires bounded acoustic dimensions")
    return configuration, vocabulary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("checkpoint", "profile", "trusted-checkout", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--receipt-sha256", required=True)
    parser.add_argument("--profile-sha256", required=True, help="Exact profile JSON file digest")
    args = parser.parse_args()
    try:
        if args.output.exists() or args.output.is_symlink() or not args.output.parent.is_dir():
            raise ValueError("Export output must be new with an existing parent")
        profile = load_config(args.profile, args.profile_sha256)
        state, receipt = load_local_checkpoint(args.checkpoint, receipt_sha256=args.receipt_sha256)
        configuration, vocabulary = export_identity(state, profile)
        checkout = args.trusted_checkout.resolve(strict=True)
        def git(*arguments):
            return subprocess.check_output(["git", "-C", str(checkout), *arguments], text=True, timeout=10).strip()
        if git("rev-parse", "HEAD") != REVISION or git("status", "--porcelain"):
            raise ValueError("Export requires the clean trusted pinned upstream checkout")
        import torch
        import numpy as np
        import onnxruntime as ort
        ort.disable_telemetry_events()
        sys.path.insert(0, str(checkout))
        from utils.hparams import hparams
        hparams.clear()
        hparams.update(configuration)
        from modules.toplevel import DiffSingerAcoustic
        from .export_adapter import check_deployment_bridge, prepare_acoustic_export
        from .onnx_acoustic import export_acoustic
        from .onnx_encoder import check_encoder_runtime
        from tools.neural_runtime.inspect_graph import inspect_bytes
        torch.set_num_threads(1)
        model = DiffSingerAcoustic(vocab_size=len(vocabulary) + 1, out_dims=profile["bins"])
        model.load_state_dict(state["model"], strict=True)
        deployment_check = check_deployment_bridge(
            model, configuration=configuration, acoustic_profile=profile)
        if not deployment_check["passed"]:
            raise ValueError("Export deployment conditioning effect check failed")
        deployment = prepare_acoustic_export(model, configuration=configuration, acoustic_profile=profile)
        encoder_check = check_encoder_runtime(deployment)
        if not encoder_check["passed"]:
            raise ValueError("Export encoder parity or conditioning effect check failed")
        graph = export_acoustic(deployment)
        inspection = inspect_bytes(graph)
        options = ort.SessionOptions()
        options.intra_op_num_threads = options.inter_op_num_threads = 1
        session = ort.InferenceSession(graph, sess_options=options, providers=["CPUExecutionProvider"])
        smoke_inputs = dict(tokens=np.ones((1, 2), dtype=np.int64),
            durations=np.array([[2, 3]], dtype=np.int64), f0=np.full((1, 5), 220., dtype=np.float32),
            steps=np.array(1, dtype=np.int64))
        conditioned = configuration.get("use_breathiness_embed", False)
        if conditioned:
            smoke_inputs["breathiness"] = np.linspace(0, 1, 5, dtype=np.float32)[None]
        result = session.run(["mel"], smoke_inputs)[0]
        if result.shape != (1, 5, profile["bins"]) or not np.isfinite(result).all():
            raise ValueError("Export runtime smoke failed")
        args.output.mkdir(mode=0o700)
        with (args.output / "acoustic.onnx").open("xb") as stream:
            stream.write(graph)
            stream.flush()
            os.fsync(stream.fileno())
        controls = ([dict(name="breathiness", type="float32", shape=[1, "T"],
                          unit="normalized-periodic-aperiodic-balance", minimum=0, maximum=1,
                          default=0, supported=True)] if conditioned else [])
        report = dict(formatId="com.project-seam.acoustic-export", schemaVersion=2,
                      acousticPath="acoustic.onnx", acousticSha256=hashlib.sha256(graph).hexdigest(),
                      acousticBytes=len(graph), checkpointReceiptSha256=args.receipt_sha256,
                      checkpointSha256=receipt["checkpointSha256"], revision=REVISION,
                      profile=profile, profileSha256=state["metadata"]["profileSha256"], vocabulary=vocabulary,
                      inspection=inspection, runtimeSmokePassed=True, runtimeVersion=ort.__version__,
                      encoderRuntimeCheck=encoder_check, deploymentBridgeCheck=deployment_check,
                      conditioningRevision=2,
                      conditioningControls=controls,
                      sourceRightsRevalidated=False, modelBundleAdmitted=False, singerQualified=False,
                      releaseEligible=False)
        publish_new(args.output / "export.json", report)
        print(json.dumps(dict(acousticSha256=report["acousticSha256"], acousticBytes=len(graph),
                              runtimeSmokePassed=True, releaseEligible=False)))
        return 0
    except KeyboardInterrupt:
        return 130
    except (ValueError, OSError, RuntimeError, ImportError, RecursionError, subprocess.SubprocessError) as error:
        print(str(error)[:256], file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
