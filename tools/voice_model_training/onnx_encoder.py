"""Serialize and execute the actual duration encoder; not a complete acoustic graph."""
import hashlib
import io


def export_duration_encoder(deployment_model) -> bytes:
    """Export owned CPU weights with dynamic token/frame axes, fixed batch one.

    Run only in the isolated trusted upstream-model process. The returned graph
    is an intermediate condition encoder, not SEAM's acoustic/vocoder pair.
    """
    import torch
    import onnx
    from modules.commons.common_layers import SinusoidalPositionalEmbedding

    # Tracing freezes the upstream lazy-growth branch. Materialize SEAM's
    # bounded token capacity rather than shipping a graph limited to 1023 tokens.
    for module in deployment_model.fs2.modules():
        if isinstance(module, SinusoidalPositionalEmbedding):
            module.weights = module.get_embedding(4096 + module.padding_idx + 1,
                                                 module.embedding_dim, module.padding_idx).to(module._float_tensor)

    class Encoder(torch.nn.Module):
        def __init__(self, model):
            super().__init__()
            self.encoder = model.fs2

        def forward(self, tokens, durations, f0):
            return self.encoder(tokens, durations, f0, variances={})

    class ConditionedEncoder(torch.nn.Module):
        def __init__(self, model):
            super().__init__()
            self.encoder = model.fs2

        def forward(self, tokens, durations, f0, breathiness):
            return self.encoder(tokens, durations, f0, variances={"breathiness": breathiness})

    conditioned = deployment_model.fs2.use_breathiness_embed is True
    wrapper = (ConditionedEncoder(deployment_model) if conditioned else Encoder(deployment_model)).eval()
    tokens = torch.tensor([[1, min(2, deployment_model.fs2.txt_embed.num_embeddings - 1),
                            min(3, deployment_model.fs2.txt_embed.num_embeddings - 1)]], dtype=torch.long)
    durations = torch.tensor([[5, 6, 5]], dtype=torch.long)
    f0 = torch.full((1, 16), 220., dtype=torch.float32)
    example = (tokens, durations, f0, torch.linspace(0., 1., 16)[None]) if conditioned else (tokens, durations, f0)
    input_names = ["tokens", "durations", "f0"] + (["breathiness"] if conditioned else [])
    dynamic_axes = {"tokens": {1: "n_tokens"}, "durations": {1: "n_tokens"},
                    "f0": {1: "n_frames"}, "condition": {1: "n_frames"}}
    if conditioned:
        dynamic_axes["breathiness"] = {1: "n_frames"}
    buffer = io.BytesIO()
    with torch.no_grad():
        torch.onnx.export(wrapper, example, buffer,
            input_names=input_names, output_names=["condition"], dynamic_axes=dynamic_axes,
            opset_version=17, dynamo=False, external_data=False)
    payload = buffer.getvalue()
    if not 1 <= len(payload) <= 64 * 1024 * 1024:
        raise ValueError("Encoder export exceeds the 64 MiB intermediate graph budget")
    graph = onnx.load_model_from_string(payload)
    # Native intake deliberately rejects nonfinite stored tensors. Attention
    # masking can use the finite float32 floor: valid token IDs are positive,
    # and all-padding sequences are outside the training/render input contract.
    import numpy as np
    consumers = {}
    for node in graph.graph.node:
        for name in node.input:
            consumers.setdefault(name, []).append(node)
    for node in graph.graph.node:
        if node.op_type != "Constant" or len(node.output) != 1:
            continue
        for attribute in node.attribute:
            if not attribute.HasField("t") or attribute.t.data_type != onnx.TensorProto.FLOAT:
                continue
            values = onnx.numpy_helper.to_array(attribute.t)
            if np.isfinite(values).all():
                continue
            uses = consumers.get(node.output[0], [])
            if (values.size != 1 or not np.isneginf(values).all() or not uses
                    or any(use.op_type != "Where" or node.output[0] not in use.input[1:] for use in uses)):
                raise ValueError("Nonfinite encoder constant is not a recognized attention mask")
            replacement = np.full(values.shape, -np.finfo(np.float32).max, dtype=np.float32)
            attribute.t.CopyFrom(onnx.numpy_helper.from_array(replacement, name=attribute.t.name))
    onnx.checker.check_model(graph, full_check=True)
    if any(tensor.external_data for tensor in graph.graph.initializer):
        raise ValueError("Encoder export must own all initializer bytes")
    return graph.SerializeToString()


def check_encoder_runtime(deployment_model) -> dict:
    """Check varied real encoder outputs in ONNX Runtime, without fixture graphs."""
    import numpy as np
    import torch
    import onnxruntime as ort
    ort.disable_telemetry_events()
    payload = export_duration_encoder(deployment_model)
    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    session = ort.InferenceSession(payload, sess_options=options, providers=["CPUExecutionProvider"])
    cases = []
    conditioned = deployment_model.fs2.use_breathiness_embed is True
    maximum_control_effect = 0.0
    for ids, lengths in (([1, 2, 3], [5, 6, 5]), ([1, 2, 3], [1, 0, 2]),
                         ([1, 3], [9, 14]), ([3, 2, 1, 3, 1], [1, 2, 3, 4, 5]),
                         ([1] * 1025, [1] * 1025)):
        tokens, durations = np.array([ids], dtype=np.int64), np.array([lengths], dtype=np.int64)
        f0 = np.linspace(110, 440, sum(lengths), dtype=np.float32)[None]
        breathiness = np.linspace(0, 1, sum(lengths), dtype=np.float32)[None]
        variances = {"breathiness": torch.from_numpy(breathiness)} if conditioned else {}
        with torch.no_grad():
            expected = deployment_model.forward_fs2_aux(torch.from_numpy(tokens), torch.from_numpy(durations),
                                                       torch.from_numpy(f0), variances=variances).numpy()
        runtime_inputs = dict(tokens=tokens, durations=durations, f0=f0)
        if conditioned:
            runtime_inputs["breathiness"] = breathiness
        actual = session.run(["condition"], runtime_inputs)[0]
        if conditioned:
            neutral = session.run(["condition"], runtime_inputs | {"breathiness": np.zeros_like(breathiness)})[0]
            maximum_control_effect = max(maximum_control_effect, float(np.max(np.abs(actual - neutral))))
        passed = (actual.shape == expected.shape and np.isfinite(actual).all()
                  and np.allclose(actual, expected, atol=1e-5, rtol=1e-5))
        cases.append(dict(tokens=len(ids), frames=sum(lengths), maximumAbsoluteError=float(np.max(np.abs(actual - expected))),
                          passed=bool(passed)))
    effect_passed = not conditioned or maximum_control_effect > 1e-7
    return dict(passed=all(case["passed"] for case in cases) and effect_passed, cases=cases,
                graphBytes=len(payload), graphSha256=hashlib.sha256(payload).hexdigest(),
                runtimeVersion=ort.__version__, encoderOnly=True, completeAcousticGraph=False,
                conditioningControls=["breathiness"] if conditioned else [],
                maximumBreathinessConditionEffect=maximum_control_effect,
                breathinessConditionEffectPassed=effect_passed,
                graphRetained=False, releaseEligible=False)
