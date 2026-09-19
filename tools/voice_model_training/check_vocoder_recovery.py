"""Captured oscillator-corpus recovery diagnostic, not singer qualification."""
import hashlib
import random

from .vocoder_training_run import train_reviewed_vocoder_epoch


def check_vocoder_recovery(*, root, dataset_inputs, conditioning_directory, targets,
                           profile_sha256, pcm_sources, _process_mode=None):
    """Use real signed admission, PCM/mel joins, optimizers, and checkpoint I/O."""
    import numpy as np
    import torch

    class Generator(torch.nn.Module):
        def __init__(self):
            super().__init__()
            self.gain = torch.nn.Parameter(torch.tensor(.01))

        def forward(self, mel, f0):
            return self.gain * mel[:, :1].repeat_interleave(256, dim=2)

    class Discriminator(torch.nn.Module):
        def __init__(self):
            super().__init__()
            self.conv = torch.nn.Conv1d(1, 2, 3, padding=1)

        def forward(self, pcm):
            features = self.conv(pcm)
            return [features.mean(dim=1)], [[features]]

    def setup():
        torch.manual_seed(71)
        random.seed(73)
        np.random.seed(79)
        g, d = Generator(), Discriminator()
        go, do = torch.optim.AdamW(g.parameters(), lr=.001), torch.optim.AdamW(d.parameters(), lr=.001)
        return g, [d], go, do

    def loss(a, b):
        return (a-b).square().mean() * (random.random() + float(np.random.random()) + torch.rand(()))

    options = dict(dataset_inputs=dataset_inputs, conditioning_directory=conditioning_directory,
        targets=targets, pcm_sources=pcm_sources, expected_profile_sha256=profile_sha256,
        run_metadata=dict(fixtureOnly=True, purpose="captured-corpus-recovery"),
        reconstruction_loss=loss, objective_id="fixture-stochastic-l2", maximum_updates=3,
        maximum_seconds=120, training_segment_frames=16,
        maximum_checkpoint_file_bytes=1024**2, maximum_checkpoint_total_bytes=2*1024**2)
    if _process_mode is not None:
        import os
        if _process_mode not in ("baseline", "interrupt", "resume"):
            raise ValueError("Unknown recovery diagnostic process mode")
        extra = {}
        if _process_mode == "interrupt":
            def hard_exit(event):
                if event["stage"] == "partial-checkpoint":
                    os._exit(73)  # Deliberate hard exit: no Python cleanup or live owner survives.
            extra.update(recovery_directory=root/"process-prefix", checkpoint_interval_updates=1,
                         maximum_recovery_bytes=4*1024**2, on_progress=hard_exit)
        elif _process_mode == "resume":
            saved = root/"process-prefix/update-000001"
            extra.update(resume_partial=saved,
                         resume_partial_sha256=hashlib.sha256((saved/"checkpoint.json").read_bytes()).hexdigest())
        return train_reviewed_vocoder_epoch(*setup(), output=root/f"process-{_process_mode}", **extra, **options)
    baseline = setup()
    expected = train_reviewed_vocoder_epoch(*baseline, output=root/"recovery-baseline", **options)
    expected_rng = (random.random(), float(np.random.random()), torch.rand(3))

    class Interrupted(Exception):
        pass

    def interrupt(event):
        if event["stage"] == "partial-checkpoint":
            raise Interrupted("intentional interruption after durable publication")

    try:
        train_reviewed_vocoder_epoch(*setup(), output=root/"recovery-interrupted",
            recovery_directory=root/"recovery-prefix", checkpoint_interval_updates=1,
            maximum_recovery_bytes=4*1024**2, on_progress=interrupt, **options)
    except Interrupted:
        pass
    else:
        raise AssertionError("Captured corpus did not publish a partial checkpoint")
    saved = root/"recovery-prefix/update-000001"
    digest = hashlib.sha256((saved/"checkpoint.json").read_bytes()).hexdigest()
    resumed = setup()
    events = []
    actual = train_reviewed_vocoder_epoch(*resumed, output=root/"recovery-resumed",
        resume_partial=saved, resume_partial_sha256=digest,
        recovery_directory=root/"recovery-continued", checkpoint_interval_updates=1,
        retain_partial_checkpoints=1, maximum_recovery_bytes=4*1024**2,
        on_progress=events.append, **options)
    if (actual["epoch"] != expected["epoch"] or actual["epoch"]["updates"] != 3
            or random.random() != expected_rng[0] or float(np.random.random()) != expected_rng[1]
            or not torch.equal(torch.rand(3), expected_rng[2])):
        raise AssertionError("Captured-corpus continuation differs from uninterrupted execution")
    for old, new in ((baseline[0], resumed[0]), (baseline[1][0], resumed[1][0])):
        if any(not torch.equal(value, new.state_dict()[key]) for key, value in old.state_dict().items()):
            raise AssertionError("Captured-corpus resumed tensors differ")
    def equal(left, right):
        if isinstance(left, torch.Tensor):
            return isinstance(right, torch.Tensor) and torch.equal(left, right)
        if type(left) is not type(right):
            return False
        if isinstance(left, dict):
            return left.keys() == right.keys() and all(equal(left[k], right[k]) for k in left)
        if isinstance(left, (list, tuple)):
            return len(left) == len(right) and all(equal(a, b) for a, b in zip(left, right))
        return left == right
    if any(not equal(baseline[i].state_dict(), resumed[i].state_dict()) for i in (2, 3)):
        raise AssertionError("Captured-corpus resumed optimizer state differs")
    restored = [e for e in events if e["stage"] == "partial-restored"]
    if len(restored) != 1 or restored[0]["completedUpdates"] != 1:
        raise AssertionError("Partial prefix was not restored exactly once")
    from .check_vocoder_recovery_process import check_process_recovery
    process = check_process_recovery(root=root, dataset_inputs=dataset_inputs,
        conditioning_directory=conditioning_directory, targets=targets,
        profile_sha256=profile_sha256, pcm_sources=pcm_sources)
    return dict(passed=True, capturedCorpus=True, admissionMocked=False, batchReaderMocked=False,
        optimizerMocked=False, diskGuardMocked=False, interruptedAfterUpdates=1,
        completedUpdates=3, validSamples=actual["epoch"]["validSamples"],
        exactEpochMetrics=True, exactModelTensors=True, exactOptimizerState=True, exactRng=True,
        processRecovery=process,
        syntheticInputs=True, fixturePolicyOnly=True, singerQualified=False, releaseEligible=False)
