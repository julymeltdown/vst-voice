"""Experimental feature noise only; not a training or deployment profile."""
import math


def fixed_feature_noise(features, f0, *, sigma, seed):
    """Return a reproducible perturbation of unvoiced conv-pre frames only.

    A local CPU generator leaves the caller's RNG untouched. Noise is defined
    relative to the captured feature RMS; it is not added to output waveform.
    """
    import torch
    if (features.device.type != 'cpu' or f0.device.type != 'cpu'
            or features.dtype != torch.float32 or f0.dtype != torch.float32
            or features.ndim != 3 or f0.ndim != 2 or features.shape[0] != 1
            or f0.shape != (1, features.shape[2]) or features.numel() > 8_000_000
            or features.numel() == 0 or not torch.isfinite(features).all()
            or not torch.isfinite(f0).all() or (f0 < 0).any()
            or type(seed) is not int or not 0 <= seed < 2**63
            or type(sigma) not in (int, float) or not math.isfinite(sigma) or not 0 <= sigma <= .1):
        raise ValueError('Expected bounded CPU float32 features, F0 and diagnostic policy')
    if sigma == 0:
        return features.clone()
    generator = torch.Generator(device='cpu').manual_seed(seed)
    noise = torch.randn(features.shape, dtype=features.dtype, generator=generator)
    scale = features.detach().square().mean().sqrt()
    perturbed = features + noise * (sigma * scale)
    return torch.where((f0 == 0).unsqueeze(1), perturbed, features)
