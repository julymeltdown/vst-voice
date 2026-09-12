"""Inspect bytes against the proposed SEAM acoustic/vocoder export profile.

This is not a claim that arbitrary DiffSinger exports use this profile. Export
adapters must target it explicitly; no tensor renaming or reshaping is guessed.
"""
import hashlib
import json

from onnx import TensorProto

from inspect_graph import MAX_ELEMENTS, inspect_bytes


def inspect_pair(acoustic_bytes: bytes, vocoder_bytes: bytes, *, bins: int,
                 layout: str, hop_size: int, maximum_sample_frames: int) -> dict:
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

    inputs = tensors(acoustic, "inputs", ("tokens", "durations", "f0", "steps"))
    outputs = tensors(acoustic, "outputs", ("mel",))
    token_axis = sequence(inputs["tokens"], TensorProto.INT64)
    if sequence(inputs["durations"], TensorProto.INT64) != token_axis:
        raise ValueError("Token and duration axes disagree")
    frame_axis = sequence(inputs["f0"], TensorProto.FLOAT)
    if frame_axis == token_axis or mel(outputs["mel"]) != frame_axis:
        raise ValueError("Acoustic time axes disagree")
    tensor(inputs["steps"], TensorProto.INT64, 0)

    inputs = tensors(vocoder, "inputs", ("mel", "f0"))
    outputs = tensors(vocoder, "outputs", ("audio",))
    # Symbol spellings are scoped to each graph, not shared across files.
    vocoder_axis = mel(inputs["mel"])
    if sequence(inputs["f0"], TensorProto.FLOAT) != vocoder_axis:
        raise ValueError("Vocoder conditioning axes disagree")
    if sequence(outputs["audio"], TensorProto.FLOAT) == vocoder_axis:
        raise ValueError("Audio samples cannot reuse the mel-frame axis")

    contract = {"profile": "seam-acoustic-vocoder-v1", "acousticHash": acoustic["sha256"],
                "vocoderHash": vocoder["sha256"], "bins": bins, "layout": layout,
                "hopSize": hop_size, "maximumSampleFrames": maximum_sample_frames,
                "maximumMelFrames": frames, "maximumMelElements": frames * bins}
    digest = hashlib.sha256(json.dumps(contract, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    return {"status": "OFFLINE_PAIR_INSPECTED", "contract": contract,
            "contractHash": digest, "executionAdmitted": False, "releaseEligible": False}
