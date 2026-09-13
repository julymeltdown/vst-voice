"""Check pinned vocoder training/deployment architectures using original fixtures.

Executes trusted upstream Python source. No pretrained weights are downloaded.
This is not GAN training, an admitted voice dataset, or singing qualification.
"""
import argparse
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys

TRAINING_REVISION = "4d0889c4c180c75ad3000cc565864656344f8190"
DEPLOYMENT_REVISION = "336cf01b57f2ad44c6b37a79cf33993043291759"


def check_onnx(adapter):
    import onnx
    import onnxruntime as ort
    import torch
    adapter.eval()
    stream = io.BytesIO()
    torch.onnx.export(adapter, (torch.full((1, 16, 80), -4.), torch.full((1, 16), 220.)),
                      stream, input_names=["mel", "f0"], output_names=["waveform"],
                      dynamic_axes={"mel": {1: "n_frames"}, "f0": {1: "n_frames"},
                                    "waveform": {1: "n_samples"}},
                      opset_version=17, dynamo=False, external_data=False)
    graph = onnx.load_model_from_string(stream.getvalue())
    # This bridge supports batch one and mono audio only, as executed below.
    graph.graph.output[0].type.tensor_type.shape.dim[0].dim_value = 1
    onnx.checker.check_model(graph, full_check=True)
    payload = graph.SerializeToString()
    from tools.neural_runtime.inspect_graph import inspect_bytes
    inspection = inspect_bytes(payload)
    ort.disable_telemetry_events()
    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    session = ort.InferenceSession(payload, options, providers=["CPUExecutionProvider"])
    cases = []
    with torch.no_grad():
        for frames in (1, 3, 16, 23):
            mel = torch.linspace(-8, -1, frames * 80).reshape(1, frames, 80)
            f0 = torch.linspace(180, 260, frames).reshape(1, frames)
            f0[:, 0] = 0
            expected = adapter(mel, f0)
            actual = torch.from_numpy(session.run(["waveform"], {"mel": mel.numpy(), "f0": f0.numpy()})[0])
            if tuple(actual.shape) != (1, frames * 256) or not torch.isfinite(actual).all():
                raise AssertionError("Exported vocoder PCM geometry or values invalid")
            torch.testing.assert_close(actual, expected, atol=1e-5, rtol=1e-5)
            cases.append(dict(frames=frames, samples=actual.shape[1],
                              maximumError=(actual - expected).abs().max().item()))
    return dict(passed=True, graphBytes=len(payload), graphSha256=inspection["sha256"],
                cases=cases, graphRetained=False, deterministicMiniNSFOnly=True)


def trusted_checkout(path, revision):
    path = path.resolve(strict=True)
    def git(*args):
        return subprocess.check_output(["git", "-C", str(path), *args], text=True, timeout=15).strip()
    if git("rev-parse", "HEAD") != revision or git("status", "--porcelain"):
        raise ValueError("Require a clean trusted checkout at the captured revision")
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("training_checkout", type=Path)
    parser.add_argument("deployment_checkout", type=Path)
    parser.add_argument("--check-onnx", action="store_true", help="Export and compare deterministic MiniNSF only")
    args = parser.parse_args()
    training = trusted_checkout(args.training_checkout, TRAINING_REVISION)
    deployment = trusted_checkout(args.deployment_checkout, DEPLOYMENT_REVISION)
    import torch
    torch.set_num_threads(1)
    # Load the self-contained generator definitions without importing upstream
    # dataset selection, checkpoint loaders or training orchestration.
    spec = importlib.util.spec_from_file_location("seam_checked_vocoder_models",
                                                 training / "models/nsf_HiFigan/models.py")
    source = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(source)
    sys.path.insert(0, str(deployment))
    from deployment.modules.nsf_hifigan import NSFHiFiGANONNX

    reports = []
    for mini in (False, True):
        config = dict(sampling_rate=48000, num_mels=80, hop_size=256, n_fft=1024,
                      win_size=1024, fmin=20, fmax=24000, mini_nsf=mini,
                      noise_sigma=0.0, upsample_rates=[8, 8, 2, 2],
                      upsample_kernel_sizes=[16, 16, 4, 4], upsample_initial_channel=32,
                      resblock_kernel_sizes=[3], resblock_dilation_sizes=[[1, 3, 5]],
                      resblock="1", pc_aug=False)
        torch.manual_seed(918)
        model = source.Generator(source.AttrDict(config))
        adapter = NSFHiFiGANONNX(config)
        adapter.generator.load_state_dict(model.state_dict(), strict=True)
        model.eval()
        adapter.eval()
        cases = []
        with torch.no_grad():
            for frames in (1, 3, 16, 23):
                mel = torch.linspace(-8, -1, frames * 80).reshape(1, frames, 80)
                f0 = torch.linspace(180, 260, frames).reshape(1, frames)
                if frames > 1:
                    f0[:, 0] = 0  # Include a genuinely unvoiced conditioning frame.
                torch.manual_seed(919)
                reference = model(mel.transpose(1, 2), f0).squeeze(1)
                torch.manual_seed(919)
                actual = adapter(mel, f0)
                if tuple(actual.shape) != (1, frames * 256) or not torch.isfinite(actual).all():
                    raise AssertionError("Vocoder returned invalid PCM geometry or values")
                error = (actual - reference).abs().max().item()
                torch.testing.assert_close(actual, reference, atol=1e-6, rtol=1e-6)
                cases.append(dict(frames=frames, samples=actual.shape[1], maximumError=error))

        # A single supervised mechanics step proves gradient connectivity only.
        # Real vocoder training needs GAN losses, admitted PCM and full GAN state.
        model.train()
        optimizer = torch.optim.AdamW(model.parameters(), lr=1e-4)
        before = {name: value.detach().clone() for name, value in model.named_parameters()}
        mel = torch.full((1, 80, 16), -4.0)
        f0 = torch.full((1, 16), 220.0)
        target = .1 * torch.sin(torch.arange(4096) * (2 * torch.pi * 220 / 48000))
        optimizer.zero_grad(set_to_none=True)
        predicted = model(mel, f0)
        loss = (predicted - target.reshape(1, 1, -1)).abs().mean()
        if not torch.isfinite(loss):
            raise AssertionError("Nonfinite fixture loss")
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0, error_if_nonfinite=True)
        optimizer.step()
        changed = sum(not torch.equal(before[name], value) for name, value in model.named_parameters())
        if not changed or not all(torch.isfinite(p).all() for p in model.parameters()):
            raise AssertionError("Vocoder optimization did not produce finite changed weights")
        adapter.generator.load_state_dict(model.state_dict(), strict=True)
        reports.append(dict(configuration=config, cases=cases, strictStateLoad=True,
                            fixtureLoss=loss.item(), changedParameterTensors=changed,
                            onnxRuntime=check_onnx(adapter) if mini and args.check_onnx else None))
    print(json.dumps(dict(passed=True, trainingRevision=TRAINING_REVISION,
                          deploymentRevision=DEPLOYMENT_REVISION, models=reports,
                          syntheticInputs=True, ganTrainingVerified=False,
                          onnxExported=args.check_onnx, singerQualified=False, releaseEligible=False), indent=2))


if __name__ == "__main__":
    main()
