"""Inspect bytes against the proposed SEAM acoustic/vocoder export profile.

This is not a claim that arbitrary DiffSinger exports use this profile. Export
adapters must target it explicitly; no tensor renaming or reshaping is guessed.
"""
import hashlib
import json

import onnx
from onnx import TensorProto

from inspect_graph import MAX_ELEMENTS, inspect_bytes


def inspect_pair(acoustic_bytes: bytes, vocoder_bytes: bytes, *, bins: int,
                 layout: str, hop_size: int, maximum_sample_frames: int,
                 steps_layout: str = "scalar", vocoder_output: str = "audio") -> dict:
    if vocoder_output not in ("audio", "waveform"):
        raise ValueError("Unsupported vocoder output name")
    if steps_layout not in ("scalar", "vector1"):
        raise ValueError("Unsupported steps layout")
    for name, value, maximum in (("bins", bins, 512), ("hop_size", hop_size, 8192),
                                 ("maximum_sample_frames", maximum_sample_frames, 4194304)):
        if type(value) is not int or not 1 <= value <= maximum:
            raise ValueError(f"Invalid {name}")
    if layout not in ("BTF", "BFT"):
        raise ValueError("Unsupported mel layout")
    frames = (maximum_sample_frames + hop_size - 1) // hop_size
    if frames * bins > MAX_ELEMENTS:
        raise ValueError("Mel tensor budget exceeded")
    acoustic, vocoder = inspect_bytes(acoustic_bytes), inspect_bytes(vocoder_bytes)
    acoustic_model = onnx.load_model_from_string(acoustic_bytes)

    def tensors(report, key, expected):
        entries = report[key]
        mapped = {entry["name"]: entry for entry in entries}
        if len(mapped) != len(entries) or set(mapped) != set(expected):
            raise ValueError(f"Unexpected {key}: expected {expected}")
        return mapped

    def tensor(entry, dtype, rank):
        if entry["dtype"] != dtype or len(entry["shape"]) != rank:
            raise ValueError("Tensor dtype or rank mismatch")
        return entry["shape"]

    def sequence(entry, dtype):
        shape = tensor(entry, dtype, 2)
        if shape[0] != 1 or not isinstance(shape[1], str):
            raise ValueError("Expected batch-one dynamically sized sequence")
        return shape[1]

    def mel(entry):
        shape = tensor(entry, TensorProto.FLOAT, 3)
        time_axis, bin_axis = (1, 2) if layout == "BTF" else (2, 1)
        if shape[0] != 1 or shape[bin_axis] != bins or not isinstance(shape[time_axis], str):
            raise ValueError("Mel layout, bins or batch mismatch")
        return shape[time_axis]

    conditioned = any(entry["name"] == "breathiness" for entry in acoustic["inputs"])
    expected_acoustic_inputs = ("tokens", "durations", "f0", "steps") + (("breathiness",) if conditioned else ())
    inputs = tensors(acoustic, "inputs", expected_acoustic_inputs)
    outputs = tensors(acoustic, "outputs", ("mel",))
    token_axis = sequence(inputs["tokens"], TensorProto.INT64)
    if sequence(inputs["durations"], TensorProto.INT64) != token_axis:
        raise ValueError("Token and duration axes disagree")
    frame_axis = sequence(inputs["f0"], TensorProto.FLOAT)
    if frame_axis == token_axis or mel(outputs["mel"]) != frame_axis:
        raise ValueError("Acoustic time axes disagree")
    if conditioned:
        if sequence(inputs["breathiness"], TensorProto.FLOAT) != frame_axis:
            raise ValueError("Breathiness and acoustic time axes disagree")
        required = {
            "seam.conditioning.revision": "2",
            "seam.conditioning.breathiness.type": "float32",
            "seam.conditioning.breathiness.unit": "normalized-periodic-aperiodic-balance",
            "seam.conditioning.breathiness.minimum": "0",
            "seam.conditioning.breathiness.maximum": "1",
            "seam.conditioning.breathiness.default": "0",
            "seam.conditioning.breathiness.supported": "true",
        }
        metadata = {entry.key: entry.value for entry in acoustic_model.metadata_props}
        if (len(metadata) != len(acoustic_model.metadata_props)
                or any(metadata.get(key) != value for key, value in required.items())
                or any(key.startswith("seam.conditioning.") and key not in required for key in metadata)):
            raise ValueError("Breathiness graph metadata is missing, stale or unsupported")

        def captures(graph):
            definitions = ({entry.name for entry in graph.input}
                           | {entry.name for entry in graph.initializer}
                           | {name for node in graph.node for name in node.output if name})
            uses = {name for node in graph.node for name in node.input if name}
            for node in graph.node:
                for attribute in node.attribute:
                    if attribute.type == onnx.AttributeProto.GRAPH:
                        uses.update(captures(attribute.g))
                    elif attribute.type == onnx.AttributeProto.GRAPHS:
                        for nested in attribute.graphs:
                            uses.update(captures(nested))
            return uses - definitions

        reachable = {"breathiness"}
        changed = True
        while changed:
            changed = False
            for node in acoustic_model.graph.node:
                node_inputs = {name for name in node.input if name}
                for attribute in node.attribute:
                    if attribute.type == onnx.AttributeProto.GRAPH:
                        node_inputs.update(captures(attribute.g))
                    elif attribute.type == onnx.AttributeProto.GRAPHS:
                        for nested in attribute.graphs:
                            node_inputs.update(captures(nested))
                if reachable.isdisjoint(node_inputs):
                    continue
                before = len(reachable)
                reachable.update(name for name in node.output if name)
                changed = changed or len(reachable) != before
        if "mel" not in reachable:
            raise ValueError("Breathiness input does not reach the acoustic output")
    elif any(entry.key.startswith("seam.conditioning.") for entry in acoustic_model.metadata_props):
        raise ValueError("Acoustic graph declares conditioning metadata without a control input")
    steps_shape = tensor(inputs["steps"], TensorProto.INT64, 0 if steps_layout == "scalar" else 1)
    if steps_layout == "vector1" and steps_shape != [1]:
        raise ValueError("Steps vector must contain one value")

    inputs = tensors(vocoder, "inputs", ("mel", "f0"))
    outputs = tensors(vocoder, "outputs", (vocoder_output,))
    # Symbol spellings are scoped to each graph, not shared across files.
    vocoder_axis = mel(inputs["mel"])
    if sequence(inputs["f0"], TensorProto.FLOAT) != vocoder_axis:
        raise ValueError("Vocoder conditioning axes disagree")
    if sequence(outputs[vocoder_output], TensorProto.FLOAT) == vocoder_axis:
        raise ValueError("Audio samples cannot reuse the mel-frame axis")

    contract = {"profile": "seam-acoustic-vocoder-v1", "stepsLayout": steps_layout, "vocoderOutput": vocoder_output, "acousticHash": acoustic["sha256"],
                "vocoderHash": vocoder["sha256"], "bins": bins, "layout": layout,
                "hopSize": hop_size, "maximumSampleFrames": maximum_sample_frames,
                "maximumMelFrames": frames, "maximumMelElements": frames * bins}
    if conditioned:
        contract.update(conditioningRevision=2, conditioningControls=["breathiness"])
    digest = hashlib.sha256(json.dumps(contract, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    return {"status": "OFFLINE_PAIR_INSPECTED", "contract": contract,
            "contractHash": digest, "executionAdmitted": False, "releaseEligible": False}
