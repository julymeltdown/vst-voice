"""Export the trained base DDPM acoustic graph using upstream scripted control flow."""
import hashlib
import io

from .onnx_encoder import export_duration_encoder


# The diffusion sampler draws its initial noise inside the graph, and ONNX Runtime
# seeds that generator per session rather than per call. Without a seed the same
# request produced a different waveform in every worker process, which contradicts
# the product's own neural reproducibility contract. Pinning the seed is the fix
# that keeps the native graph interface intact: the worker admits exactly
# ``tokens``, ``durations``, ``f0`` and ``steps``, so the noise cannot simply be
# passed in as another input without changing that admission contract.
# ONNX stores a random op's seed in a float32 field, so the value must be exactly
# representable: any integer below 2**24 is. A larger constant would be silently
# rounded, and the rounded value is what a later check would then compare against.
SAMPLING_SEED = 2026091
MAXIMUM_SAMPLING_SEED = 1 << 24
RANDOM_OPS = ("RandomNormal", "RandomNormalLike", "RandomUniform", "RandomUniformLike")

# The exported sampler must agree with the pinned upstream algorithm, and the only
# intentional difference is the bounded clean-latent estimate. Both halves are
# checked against the same upstream call, so neither can be satisfied by weakening
# the other: the transcription is compared with the clamp disabled and must match
# bit-for-bit, and the shipped configuration is compared with the clamp enabled
# and must remain finite within the schedule-derived noisy-latent envelope.
UNSTAGED_STEPS = (1, 4, 8)


def check_sampler_transcription(deployment_model) -> dict:
    """Check upstream transcription and the bounded-clean sampling envelope.

    Two independent claims are checked against the same upstream call, so neither
    can be satisfied by weakening the other:

    * with the clamp disabled the transcription must equal upstream exactly, which
      is what makes this a transcription rather than a rewrite;
    * with the clamp enabled the output must stay finite within the envelope
      implied by the schedule, initial noise and bounded clean estimates. Standard
      deviation is diagnostic, not a monotonicity invariant across step counts.

    The condition comes from the real duration encoder rather than zeros, so the
    measurement is about the sampler in its shipped configuration.
    """
    import torch
    from .diffusion_export_wrapper import DiffusionExportWrapper, sampling_absolute_bound

    decoder = deployment_model.view_as_diffusion().eval()
    hidden = deployment_model.fs2.txt_embed.embedding_dim
    unclamped = DiffusionExportWrapper(decoder.diffusion, clamp_latent=False).eval()
    shipped = DiffusionExportWrapper(decoder.diffusion, clamp_latent=True).eval()
    with torch.no_grad():
        lengths = [5, 6, 5]
        tokens = torch.tensor([[1, 2, 3]], dtype=torch.long)
        durations = torch.tensor([lengths], dtype=torch.long)
        f0 = torch.full((1, sum(lengths)), 220., dtype=torch.float32)
        # The condition comes from the deployment model, because view_as_diffusion
        # drops the duration encoder that produces it.
        condition = deployment_model.forward_fs2_aux(tokens, durations, f0, variances={})
        if tuple(condition.shape) != (1, sum(lengths), hidden):
            raise ValueError("Sampler transcription check built an unexpected condition shape")
        transcription = []
        for steps in UNSTAGED_STEPS:
            torch.manual_seed(91)
            expected = decoder(condition, steps)
            torch.manual_seed(91)
            actual = unclamped(condition, steps)
            error = float((expected - actual).abs().max())
            transcription.append(dict(steps=steps, maximumAbsoluteError=error, passed=error == 0.0))
        spread = []
        for steps in (2, 4, 8, 16):
            torch.manual_seed(91)
            latent = shipped.sample_latent(condition, steps)
            finite = bool(torch.isfinite(latent).all())
            bound = sampling_absolute_bound(decoder.diffusion, sum(lengths), steps, 91)
            peak = float(latent.abs().max()) if finite else None
            spread.append(dict(steps=steps, finite=finite,
                               standardDeviation=float(latent.std()) if finite else None,
                               maximumAbsoluteValue=peak, analyticalAbsoluteBound=bound,
                               boundTolerance=1e-4,
                               passed=finite and peak <= bound + 1e-4))
        torch.manual_seed(91)
        clamp_active = not torch.equal(shipped.sample_latent(condition, 16),
                                       unclamped.sample_latent(condition, 16))
    deviations = [case["standardDeviation"] for case in spread if case["finite"]]
    # Dispersion need not decrease with step count: bounded, different samples
    # can have different variances. Enforce the schedule-derived envelope instead.
    monotone = len(deviations) == len(spread) and deviations[-1] <= deviations[0]
    return dict(transcriptionMatchesUpstream=all(case["passed"] for case in transcription),
                shippedIsFinite=all(case["finite"] for case in spread),
                boundedSamplingPassed=all(case["passed"] for case in spread),
                spreadDoesNotIncrease=bool(monotone), diagnosticRevision=2,
                clampIsActive=bool(clamp_active),
                transcription=transcription, spread=spread, releaseEligible=False)


