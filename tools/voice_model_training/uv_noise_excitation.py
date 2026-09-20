"""UV-gated aperiodic excitation for the mini-NSF vocoder family.

The exported mini_nsf generator is deterministic: fastsinegen emits a pure
sine (all-zeros at f0=0) and noise_sigma is pinned to zero, so no stochastic
excitation reaches the source path. This module adds a caller-owned noise
input summed into the harmonic source before source_conv. It introduces no
new parameters, so a checkpoint's state_dict is identical to the base
architecture and governed warm start still applies.

The gate is admitted non-rest unvoiced ownership from conditioning frames
((not voiced) and (not rest)); silence is never inferred from f0==0. The
control arm draws the same random values and multiplies them by zero, so a
paired experiment varies only the excitation content, not the draw sequence.

Production-inference decision (recorded before training): the exported graph
takes noise as an explicit input; the caller supplies a deterministic seeded
realization bound to the request, gated by the score's own phone ownership.
The graph remains a pure function of its inputs, so replay is exact.
"""

EXCITATION_IDS = ('zero-v1', 'uv-gated-v1')
# mini_nsf fastsinegen emits a full-amplitude sine; the unvoiced noise level
# follows the classic NSF ratio of one third of the harmonic amplitude.
EXCITATION_AMPLITUDE = {'zero-v1': 0.0, 'uv-gated-v1': 1.0 / 3.0}
# mini_nsf source rate: upsample_rates[:2] product (8*8=64 samples per frame).
SOURCE_SAMPLES_PER_FRAME = 64


def excitation_amplitude(excitation_id):
    if excitation_id not in EXCITATION_AMPLITUDE:
        raise ValueError('Unsupported excitation noise identity')
    return EXCITATION_AMPLITUDE[excitation_id]


def unvoiced_frame_mask(columns):
    """Boolean per-frame mask of admitted non-rest unvoiced ownership."""
    import numpy as np
    voiced, rest = columns.get('voiced'), columns.get('rest')
    if (not isinstance(voiced, list) or not isinstance(rest, list)
            or len(voiced) != len(rest) or not 1 <= len(voiced) <= 4096
            or any(type(v) is not bool for v in voiced + rest)):
        raise ValueError('Expected bounded per-frame voiced/rest ownership')
    return np.array([(not v) and (not r) for v, r in zip(voiced, rest)], dtype=bool)


def build_excitation_noise(unvoiced_frames, excitation_id):
    """Draw the source-rate noise tensor for one training segment.

    Always consumes the same randn draw regardless of arm; the amplitude
    factor is the only difference between control and treatment. The gate is
    upsampled by nearest-neighbor to the mini-NSF source rate.
    """
    import numpy as np
    import torch
    amplitude = excitation_amplitude(excitation_id)
    mask = np.asarray(unvoiced_frames, dtype=bool)
    if mask.ndim != 1 or not 1 <= mask.size <= 4096:
        raise ValueError('Expected a bounded per-frame unvoiced mask')
    gate = torch.from_numpy(
        np.repeat(mask.astype(np.float32), SOURCE_SAMPLES_PER_FRAME))
    draws = torch.randn(1, 1, mask.size * SOURCE_SAMPLES_PER_FRAME)
    return draws * gate.reshape(1, 1, -1) * amplitude


def uv_noise_forward(self, x, f0, noise):
    """Upstream mini-NSF forward with an additive source-path noise input.

    Mirrors singing-vocoders-source models/nsf_HiFigan/models.py Generator.forward
    exactly except for the noise addition; keep in sync with the pinned revision.
    Only the mini_nsf branch is supported by this variant.
    """
    import torch
    import torch.nn.functional as F
    if not self.mini_nsf:
        raise ValueError('UV-noise excitation requires the mini_nsf source path')
    if (not torch.is_tensor(noise) or noise.dtype != torch.float32
            or noise.shape != (x.shape[0], 1, f0.shape[1] * SOURCE_SAMPLES_PER_FRAME)
            or not torch.isfinite(noise).all()):
        raise ValueError('Expected finite float32 source-rate noise input')
    har_source = self.fastsinegen(f0) + noise
    x = self.conv_pre(x)
    if self.noise_sigma is not None and self.noise_sigma > 0:
        x += self.noise_sigma * torch.randn_like(x)
    for i in range(self.num_upsamples):
        x = F.leaky_relu(x, 0.1, True)
        x = self.ups[i](x)
        if i == 1:
            x = x + self.source_conv(har_source)
        xs = None
        for j in range(self.num_kernels):
            if xs is None:
                xs = self.resblocks[i * self.num_kernels + j](x)
            else:
                xs += self.resblocks[i * self.num_kernels + j](x)
        x = xs / self.num_kernels
    x = F.leaky_relu(x, inplace=True)
    x = self.conv_post(x)
    return torch.tanh(x)


def uv_noise_generator_class(base):
    """Subclass the checkout's Generator with the noise-input forward.

    No parameters are added, so state_dict keys/shapes/dtypes are identical
    and warm-start weight checks still govern compatibility.
    """
    return type('UvNoiseGenerator', (base,), {'forward': uv_noise_forward,
                '__doc__': 'mini-NSF generator with UV-gated noise excitation'})


class UvNoiseONNXAdapter:
    """Build the export-time (mel, f0, noise) adapter around a Generator class.

    Lives outside the trusted deployment checkout; wraps the checkout's
    Generator subclass so the graph gains a noise input while weights stay
    byte-identical to the two-input architecture.
    """
    @staticmethod
    def build(generator_cls, configuration):
        import torch

        class Adapter(torch.nn.Module):
            def __init__(self, attrs):
                super().__init__()
                self.generator = generator_cls(attrs)

            def forward(self, mel, f0, noise):
                mel = mel.transpose(1, 2)
                wav = self.generator(mel, f0, noise.unsqueeze(1))
                return wav.squeeze(1)

        return Adapter(configuration)
