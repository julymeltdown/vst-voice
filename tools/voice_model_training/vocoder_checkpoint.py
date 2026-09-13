"""Complete local CPU GAN checkpoints over the captured checkpoint transport.

No third-party checkpoint import or source authorization is provided. Caller must
re-admit the dataset before continuing and discard objects after a failed restore.
"""
import random

from .checkpoint import load_local_checkpoint, publish_checkpoint


def _owners(generator, discriminators, generator_optimizer, discriminator_optimizer):
    import torch
    if not discriminators:
        raise ValueError("GAN checkpoint requires discriminators")
    owners = torch.nn.ModuleDict({"generator": generator, **{
        f"discriminator_{i}": model for i, model in enumerate(discriminators)}})
    gp = list(generator.parameters())
    dp = [p for d in discriminators for p in d.parameters()]
    if len({id(p) for p in gp + dp}) != len(gp + dp):
        raise ValueError("GAN checkpoint parameters must have unique owners")
    for parameters, optimizer in ((gp, generator_optimizer), (dp, discriminator_optimizer)):
        actual = [p for group in optimizer.param_groups for p in group["params"]]
        if not parameters or len(actual) != len(parameters) or {id(p) for p in actual} != {id(p) for p in parameters}:
            raise ValueError("GAN checkpoint optimizer ownership differs")
    return owners


def _metadata(metadata, count, go, do, schedulers):
    import numpy as np
    if not isinstance(metadata, dict) or "ganCheckpoint" in metadata:
        raise ValueError("Caller metadata must not override GAN checkpoint identity")
    def identity(value):
        return f"{type(value).__module__}.{type(value).__qualname__}"
    return dict(metadata, ganCheckpoint=dict(schemaVersion=1, discriminatorCount=count,
        numpyVersion=str(np.__version__), boundary="complete-epoch",
        optimizerTypes=dict(generator=identity(go), discriminator=identity(do)),
        schedulerTypes={name: identity(value) for name, value in schedulers.items()}))


class _TrainingState:
    def __init__(self, go, do, schedulers):
        self.go, self.do, self.schedulers = go, do, schedulers

    def state_dict(self):
        import numpy as np
        algorithm, keys, position, has_gauss, cached = np.random.get_state()
        return dict(generator=self.go.state_dict(), discriminator=self.do.state_dict(),
                    schedulers={name: value.state_dict() for name, value in self.schedulers.items()},
                    pythonRng=random.getstate(), numpyRng=dict(algorithm=algorithm,
                    keys=keys.tolist(), position=position, hasGauss=has_gauss, cachedGaussian=cached))


def _schedulers(schedulers, go, do):
    schedulers = {} if schedulers is None else dict(schedulers)
    if not set(schedulers) <= {"generator", "discriminator"}:
        raise ValueError("Only explicitly named generator/discriminator schedulers are supported")
    for name, value in schedulers.items():
        if value.optimizer is not (go if name == "generator" else do):
            raise ValueError("GAN scheduler optimizer ownership differs")
    return schedulers


def publish_vocoder_checkpoint(generator, discriminators, generator_optimizer, discriminator_optimizer,
                               output, *, metadata, epoch, schedulers=None,
                               maximum_bytes=512 * 1024 * 1024, before_publish=None):
    owners = _owners(generator, discriminators, generator_optimizer, discriminator_optimizer)
    schedulers = _schedulers(schedulers, generator_optimizer, discriminator_optimizer)
    return publish_checkpoint(owners, _TrainingState(generator_optimizer, discriminator_optimizer, schedulers),
        output, metadata=_metadata(metadata, len(discriminators), generator_optimizer, discriminator_optimizer, schedulers), epoch=epoch,
        maximum_bytes=maximum_bytes, before_publish=before_publish)


def restore_vocoder_checkpoint(generator, discriminators, generator_optimizer, discriminator_optimizer,
                               directory, *, receipt_sha256, expected_metadata, schedulers=None,
                               maximum_bytes=512 * 1024 * 1024):
    import numpy as np
    import torch
    owners = _owners(generator, discriminators, generator_optimizer, discriminator_optimizer)
    schedulers = _schedulers(schedulers, generator_optimizer, discriminator_optimizer)
    state, receipt = load_local_checkpoint(directory, receipt_sha256=receipt_sha256, maximum_bytes=maximum_bytes)
    if state["metadata"] != _metadata(expected_metadata, len(discriminators), generator_optimizer, discriminator_optimizer, schedulers):
        raise ValueError("GAN checkpoint metadata differs from expected run identity")
    training = state["optimizer"]
    if (not isinstance(training, dict) or set(training) != {
            "generator", "discriminator", "schedulers", "pythonRng", "numpyRng"}
            or set(training["schedulers"]) != set(schedulers)):
        raise ValueError("GAN checkpoint training-state owners differ")
    nr = training["numpyRng"]
    if (not isinstance(nr, dict) or set(nr) != {"algorithm", "keys", "position", "hasGauss", "cachedGaussian"}
            or nr["algorithm"] != "MT19937" or len(nr["keys"]) != 624):
        raise ValueError("Unsupported NumPy RNG state")
    numpy_state = (nr["algorithm"], np.array(nr["keys"], dtype=np.uint32),
                   nr["position"], nr["hasGauss"], nr["cachedGaussian"])
    # Validate RNG payloads with private instances before touching global state.
    random.Random().setstate(training["pythonRng"])
    np.random.RandomState().set_state(numpy_state)
    torch.Generator(device="cpu").set_state(state["rng"])
    owners.load_state_dict(state["model"], strict=True)
    generator_optimizer.load_state_dict(training["generator"])
    discriminator_optimizer.load_state_dict(training["discriminator"])
    for name, scheduler in schedulers.items():
        scheduler.load_state_dict(training["schedulers"][name])
    random.setstate(training["pythonRng"])
    np.random.set_state(numpy_state)
    torch.set_rng_state(state["rng"])
    return receipt
