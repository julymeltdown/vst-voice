"""Run the real pinned upstream architecture on synthetic engineering inputs."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import wave

REVISION = "336cf01b57f2ad44c6b37a79cf33993043291759"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trusted_checkout", type=Path)
    args = parser.parse_args()
    checkout = args.trusted_checkout.resolve(strict=True)
    def git(*arguments):
        return subprocess.check_output(["git", "-C", str(checkout), *arguments], text=True, timeout=10).strip()
    if git("rev-parse", "HEAD") != REVISION or git("status", "--porcelain"):
        raise ValueError("Select a clean trusted checkout at the pinned DiffSinger revision")
    import numpy as np
    import torch
    from tools.voice_model_training.optimization import acoustic_training_step, acoustic_evaluation_step, run_acoustic_epoch
    from tools.voice_model_training.diffsinger_objective import DiffSingerDDPMObjective
    from tools.voice_model_training.acoustics import wav_log_mel_targets
    from tools.voice_model_training.checkpoint import publish_checkpoint, load_local_checkpoint
    # Original oscillator fixture only: no human identity, borrowed voice or claimed lyric supervision.
    positions = np.arange(4096) / 48000
    signal = .2 * np.sin(2 * np.pi * 220 * positions) + .05 * np.sin(2 * np.pi * 440 * positions)
    wav_bytes = io.BytesIO()
    with wave.open(wav_bytes, "wb") as writer:
        writer.setnchannels(1)
        writer.setsampwidth(2)
        writer.setframerate(48000)
        writer.writeframes(np.rint(signal * 32767).astype("<i2").tobytes())
    source_bytes = wav_bytes.getvalue()
    acoustic, mel_targets = wav_log_mel_targets(source_bytes,
        expected_sha256=hashlib.sha256(source_bytes).hexdigest(), sample_rate=48000)
    # This standalone process deliberately isolates upstream global module/hparams state.
    sys.path.insert(0, str(checkout))
    from utils.hparams import hparams
    config = dict(hidden_size=32, enc_layers=1, enc_ffn_kernel_size=3, ffn_act="gelu",
                  dropout=0., num_heads=2, use_pos_embed=True, use_spk_id=False,
                  diffusion_type="ddpm", use_shallow_diffusion=False, timesteps=8, K_step=8,
                  backbone_type="wavenet", backbone_args=dict(num_layers=2, num_channels=32, dilation_cycle_length=2),
                  spec_min=[-12], spec_max=[0], schedule_type="linear", max_beta=.02,
                  diff_speedup=2, diff_accelerator="ddim", infer=False)
    hparams.clear()
    hparams.update(config)
    from modules.toplevel import DiffSingerAcoustic
    torch.set_num_threads(1)
    torch.manual_seed(17)
    model = DiffSingerAcoustic(vocab_size=4, out_dims=80)
    optimizer = torch.optim.AdamW(model.parameters(), lr=.001)
    before = {key: value.detach().clone() for key, value in model.named_parameters()}
    batch = dict(sourceId="synthetic-architecture-check-not-a-singer", partition="train", hopSize=256,
                 frameOffset=0, phraseAnalysisFrames=16, tokens=[1, 2, 3], mel2ph=[1]*5 + [2]*6 + [3]*5,
                 columns=dict(phoneId=[1]*5 + [2]*6 + [3]*5, f0Hz=[220.]*16, voiced=[True]*16,
                              midi=[57]*16, rest=[False]*16, slur=[False]*16, validSamples=[256]*16),
                 melTargets=mel_targets)
    objective = DiffSingerDDPMObjective("l2")
    losses = []
    # Bind the actual engineering inputs without pretending this is signed dataset admission.
    engineering_identity = dict(acoustic=acoustic, tokens=batch["tokens"], mel2ph=batch["mel2ph"], columns=batch["columns"])
    dataset_hash = hashlib.sha256(json.dumps(engineering_identity, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    batch.update(datasetSha256=dataset_hash, profileSha256=acoustic["profileSha256"],
                 coreFrameOffset=0, coreFrameCount=16, lossMask=[True] * 16)
    for _ in range(8):
        torch.manual_seed(77)  # Same synthetic noise/timestep, not held-out evaluation.
        epoch = run_acoustic_epoch(model, optimizer, [batch], expected_dataset_sha256=dataset_hash,
            expected_profile_sha256=acoustic["profileSha256"], vocabulary_size=3, maximum_updates=1,
            expected_source_frames={batch["sourceId"]: 16},
            on_step=lambda result: losses.append(result["loss"]), objective=objective, objective_id=objective.objective_id)
    changed = sum(not torch.equal(before[key], value.detach()) for key, value in model.named_parameters())
    rng_before_evaluation = torch.get_rng_state().clone()
    # Reuse the engineering fixture only to test evaluation mechanics, not held-out quality.
    evaluation = acoustic_evaluation_step(model, dict(batch, partition="validation"), vocabulary_size=3,
        seed=501, objective=objective, objective_id=objective.objective_id)
    evaluation_isolated = torch.equal(torch.get_rng_state(), rng_before_evaluation) and model.training
    # This is a self-produced temporary checkpoint, not an untrusted checkpoint importer.
    # Keep optimizer and CPU RNG state so continuation can be compared exactly.
    with tempfile.TemporaryDirectory(prefix="seam-model-roundtrip-") as temporary:
        checkpoint_directory = Path(temporary) / "checkpoint"
        publication = publish_checkpoint(model, optimizer, checkpoint_directory, epoch=epoch,
            metadata=dict(configuration=config, revision=REVISION, completedSteps=8,
                          syntheticInputs=True, engineeringIdentity=engineering_identity))
        checkpoint = checkpoint_directory / publication["checkpointPath"]
        checkpoint_bytes = checkpoint.read_bytes()
        checkpoint_hash = hashlib.sha256(checkpoint_bytes).hexdigest()
        if checkpoint_hash != publication["checkpointSha256"]:
            raise ValueError("Published checkpoint bytes differ")
        receipt_hash = hashlib.sha256((checkpoint_directory / "checkpoint.json").read_bytes()).hexdigest()
        restored, loaded_publication = load_local_checkpoint(checkpoint_directory, receipt_sha256=receipt_hash)
        if loaded_publication != publication:
            raise ValueError("Checkpoint receipt round trip differs")
        restored_metadata = restored["metadata"]
        if (restored_metadata["revision"] != REVISION or restored_metadata["configuration"] != config
                or restored_metadata["completedSteps"] != 8 or restored_metadata["engineeringIdentity"] != engineering_identity):
            raise ValueError("Checkpoint experiment identity changed")
        resumed_model = DiffSingerAcoustic(vocab_size=4, out_dims=80)
        resumed_model.load_state_dict(restored["model"], strict=True)
        resumed_optimizer = torch.optim.AdamW(resumed_model.parameters(), lr=.001)
        resumed_optimizer.load_state_dict(restored["optimizer"])
    model.eval()
    resumed_model.eval()
    with torch.no_grad():
        torch.manual_seed(91)
        output = model(torch.tensor([batch["tokens"]]), mel2ph=torch.tensor([batch["mel2ph"]]),
                       f0=torch.full((1, 16), 220.), infer=True).diff_out
        torch.manual_seed(91)
        restored_output = resumed_model(torch.tensor([batch["tokens"]]), mel2ph=torch.tensor([batch["mel2ph"]]),
                                       f0=torch.full((1, 16), 220.), infer=True).diff_out
    inference_equal = torch.equal(output, restored_output)
    continuation = []
    for candidate, candidate_optimizer in ((model, optimizer), (resumed_model, resumed_optimizer)):
        torch.set_rng_state(restored["rng"])
        continuation.append(acoustic_training_step(candidate, candidate_optimizer, batch, vocabulary_size=3,
                            objective=objective, objective_id=objective.objective_id)["loss"])
    resumed_equal = (continuation[0] == continuation[1]
                     and all(torch.equal(value, resumed_model.state_dict()[key]) for key, value in model.state_dict().items()))
    passed = (changed > 0 and losses[-1] < losses[0] and tuple(output.shape) == (1, 16, 80)
              and bool(torch.isfinite(output).all()) and inference_equal and resumed_equal and evaluation_isolated)
    from tools.voice_model_training.check_reviewed_run import check_reviewed_run
    reviewed_run = check_reviewed_run(model, optimizer, objective=objective,
                                     model_metadata=dict(configuration=config, revision=REVISION),
                                     trusted_checkout=checkout)
    passed = passed and reviewed_run["passed"]
    print(json.dumps(dict(revision=REVISION, torch=torch.__version__, numpy=np.__version__, configuration=config,
                         parameterCount=sum(p.numel() for p in model.parameters()), changedParameterTensors=changed,
                         losses=losses, inferenceShape=list(output.shape), passed=passed,
                         finalEpoch=epoch, completedEpochs=8,
                         acousticTarget=acoustic,
                         evaluation=evaluation, evaluationStatePreserved=evaluation_isolated,
                         evaluationUsesTrainingFixture=True,
                         checkpointBytes=len(checkpoint_bytes), checkpointSha256=checkpoint_hash,
                         restoredInferenceExact=inference_equal, continuationLosses=continuation,
                         resumedUpdateExact=resumed_equal, checkpointRetained=False,
                         reviewedRun=reviewed_run,
                         syntheticInputs=True, singerQualified=False, releaseEligible=False), indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
