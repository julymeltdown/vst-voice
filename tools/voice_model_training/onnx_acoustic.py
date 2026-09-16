"""Export the trained base DDPM acoustic graph using upstream scripted control flow."""
import hashlib
import io

from .onnx_encoder import export_duration_encoder


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
    with torch.no_grad():
        from .diffusion_export_wrapper import DiffusionExportWrapper
        wrapper = DiffusionExportWrapper(decoder.diffusion)
        for steps in (1, 4, 8):
            torch.manual_seed(91)
            expected = decoder(condition, steps)
            torch.manual_seed(91)
            actual = wrapper(condition, steps)
            if not torch.equal(expected, actual):
                raise ValueError("Typed diffusion entry differs from upstream sampling")
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
