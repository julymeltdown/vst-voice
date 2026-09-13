"""Real vocoder epoch diagnostic under explicitly synthetic fixture policies."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile

from .check_vocoder_model import TRAINING_REVISION, trusted_checkout
from .vocoder_checkpoint import restore_vocoder_checkpoint
from .vocoder_training_run import train_reviewed_vocoder_epoch


def check_reviewed_vocoder(checkout, *, root, dataset_inputs, conditioning_directory,
                           targets, profile_sha256, pcm_sources):
    import torch
    checkout = trusted_checkout(checkout, TRAINING_REVISION)
    def module(name, path):
        spec = importlib.util.spec_from_file_location(name, checkout / path)
        value = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(value)
        return value
    source = module("seam_reviewed_vocoder_architecture", "models/nsf_HiFigan/models.py")
    mel_module = module("seam_reviewed_vocoder_mel", "utils/wav2mel.py")
    config = dict(sampling_rate=48000, num_mels=80, hop_size=256, n_fft=1024,
        win_size=1024, fmin=20, fmax=24000, mini_nsf=True, noise_sigma=0.,
        upsample_rates=[8, 8, 2, 2], upsample_kernel_sizes=[16, 16, 4, 4],
        upsample_initial_channel=32, resblock_kernel_sizes=[3],
        resblock_dilation_sizes=[[1, 3, 5]], resblock="1", pc_aug=False)
    profile = next(iter(targets.values()))[0]["profile"]
    for key, expected in dict(sampleRate=48000, bins=80, hopSize=256, fftSize=1024,
                              windowSize=1024, minimumHz=20, maximumHz=24000).items():
        if profile[key] != expected:
            raise ValueError("Reviewed vocoder fixture profile differs")
    torch.manual_seed(928)
    generator = source.Generator(source.AttrDict(config))
    discriminators = [source.MultiScaleDiscriminator(), source.MultiPeriodDiscriminator([3, 5])]
    go = torch.optim.AdamW(generator.parameters(), lr=1e-4, betas=(.8, .99), weight_decay=0)
    do = torch.optim.AdamW([p for d in discriminators for p in d.parameters()],
                          lr=1e-4, betas=(.8, .99), weight_decay=0)
    transform = mel_module.PitchAdjustableMelSpectrogram(sample_rate=48000,
        n_fft=1024, win_length=1024, hop_length=256, f_min=20, f_max=24000, n_mels=80)
    def reconstruction(generated, real):
        return (transform(generated.squeeze(1)).clamp_min(1e-5).log() -
                transform(real.squeeze(1)).clamp_min(1e-5).log()).abs().mean()
    options = dict(dataset_inputs=dataset_inputs, conditioning_directory=conditioning_directory,
        targets=targets, pcm_sources=pcm_sources, expected_profile_sha256=profile_sha256,
        run_metadata=dict(configuration=config, trainingRevision=TRAINING_REVISION,
                          syntheticInputs=True, fixturePolicyOnly=True),
        reconstruction_loss=reconstruction, objective_id="nsf-lsgan-logmel-48k80-v1", maximum_updates=1)
    def epoch(name, expected_dataset=None):
        return train_reviewed_vocoder_epoch(generator, discriminators, go, do,
            output=root / name, expected_dataset_sha256=expected_dataset, **options)
    first = epoch("vocoder-first")
    receipt_sha = hashlib.sha256((root / "vocoder-first/checkpoint.json").read_bytes()).hexdigest()
    # Reference checkpoint bytes are temporary; keep only the receipt and the
    # comparison tensors after checking the complete publication path.
    with tempfile.TemporaryDirectory(prefix="vocoder-reference-", dir=root) as temporary:
        continuous = epoch(Path(temporary) / "checkpoint", first["epoch"]["datasetSha256"])
        expected_state = {f"{i}.{name}": value.detach().clone()
                          for i, owner in enumerate([generator, *discriminators])
                          for name, value in owner.state_dict().items()}
    metadata = {key: value for key, value in first["metadata"].items() if key != "ganCheckpoint"}
    restore_vocoder_checkpoint(generator, discriminators, go, do, root / "vocoder-first",
                              receipt_sha256=receipt_sha, expected_metadata=metadata)
    resumed = epoch("vocoder-resumed", first["epoch"]["datasetSha256"])
    if continuous["epoch"] != resumed["epoch"]:
        raise AssertionError("Reviewed vocoder resumed epoch differs")
    for i, owner in enumerate([generator, *discriminators]):
        for name, value in owner.state_dict().items():
            if not torch.equal(value, expected_state[f"{i}.{name}"]):
                raise AssertionError("Reviewed vocoder resumed state differs")
    return dict(passed=True, firstEpoch=first["epoch"], resumedEpoch=resumed["epoch"],
                continuationExact=True, checkpointBytes=first["checkpointBytes"],
                syntheticInputs=True, fixturePolicyOnly=True, checkpointRetained=False,
                singerQualified=False, releaseEligible=False)
