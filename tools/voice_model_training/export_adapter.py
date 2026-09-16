"""Bridge an owned base DDPM model to the pinned upstream deployment architecture.

Use only inside an isolated process that has loaded the trusted pinned upstream
checkout. Upstream forwards share global hparams; this is not an in-app loader.
Preparing this model does not serialize/admit an ONNX graph or license weights.
"""


def prepare_acoustic_export(trained_model, *, configuration: dict, acoustic_profile: dict):
    if (not isinstance(acoustic_profile, dict)
            or acoustic_profile.get("profileId") != "seam-full-hop-slaney-v1"
            or acoustic_profile.get("amplitudeScale") != "ln-amplitude"
            or acoustic_profile.get("layout") != "TF"
            or acoustic_profile.get("dtype") != "float32-le"
            or type(acoustic_profile.get("bins")) is not int
            or not 1 <= acoustic_profile["bins"] <= 512):
        raise ValueError("Deployment bridge requires the captured natural-log acoustic profile")
    if (not isinstance(configuration, dict) or configuration.get("diffusion_type") != "ddpm"
            or configuration.get("use_shallow_diffusion") is not False):
        raise ValueError("Deployment bridge supports base non-shallow DDPM only")
    flags = ("use_lang_id", "use_spk_id", "use_energy_embed",
             "use_voicing_embed", "use_tension_embed", "use_key_shift_embed", "use_speed_embed")
    if any(configuration.get(flag, False) is not False for flag in flags):
        raise ValueError("Deployment bridge does not support additional conditioning embeddings")
    if type(configuration.get("use_breathiness_embed", False)) is not bool:
        raise ValueError("Deployment bridge requires an explicit boolean breathiness embedding declaration")
    if configuration.get("mel_base", "e") != "e":
        raise ValueError("Training configuration conflicts with natural-log targets")
    from utils.hparams import hparams
    from deployment.modules.toplevel import DiffSingerAcousticONNX
    hparams.clear()
    hparams.update(configuration)
    hparams.update({flag: False for flag in flags})
    hparams["use_breathiness_embed"] = configuration.get("use_breathiness_embed", False)
    hparams["use_variance_scaling"] = False
    hparams["mel_base"] = "e"  # Upstream defaults to log10: never silently multiply our targets.
    model = DiffSingerAcousticONNX(vocab_size=trained_model.fs2.txt_embed.num_embeddings,
                                  out_dims=acoustic_profile["bins"], cross_lingual_token_idx=[])
    model.load_state_dict(trained_model.state_dict(), strict=True)
    model.eval()
    return model


def check_deployment_bridge(trained_model, *, configuration: dict, acoustic_profile: dict) -> dict:
    """Actual deployment forwards over varied engineering sequences, not ONNX execution."""
    import torch
    deployment = prepare_acoustic_export(trained_model, configuration=configuration, acoustic_profile=acoustic_profile)
    trained_model.eval()
    cases = []
    maximum_control_effect = 0.0
    maximum_mel_effect = 0.0
    use_breathiness = configuration.get("use_breathiness_embed", False)
    with torch.no_grad():
        for lengths in ([5, 6, 5], [1, 0, 2], [9, 3, 11]):
            tokens = torch.tensor([[1, 2, 3]], dtype=torch.long)
            durations = torch.tensor([lengths], dtype=torch.long)
            alignment = torch.repeat_interleave(torch.arange(1, 4), torch.tensor(lengths))[None]
            f0 = torch.full((1, sum(lengths)), 220., dtype=torch.float32)
            breathiness = torch.linspace(0., 1., sum(lengths), dtype=torch.float32)[None]
            variances = {"breathiness": breathiness} if use_breathiness else {}
            original_condition = trained_model.fs2(tokens, alignment, f0, **variances)
            exported_condition = deployment.forward_fs2_aux(tokens, durations, f0, variances=variances)
            error = float((original_condition - exported_condition).abs().max())
            if use_breathiness:
                neutral = deployment.forward_fs2_aux(tokens, durations, f0,
                    variances={"breathiness": torch.zeros_like(breathiness)})
                maximum_control_effect = max(maximum_control_effect,
                                             float((exported_condition - neutral).abs().max()))
            cases.append(dict(durations=lengths, frames=sum(lengths), maximumConditionError=error,
                              passed=torch.allclose(original_condition, exported_condition, atol=1e-5, rtol=1e-5)))
        # Check the deployment's scale adapter directly, including negative log values.
        mel = torch.tensor([[[-11., -6., -1.]]])
        scale_exact = torch.equal(deployment.ensure_mel_base(mel), mel)
        torch.manual_seed(91)
        output = deployment.forward_diffusion(exported_condition, steps=4)
        if use_breathiness:
            torch.manual_seed(91)
            neutral_output = deployment.forward_diffusion(neutral, steps=4)
            maximum_mel_effect = float((output - neutral_output).abs().max())
    finite = tuple(output.shape) == (1, 23, acoustic_profile["bins"]) and bool(torch.isfinite(output).all())
    control_effect_passed = (not use_breathiness
                             or (maximum_control_effect > 1e-7 and maximum_mel_effect > 1e-7))
    return dict(passed=all(case["passed"] for case in cases) and scale_exact and finite and control_effect_passed,
                cases=cases, naturalLogScalePreserved=scale_exact, deploymentMelShape=list(output.shape),
                deploymentMelFinite=finite, strictStateLoad=True, onnxExported=False,
                conditioningControls=["breathiness"] if use_breathiness else [],
                maximumBreathinessConditionEffect=maximum_control_effect,
                maximumBreathinessMelEffect=maximum_mel_effect,
                breathinessConditionEffectPassed=control_effect_passed,
                singerQualified=False, releaseEligible=False)
