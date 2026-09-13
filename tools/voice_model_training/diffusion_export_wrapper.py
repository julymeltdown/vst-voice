"""Typed non-shallow entry point for the pinned upstream diffusion module.

Preserves openvpi/DiffSinger 336cf01b's non-shallow forward algorithm while
avoiding unannotated optional x_start/depth arguments inferred as Tensor by JIT.
Uses the upstream trained backbone, sampling helpers, schedule and denormalizer.
"""
import torch


class DiffusionExportWrapper(torch.nn.Module):
    def __init__(self, diffusion):
        super().__init__()
        self.diffusion = diffusion

    def forward(self, condition: torch.Tensor, steps: int) -> torch.Tensor:
        condition = condition.transpose(1, 2)
        noise = torch.randn((1, self.diffusion.num_feats, self.diffusion.out_dims, condition.shape[2]),
                            device=condition.device)
        speedup = max(1, self.diffusion.timesteps // steps)
        speedup = int(self.diffusion.timestep_factors[torch.sum(self.diffusion.timestep_factors <= speedup) - 1])
        step_range = torch.arange(0, self.diffusion.k_step, speedup, dtype=torch.long,
                                 device=condition.device).flip(0)[:, None]
        x = noise
        if speedup > 1:
            for t in step_range:
                x = self.diffusion.p_sample_ddim(x, t, speedup, condition)
        else:
            for t in step_range:
                x = self.diffusion.p_sample(x, t, condition)
        return self.diffusion.denorm_spec(x.squeeze(1).permute(0, 2, 1))
