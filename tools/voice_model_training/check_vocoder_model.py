"""Check pinned vocoder training/deployment architectures using original fixtures.

Executes trusted upstream Python source. No pretrained weights are downloaded.
This is not GAN training, an admitted voice dataset, or singing qualification.
"""
import argparse
import hashlib
import importlib.util
import io
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

TRAINING_REVISION = "4d0889c4c180c75ad3000cc565864656344f8190"
DEPLOYMENT_REVISION = "336cf01b57f2ad44c6b37a79cf33993043291759"


def vocoder_configuration(profile="mini-nsf-32-smoke-v1"):
    """Named architectures shared by training and strict checkpoint export.

    The larger profile adopts the pinned upstream channel/kernel capacity, not
    pretrained weights or a claim of quality. Acoustic geometry stays SEAM's.
    """
    if profile not in ("mini-nsf-32-smoke-v1", "mini-nsf-512-mrf-v1"):
        raise ValueError("Unsupported vocoder architecture profile")
    large = profile == "mini-nsf-512-mrf-v1"
    return dict(sampling_rate=48000, num_mels=80, hop_size=256, n_fft=1024,
                win_size=1024, fmin=20, fmax=24000, mini_nsf=True, noise_sigma=0.,
                upsample_rates=[8, 8, 2, 2], upsample_kernel_sizes=[16, 16, 4, 4],
                upsample_initial_channel=512 if large else 32,
                resblock_kernel_sizes=[3, 7, 11] if large else [3],
                resblock_dilation_sizes=[[1, 3, 5] for _ in range(3 if large else 1)],
                resblock="1", pc_aug=False)


def summarize_pitch_conditioning(cases):
    """Separate a responsive pitch diagnostic from accurate note following.

    This remains synthetic, target-windowed autocorrelation, not an independent
    held-out singing-quality gate. Missing/low-coverage cases cannot pass.
    """
    expected = (110.0, 220.0, 440.0, 880.0)
    maximum_error, minimum_coverage = 50.0, 0.8
    if len(cases) != len(expected) or tuple(c.get("requestedHz") for c in cases) != expected:
        raise ValueError("Pitch diagnostic requires all four ordered reference notes")
    annotated = []
    for case in cases:
        measured, coverage = case.get("measuredHz"), case.get("voicedCoverage")
        if (type(coverage) not in (int, float) or not math.isfinite(coverage) or
                not 0 <= coverage <= 1 or type(case.get("finite")) is not bool):
            raise ValueError("Invalid pitch diagnostic coverage or finiteness")
        if measured is not None and (type(measured) not in (int, float) or
                                     not math.isfinite(measured) or measured <= 0):
            raise ValueError("Invalid measured pitch")
        error = None if measured is None else abs(1200 * math.log2(measured / case["requestedHz"]))
        passed = (case["finite"] and case.get("measurementReason") is None and error is not None
                  and error <= maximum_error and coverage >= minimum_coverage)
        annotated.append(dict(case, absoluteErrorCents=error, withinTolerance=bool(passed)))
    pitches = {case["measuredHz"] for case in annotated if case["measuredHz"] is not None}
    return dict(conditioning=annotated, pitchChangesWithRequestedNote=len(pitches) > 1,
                pitchFollowsRequestedNote=all(case["withinTolerance"] for case in annotated),
                pitchDiagnosticRevision=2, maximumPitchErrorCents=maximum_error,
                minimumVoicedCoverage=minimum_coverage,
                pitchEstimator="target-windowed-autocorrelation-diagnostic-only")


def export_checked_onnx(adapter):
    import onnx
    import onnxruntime as ort
    import torch
    import numpy as np
    from .qualification import measure_median_pitch_hz
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
    conditioning = []
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
        # Parity says the graph computes the same thing as PyTorch; it says nothing
        # about whether pitch conditioning reaches the output as pitch. A vocoder
        # whose harmonic excitation is not yet learned emits a fixed frame-rate tone
        # and still passes parity perfectly, so the note it actually sings has to be
        # measured. The predicted frequency is reported rather than asserted against
        # a threshold: an undertrained vocoder is a state this export must be able to
        # describe, and a hard check here would make the honest case unrepresentable.
        frames = 64
        mel = torch.linspace(-6, -2, frames * 80).reshape(1, frames, 80)
        for hz in (110.0, 220.0, 440.0, 880.0):
            f0 = torch.full((1, frames), hz)
            audio = session.run(["waveform"], {"mel": mel.numpy(), "f0": f0.numpy()})[0]
            rendered, coverage, reason = measure_median_pitch_hz(
                np.asarray(audio, dtype=np.float64).ravel(), 48000, hz)
            conditioning.append(dict(
                requestedHz=hz,
                measuredHz=None if rendered is None else float(rendered),
                voicedCoverage=float(coverage),
                measurementReason=reason,
                finite=bool(np.isfinite(audio).all())))
    pitch_summary = summarize_pitch_conditioning(conditioning)
    return payload, dict(passed=True, graphBytes=len(payload), graphSha256=inspection["sha256"],
                cases=cases, **pitch_summary,
                graphRetained=False, deterministicMiniNSFOnly=True)


