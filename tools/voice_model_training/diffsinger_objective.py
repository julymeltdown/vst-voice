"""DDPM adapter for the inspected DiffSinger training output; no model loading."""


class DiffSingerDDPMObjective:
    """Whole-phrase, non-shallow, base-conditioning pilot adapter.

    Source contract: openvpi/DiffSinger 336cf01b57f2ad44c6b37a79cf33993043291759,
    training/acoustic_task.py and modules/losses/diff_loss.py. Source inspection
    and interface tests do not prove a trained model or licensed weight bundle.
    """
    def __init__(self, loss_type: str):
        if loss_type not in ("l1", "l2"):
            raise ValueError("DDPM loss must be l1 or l2")
        self.loss_type = loss_type
        self.objective_id = f"diffsinger-ddpm-{loss_type}-whole-phrase-v2"

    def __call__(self, model, inputs, target):
        import torch
        if (getattr(model, "diffusion_type", None) != "ddpm"
                or getattr(model, "use_shallow_diffusion", None) is not False):
            raise ValueError("Adapter requires a non-shallow DDPM model")
        encoder = getattr(model, "fs2", None)
        flags = ("use_lang_id", "use_energy_embed", "use_voicing_embed",
                 "use_tension_embed", "use_key_shift_embed", "use_speed_embed", "use_spk_id")
        if any(getattr(encoder, flag, None) is not False for flag in flags):
            raise ValueError("Model requires conditioning not provided by the base DDPM adapter")
        if ("tokens" not in inputs or "mel2ph" not in inputs
                or type(inputs.get("frameOffset")) is not int or inputs["frameOffset"] != 0
                or type(inputs.get("phraseAnalysisFrames")) is not int
                or inputs["phraseAnalysisFrames"] != target.shape[1]):
            raise ValueError("DDPM duration conditioning requires the entire phrase")
        use_breathiness = getattr(encoder, "use_breathiness_embed", None)
        if use_breathiness not in (False, True):
            raise ValueError("Model breathiness embedding declaration is invalid")
        breathiness = inputs.get("breathiness")
        if (not isinstance(breathiness, torch.Tensor) or breathiness.shape != inputs["f0Hz"].shape
                or breathiness.dtype != torch.float32 or breathiness.device != target.device
                or not torch.isfinite(breathiness).all() or torch.any(breathiness < 0)
                or torch.any(breathiness > 1)):
            raise ValueError("DDPM breathiness conditioning must be finite normalized [0, 1]")
        if not use_breathiness and torch.any(breathiness != 0):
            raise ValueError("Base DDPM model cannot silently discard breathiness supervision")
        output = model(inputs["tokens"], mel2ph=inputs["mel2ph"], f0=inputs["f0Hz"],
                       gt_mel=target, infer=False,
                       **({"breathiness": breathiness} if use_breathiness else {}))
        pair = getattr(output, "diff_out", None)
        if getattr(output, "aux_out", None) is not None or not isinstance(pair, tuple) or len(pair) != 2:
            raise ValueError("Unexpected DDPM training outputs")
        predicted, noise = pair
        shape = (1, 1, target.shape[2], target.shape[1])
        if any(not isinstance(value, torch.Tensor) or tuple(value.shape) != shape
               or value.dtype != torch.float32 or value.device != target.device
               or not torch.isfinite(value).all() for value in pair) or noise.requires_grad:
            raise ValueError("Invalid DDPM noise prediction or target")
        error = predicted - noise
        loss = error.abs() if self.loss_type == "l1" else error.square()
        return loss[:, 0].transpose(1, 2)
