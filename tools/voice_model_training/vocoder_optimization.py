"""Alternating least-squares GAN update for trusted vocoder training services.

Caller owns fresh source admission and checkpoint publication. An exception after
the discriminator step invalidates this in-memory attempt; resume from a complete
checkpoint rather than continuing partially updated GAN state.
"""
import math


def vocoder_gan_step(generator, discriminators, generator_optimizer, discriminator_optimizer,
                     *, mel, f0, pcm, hop_size, partition, reconstruction_loss,
                     reconstruction_weight=45.0, feature_weight=2.0):
    import torch
    if partition != "train":
        raise ValueError("Vocoder updates require the training partition")
    if type(hop_size) is not int or not 1 <= hop_size <= 8192:
        raise ValueError("Invalid hop size")
    if not callable(reconstruction_loss) or any(not math.isfinite(w) or w < 0
                                               for w in (reconstruction_weight, feature_weight)):
        raise ValueError("Invalid reconstruction objective or weights")
    if not discriminators:
        raise ValueError("Discriminators are required")
    if (mel.ndim != 3 or mel.shape[0] != 1 or not 1 <= mel.shape[1] <= 512 or
            not 1 <= mel.shape[2] <= 4096 or tuple(f0.shape) != (1, mel.shape[2]) or
            tuple(pcm.shape) != (1, 1, mel.shape[2] * hop_size) or pcm.numel() > 1048576):
        raise ValueError("Require bounded batch-one BFT mel, BF pitch and B1S PCM")
    for value in (mel, f0, pcm):
        if value.dtype != torch.float32 or value.device.type != "cpu" or value.requires_grad or not torch.isfinite(value).all():
            raise ValueError("Training inputs must be finite owned CPU float32 tensors")
    if torch.any(f0 < 0) or torch.any(pcm.abs() > 1):
        raise ValueError("Invalid pitch or normalized PCM")
    gp = list(generator.parameters())
    dp = [p for model in discriminators for p in model.parameters()]
    def validate_optimizer(parameters, optimizer):
        actual = [p for group in optimizer.param_groups for p in group["params"]]
        if (not parameters or len({id(p) for p in parameters}) != len(parameters) or
                len(actual) != len(parameters) or {id(p) for p in actual} != {id(p) for p in parameters} or
                any(p.device.type != "cpu" or p.dtype != torch.float32 or not p.requires_grad
                    or not torch.isfinite(p).all() for p in parameters)):
            raise ValueError("Optimizer must exclusively own all finite trainable model parameters")
    validate_optimizer(gp, generator_optimizer)
    validate_optimizer(dp, discriminator_optimizer)
    if {id(p) for p in gp} & {id(p) for p in dp}:
        raise ValueError("Generator and discriminator parameters must not overlap")
    modules = [module for model in (generator, *discriminators) for module in model.modules()]
    modes = [(module, module.training) for module in modules]

    def outputs(model, audio):
        scores, features = model(audio)
        if not scores or len(scores) != len(features) or any(not layers for layers in features):
            raise ValueError("Invalid discriminator output structure")
        for tensor in [*scores, *(t for layers in features for t in layers)]:
            if not tensor.numel() or not torch.isfinite(tensor).all():
                raise ValueError("Nonfinite or empty discriminator output")
        return scores, features

    def checked_step(loss, parameters, optimizer):
        if loss.ndim != 0 or not torch.isfinite(loss):
            raise ValueError("Nonfinite or nonscalar GAN loss")
        loss.backward()
        norm = torch.nn.utils.clip_grad_norm_(parameters, 1.0, error_if_nonfinite=True)
        optimizer.step()
        if not all(torch.isfinite(p).all() for p in parameters):
            raise ValueError("Nonfinite GAN parameters after update; discard attempt")
        return float(norm)

    try:
        generator.train()
        for model in discriminators:
            model.train()
        generator_optimizer.zero_grad(set_to_none=True)
        discriminator_optimizer.zero_grad(set_to_none=True)
        predicted = generator(mel, f0)
        if predicted.shape != pcm.shape or not torch.isfinite(predicted).all():
            raise ValueError("Generator PCM geometry or values invalid")
        dloss = torch.zeros(())
        for model in discriminators:
            real, _ = outputs(model, pcm)
            fake, _ = outputs(model, predicted.detach())
            if len(real) != len(fake) or any(r.shape != f.shape for r, f in zip(real, fake)):
                raise ValueError("Discriminator score geometry mismatch")
            dloss = dloss + sum((1 - r).square().mean() + f.square().mean() for r, f in zip(real, fake))
        dnorm = checked_step(dloss, dp, discriminator_optimizer)
        if any(p.grad is not None for p in gp):
            raise AssertionError("Discriminator update leaked generator gradients")
        discriminator_optimizer.zero_grad(set_to_none=True)
        # Freeze discriminator parameters and spectral-normalization buffers for
        # the generator phase, while preserving gradients with respect to audio.
        for model in discriminators:
            model.eval()
        for parameter in dp:
            parameter.requires_grad_(False)
        adversarial, feature = torch.zeros(()), torch.zeros(())
        for model in discriminators:
            with torch.no_grad():
                _, real_features = outputs(model, pcm)
            fake_scores, fake_features = outputs(model, predicted)
            adversarial = adversarial + sum((1 - score).square().mean() for score in fake_scores)
            if len(real_features) != len(fake_features):
                raise ValueError("Discriminator feature structure mismatch")
            for real_layers, fake_layers in zip(real_features, fake_features):
                if len(real_layers) != len(fake_layers):
                    raise ValueError("Discriminator feature layer count mismatch")
                for real, fake in zip(real_layers, fake_layers):
                    if real.shape != fake.shape:
                        raise ValueError("Discriminator feature geometry mismatch")
                    feature = feature + (real - fake).abs().mean()
        reconstruction = reconstruction_loss(predicted, pcm)
        if reconstruction.ndim != 0 or not torch.isfinite(reconstruction):
            raise ValueError("Invalid reconstruction loss")
        gloss = adversarial + feature_weight * feature + reconstruction_weight * reconstruction
        gnorm = checked_step(gloss, gp, generator_optimizer)
        if any(p.grad is not None for p in dp):
            raise AssertionError("Generator update leaked discriminator gradients")
        return dict(discriminatorLoss=dloss.item(), generatorLoss=gloss.item(),
                    adversarialLoss=adversarial.item(), featureLoss=feature.item(),
                    reconstructionLoss=reconstruction.item(), generatorGradientNorm=gnorm,
                    discriminatorGradientNorm=dnorm, gradientOwnershipVerified=True,
                    trainingAdmitted=False, releaseEligible=False)
    finally:
        for parameter in dp:
            parameter.requires_grad_(True)
        for module, mode in modes:
            module.training = mode