def pin_sampling_seed(model, seed: int = SAMPLING_SEED) -> int:
    """Give every unseeded random sampling node the same explicit seed.

    ONNX Runtime reseeds a fresh session from a wall-clock-independent constant and
    then advances it per call, so a seeded node repeats across processes and across
    a single-call process, which is the contract the worker actually ships. A node
    that already carries a seed is left alone, and an existing seed that disagrees
    is refused rather than silently rewritten: the export must describe one
    sampling behaviour, not whichever seed happened to survive process order.
    """
    from onnx import helper
    if type(seed) is not int or not 0 <= seed < MAXIMUM_SAMPLING_SEED:
        raise ValueError("Sampling seed must be an exactly representable nonnegative integer")
    pinned = 0
    for node in model.graph.node:
        if node.op_type not in RANDOM_OPS:
            continue
        existing = [attribute for attribute in node.attribute if attribute.name == "seed"]
        if existing:
            if existing[0].f != float(seed):
                raise ValueError("Acoustic graph already carries a different sampling seed")
            continue
        node.attribute.extend([helper.make_attribute("seed", float(seed))])
        pinned += 1
    return pinned


def check_denoiser_runtime(deployment_model) -> dict:
    """Compare the actual trained denoiser with identical owned inputs across runtimes.

    This isolates numerical correctness from different runtime random generators;
    it does not claim end-to-end stochastic sampling parity.
    """
    import numpy as np
    import onnx
    import onnxruntime as ort
    ort.disable_telemetry_events()
    import torch
    backbone = deployment_model.diffusion.backbone.eval()
    bins = deployment_model.diffusion.out_dims
    hidden = deployment_model.fs2.txt_embed.embedding_dim
    buffer = io.BytesIO()
    with torch.no_grad():
        torch.onnx.export(backbone,
            (torch.zeros((1, 1, bins, 16)), torch.zeros((1,), dtype=torch.float32),
             torch.zeros((1, hidden, 16))), buffer,
            input_names=["noise", "timestep", "condition"], output_names=["predicted_noise"],
            dynamic_axes={"noise": {3: "n_frames"}, "condition": {2: "n_frames"},
                          "predicted_noise": {3: "n_frames"}},
            opset_version=17, dynamo=False, external_data=False)
    payload = buffer.getvalue()
    onnx.checker.check_model(onnx.load_model_from_string(payload), full_check=True)
    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    session = ort.InferenceSession(payload, sess_options=options, providers=["CPUExecutionProvider"])
    generator = np.random.default_rng(914)
    cases = []
    for frames in (3, 16, 23, 257):
        for timestep in (0, deployment_model.diffusion.timesteps - 1):
            inputs = dict(noise=generator.normal(size=(1, 1, bins, frames)).astype(np.float32),
                          timestep=np.array([timestep], dtype=np.float32),
                          condition=generator.normal(size=(1, hidden, frames)).astype(np.float32))
            with torch.no_grad():
                expected = backbone(torch.from_numpy(inputs["noise"]), torch.from_numpy(inputs["timestep"]),
                                    torch.from_numpy(inputs["condition"])).numpy()
            actual = session.run(["predicted_noise"], inputs)[0]
            passed = (actual.shape == expected.shape and np.isfinite(actual).all()
                      and np.allclose(actual, expected, atol=1e-5, rtol=1e-5))
            cases.append(dict(frames=frames, timestep=timestep,
                              maximumAbsoluteError=float(np.max(np.abs(actual - expected))), passed=bool(passed)))
    return dict(passed=all(case["passed"] for case in cases), cases=cases,
                graphBytes=len(payload), graphSha256=hashlib.sha256(payload).hexdigest(),
                deterministicDenoiserOnly=True, stochasticSamplingParity=False,
                graphRetained=False, releaseEligible=False)