def check_onnx(adapter):
    return export_checked_onnx(adapter)[1]


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
    parser.add_argument("--check-gan", action="store_true", help="Run one real MiniNSF GAN mechanics step on synthetic PCM")
    parser.add_argument("--check-resume", action="store_true", help="Verify complete upstream GAN checkpoint continuation; requires --check-gan")
    args = parser.parse_args()
    if args.check_resume and not args.check_gan:
        parser.error("--check-resume requires --check-gan")
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
        gan = None
        if mini and args.check_gan:
            from tools.voice_model_training.vocoder_optimization import vocoder_gan_step
            mel_spec = importlib.util.spec_from_file_location("seam_checked_vocoder_mel",
                                                            training / "utils/wav2mel.py")
            mel_module = importlib.util.module_from_spec(mel_spec)
            mel_spec.loader.exec_module(mel_module)
            transform = mel_module.PitchAdjustableMelSpectrogram(
                sample_rate=48000, n_fft=1024, win_length=1024, hop_length=256,
                f_min=20, f_max=24000, n_mels=80)
            def logarithmic_mel(audio):
                return transform(audio.squeeze(1)).clamp_min(1e-5).log()
            pcm = target.reshape(1, 1, -1)
            conditioned_mel = logarithmic_mel(pcm)
            discriminators = [source.MultiScaleDiscriminator(), source.MultiPeriodDiscriminator([3, 5])]
            discriminator_optimizer = torch.optim.AdamW(
                [p for d in discriminators for p in d.parameters()], lr=1e-4, betas=(.8, .99), weight_decay=0)
            generator_optimizer = torch.optim.AdamW(model.parameters(), lr=1e-4, betas=(.8, .99), weight_decay=0)
            gan = vocoder_gan_step(model, discriminators, generator_optimizer, discriminator_optimizer,
                mel=conditioned_mel, f0=f0, pcm=pcm, hop_size=256, partition="train",
                reconstruction_loss=lambda generated, real: (logarithmic_mel(generated) - logarithmic_mel(real)).abs().mean())
            if args.check_resume:
                from tools.voice_model_training.vocoder_checkpoint import publish_vocoder_checkpoint, restore_vocoder_checkpoint
                metadata = dict(trainingRevision=TRAINING_REVISION, deploymentRevision=DEPLOYMENT_REVISION,
                                configuration=config, syntheticInputs=True, fixtureOnly=True)
                def next_step():
                    return vocoder_gan_step(model, discriminators, generator_optimizer, discriminator_optimizer,
                        mel=conditioned_mel, f0=f0, pcm=pcm, hop_size=256, partition="train",
                        reconstruction_loss=lambda generated, real: (logarithmic_mel(generated) - logarithmic_mel(real)).abs().mean())
                def owned_state():
                    return {f"{i}.{name}": tensor.detach().clone()
                            for i, owner in enumerate([model, *discriminators])
                            for name, tensor in owner.state_dict().items()}
                with tempfile.TemporaryDirectory(prefix="seam-vocoder-resume-") as temporary:
                    output = Path(temporary) / "checkpoint"
                    receipt = publish_vocoder_checkpoint(model, discriminators, generator_optimizer,
                        discriminator_optimizer, output, metadata=metadata,
                        epoch=dict(epochComplete=True, coverageVerified=True, fixtureOnly=True))
                    digest = hashlib.sha256((output / "checkpoint.json").read_bytes()).hexdigest()
                    continuous = next_step()
                    expected = owned_state()
                    restore_vocoder_checkpoint(model, discriminators, generator_optimizer, discriminator_optimizer,
                        output, receipt_sha256=digest, expected_metadata=metadata)
                    resumed = next_step()
                    if continuous != resumed:
                        raise AssertionError("Upstream GAN resumed losses/gradients differ")
                    for i, owner in enumerate([model, *discriminators]):
                        for name, tensor in owner.state_dict().items():
                            if not torch.equal(tensor, expected[f"{i}.{name}"]):
                                raise AssertionError("Upstream GAN resumed weights/buffers differ")
                    gan["continuation"] = dict(exact=True, checkpointBytes=receipt["checkpointBytes"],
                                                checkpointSha256=receipt["checkpointSha256"],
                                                checkpointRetained=False, fixtureOnly=True)
            adapter.generator.load_state_dict(model.state_dict(), strict=True)
        reports.append(dict(configuration=config, cases=cases, strictStateLoad=True,
                            fixtureLoss=loss.item(), changedParameterTensors=changed,
                            ganStep=gan,
                            onnxRuntime=check_onnx(adapter) if mini and args.check_onnx else None))
    print(json.dumps(dict(passed=True, trainingRevision=TRAINING_REVISION,
                          deploymentRevision=DEPLOYMENT_REVISION, models=reports,
                          syntheticInputs=True, ganStepVerified=args.check_gan, ganTrainingVerified=False,
                          onnxExported=args.check_onnx, singerQualified=False, releaseEligible=False), indent=2))


if __name__ == "__main__":
    main()
