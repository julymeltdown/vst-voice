"""Experimental opt-in epoch objectives; enabled per training configuration."""

OBJECTIVE_ID = 'nsf-lsgan-logmel-uvperiodic-48k80-v1'
MULTILAG_OBJECTIVE_ID = 'nsf-lsgan-logmel-uvmultilag-48k80-v1'
# Fixed modest lag set inside the existing 1024-sample windows; reviewed
# experiment design, not a sweep.
MULTILAGS = (64, 128, 192, 256, 384, 512)


def phone_mask(entry, *, sample_offset, sample_count, valid_samples):
    """Build a bounded Japanese pilot mask from already admitted exact intervals."""
    import torch
    if (entry['score']['language'] != 'ja'
            or any(type(v) is not int for v in (sample_offset, sample_count, valid_samples))
            or sample_offset < 0 or not 1 <= valid_samples <= sample_count <= 1048576
            or sample_offset + valid_samples > entry['label']['frameCount']):
        raise ValueError('Periodicity objective requires bounded Japanese phone ownership')
    mask = torch.zeros(1, 1, sample_count, dtype=torch.bool)
    end = 0
    for phone in entry['label']['phonemes']:
        start, stop = phone['startFrame'], phone['endFrame']
        if type(start) is not int or type(stop) is not int or start != end or not start < stop <= entry['label']['frameCount']:
            raise ValueError('Phone intervals must cover the source contiguously')
        end = stop
        # Explicit renderer inventory; pau and voiced phones are never inferred
        # to be unvoiced from pitch-extractor uncertainty or a zero F0 value.
        if phone['symbol'] in ('h', 'f', 'k', 's', 'sh', 't', 'ch', 'ts'):
            left = max(start, sample_offset) - sample_offset
            right = min(stop, sample_offset + valid_samples) - sample_offset
            if left < right:
                mask[:, :, left:right] = True
    if end != entry['label']['frameCount']:
        raise ValueError('Incomplete phone ownership')
    return mask


def periodicity_loss(predicted, target, unvoiced_mask, lags=(256,)):
    """Compare lag correlation and level in full unvoiced 1024-sample windows.

    Inputs are batch-one mono PCM and an explicit per-sample boolean mask. The
    caller must derive that mask from admitted phone ownership, not assume zero
    F0 distinguishes consonants from silence. Every selected window uses actual
    target statistics; voiced boundaries and silent targets are not optimized.
    Existing waveform/spectral/adversarial losses must remain in the objective.
    """
    import torch
    if (predicted.ndim != 3 or predicted.shape[:2] != (1, 1)
            or not 1024 <= predicted.shape[2] <= 1048576
            or predicted.shape != target.shape or predicted.shape != unvoiced_mask.shape
            or predicted.dtype != torch.float32 or target.dtype != torch.float32
            or unvoiced_mask.dtype != torch.bool
            or predicted.device != target.device or predicted.device != unvoiced_mask.device
            or not torch.isfinite(predicted).all() or not torch.isfinite(target).all()
            or predicted.detach().abs().max() > 1 or target.detach().abs().max() > 1):
        raise ValueError('Expected bounded normalized mono float32 PCM and boolean sample mask')
    if (not isinstance(lags, tuple) or not 1 <= len(lags) <= 16
            or any(type(lag) is not int or not 16 <= lag <= 512 for lag in lags)
            or len(set(lags)) != len(lags)):
        raise ValueError('Expected a bounded set of distinct window lags')
    p = predicted.unfold(2, 1024, 256)[0, 0]
    t = target.detach().unfold(2, 1024, 256)[0, 0]
    mask = unvoiced_mask.unfold(2, 1024, 256)[0, 0].all(dim=-1)
    p, t = p - p.mean(dim=-1, keepdim=True), t - t.mean(dim=-1, keepdim=True)
    target_energy = t.square().mean(dim=-1)
    selected = mask & (target_energy > 1e-8)
    count = int(selected.sum().item())
    if not count:
        return predicted.sum() * 0, dict(selectedWindows=0, candidateWindows=int(mask.sum().item()))
    p, t = p[selected], t[selected]
    def correlation(x, lag):
        left, right = x[:, :-lag], x[:, lag:]
        denominator = (left.square().mean(dim=-1) * right.square().mean(dim=-1)).clamp_min(1e-16).sqrt()
        return (left * right).mean(dim=-1) / denominator
    # Mean across lags and windows: averaging rather than summing keeps the
    # term's scale independent of the lag-set size, matching actual target
    # correlations rather than forcing all correlations to zero.
    periodic = torch.stack([(correlation(p, lag) - correlation(t, lag)).square().mean()
                            for lag in lags]).mean()
    # Prevent a zero-output shortcut in this auxiliary loss; target scale is
    # detached and bounded by selected-window energy admission above.
    reference_rms = target_energy[selected].sqrt()
    level = ((p.square().mean(dim=-1) + 1e-12).sqrt() / reference_rms - 1).square().mean()
    return periodic + level, dict(selectedWindows=count, candidateWindows=int(mask.sum().item()),
                                  lags=list(lags),
                                  correlationLoss=float(periodic.detach()), levelLoss=float(level.detach()))