def export_diffusion(deployment_model) -> bytes:
    import onnx
    import torch
    decoder = deployment_model.view_as_diffusion().eval()
    bins = decoder.diffusion.out_dims
    hidden = deployment_model.fs2.txt_embed.embedding_dim
    condition = torch.zeros((1, 16, hidden), dtype=torch.float32)
    fidelity = check_sampler_transcription(deployment_model)
    if not fidelity["transcriptionMatchesUpstream"]:
        raise ValueError("Typed diffusion entry no longer transcribes upstream sampling")
    if not fidelity["boundedSamplingPassed"]:
        raise ValueError("Shipped sampler exceeds its schedule-derived latent bound")
    if not fidelity["clampIsActive"]:
        raise ValueError("Bounded clean-latent estimate has no effect on the shipped sampler")
    with torch.no_grad():
        from .diffusion_export_wrapper import DiffusionExportWrapper
        wrapper = DiffusionExportWrapper(decoder.diffusion)
        if not fidelity["shippedIsFinite"]:
            raise ValueError("Shipped sampler produced a non-finite latent")
        decoder.diffusion.set_backbone(torch.jit.trace(decoder.diffusion.backbone,
            (torch.zeros((1, 1, bins, 16)), torch.zeros((1,), dtype=torch.float32), condition.transpose(1, 2))))
        scripted = torch.jit.script(DiffusionExportWrapper(decoder.diffusion))
        buffer = io.BytesIO()
        torch.onnx.export(scripted, (condition, 4), buffer,
            input_names=["condition", "steps"], output_names=["mel"],
            dynamic_axes={"condition": {1: "n_frames"}, "mel": {1: "n_frames"}},
            opset_version=17, dynamo=False, external_data=False)
    payload = buffer.getvalue()
    if not 1 <= len(payload) <= 64 * 1024 * 1024:
        raise ValueError("Diffusion intermediate exceeds 64 MiB")
    onnx.checker.check_model(onnx.load_model_from_string(payload), full_check=True)
    return payload


def export_acoustic(deployment_model) -> bytes:
    import onnx
    from onnx import compose
    encoder = onnx.load_model_from_string(export_duration_encoder(deployment_model))
    diffusion = onnx.load_model_from_string(export_diffusion(deployment_model))
    # Preserve public tensor names; disambiguate intermediate weights/edges.
    for model, prefix in ((encoder, "encoder/"), (diffusion, "diffusion/")):
        # Seed outer initializer names before ONNX's recursive prefix traversal:
        # If/Loop bodies capture these weights from their enclosing graph.
        mapping = {initializer.name: prefix + initializer.name for initializer in model.graph.initializer}
        graph = compose.add_prefix_graph(model.graph, prefix, rename_inputs=False, rename_outputs=False,
                                         name_map=mapping)
        model.graph.CopyFrom(graph)
    merged = compose.merge_models(encoder, diffusion, io_map=[("condition", "condition")])
    # Pin sampling before the interface checks so a graph that cannot be made
    # reproducible is refused here rather than shipped and discovered by a listener.
    if pin_sampling_seed(merged) == 0:
        raise ValueError("Acoustic graph has no random sampling node to make reproducible")
    revisions = {(entry.domain, entry.version) for entry in merged.opset_import}
    if revisions != {("", 17)}:
        raise ValueError("Merged acoustic graph must use only standard opset 17")
    del merged.opset_import[:]
    merged.opset_import.add(domain="", version=17)
    # These are constructor-bound dimensions, not inferred from sample sequence length.
    shape = merged.graph.output[0].type.tensor_type.shape.dim
    shape[0].dim_value = 1
    shape[2].dim_value = deployment_model.diffusion.out_dims
    onnx.checker.check_model(merged, full_check=True)
    conditioned = deployment_model.fs2.use_breathiness_embed is True
    expected_inputs = {"tokens", "durations", "f0", "steps"} | ({"breathiness"} if conditioned else set())
    if {item.name for item in merged.graph.input} != expected_inputs:
        raise ValueError("Merged acoustic graph has unexpected public inputs")
    if [item.name for item in merged.graph.output] != ["mel"]:
        raise ValueError("Merged acoustic graph has unexpected public outputs")
    if conditioned:
        for key, value in (
                ("seam.conditioning.revision", "2"),
                ("seam.conditioning.breathiness.type", "float32"),
                ("seam.conditioning.breathiness.unit", "normalized-periodic-aperiodic-balance"),
                ("seam.conditioning.breathiness.minimum", "0"),
                ("seam.conditioning.breathiness.maximum", "1"),
                ("seam.conditioning.breathiness.default", "0"),
                ("seam.conditioning.breathiness.supported", "true")):
            entry = merged.metadata_props.add()
            entry.key, entry.value = key, value
    payload = merged.SerializeToString()
    if len(payload) > 128 * 1024 * 1024:
        raise ValueError("Merged acoustic graph exceeds 128 MiB")
    return payload


def check_acoustic_runtime(deployment_model) -> dict:
    """Execute genuine merged weights; stochastic outputs are not numerical parity."""
    import numpy as np
    import onnxruntime as ort
    ort.disable_telemetry_events()
    from tools.neural_runtime.inspect_graph import inspect_bytes
    payload = export_acoustic(deployment_model)
    inspection = inspect_bytes(payload)
    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    session = ort.InferenceSession(payload, sess_options=options, providers=["CPUExecutionProvider"])
    cases = []
    conditioned = deployment_model.fs2.use_breathiness_embed is True
    for lengths, steps in (([5, 6, 5], 1), ([1, 0, 2], 4), ([9, 14], 8)):
        inputs = dict(tokens=np.array([list(range(1, len(lengths) + 1))], dtype=np.int64),
            durations=np.array([lengths], dtype=np.int64), f0=np.full((1, sum(lengths)), 220., dtype=np.float32),
            steps=np.array(steps, dtype=np.int64))
        if conditioned:
            inputs["breathiness"] = np.linspace(0, 1, sum(lengths), dtype=np.float32)[None]
        result = session.run(["mel"], inputs)[0]
        passed = (result.shape == (1, sum(lengths), deployment_model.diffusion.out_dims)
                  and np.isfinite(result).all())
        cases.append(dict(frames=sum(lengths), steps=steps, shape=list(result.shape), passed=bool(passed)))
    return dict(passed=all(case["passed"] for case in cases), cases=cases,
                graphBytes=len(payload), graphSha256=hashlib.sha256(payload).hexdigest(),
                runtimeVersion=ort.__version__, stochasticNumericalParityVerified=False,
                conditioningControls=["breathiness"] if conditioned else [], conditioningRevision=2,
                offlineGraphInspection=inspection["status"],
                completeAcousticGraph=True, vocoderIntegrated=False, graphRetained=False,
                singerQualified=False, releaseEligible=False)
