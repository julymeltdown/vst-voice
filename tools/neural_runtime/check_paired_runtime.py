"""Execute dynamic paired-profile arithmetic, not trained singing models."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import onnx
from onnx import TensorProto as T, helper as h

from inspect_pair import inspect_pair


def graphs(hop=256):
    def value(name, dtype, shape):
        return h.make_tensor_value_info(name, dtype, shape)
    def constant(name, dtype, shape, data):
        return h.make_tensor(name, dtype, shape, data)
    def model(nodes, inputs, outputs, constants):
        result = h.make_model(h.make_graph(nodes, "paired-arithmetic", inputs, outputs, constants),
                              opset_imports=[h.make_opsetid("", 17)])
        result.ir_version = 9
        onnx.checker.check_model(result)
        return result.SerializeToString()

    constants = [constant("axis", T.INT64, [1], [2]), constant("bins", T.INT64, [1], [80]),
                 constant("f0scale", T.FLOAT, [], [0.001])]
    nodes = [h.make_node("Mul", ["f0", "f0scale"], ["base"])]
    previous = "base"
    for source, scale in (("tokens", 0.01), ("durations", 0.001), ("steps", 0.0001)):
        nodes += [h.make_node("Cast", [source], [source + "float"], to=T.FLOAT),
                  h.make_node("ReduceSum", [source + "float"], [source + "sum"], keepdims=0),
                  h.make_node("Mul", [source + "sum", source + "scale"], [source + "scaled"]),
                  h.make_node("Add", [previous, source + "scaled"], [source + "added"])]
        constants.append(constant(source + "scale", T.FLOAT, [], [scale]))
        previous = source + "added"
    nodes += [h.make_node("Unsqueeze", [previous, "axis"], ["singlebin"]),
              h.make_node("Shape", ["f0"], ["f0shape"]),
              h.make_node("Concat", ["f0shape", "bins"], ["melshape"], axis=0),
              h.make_node("Expand", ["singlebin", "melshape"], ["mel"])]
    acoustic = model(nodes, [value("tokens", T.INT64, [1, "phones"]),
                            value("durations", T.INT64, [1, "phones"]),
                            value("f0", T.FLOAT, [1, "frames"]), value("steps", T.INT64, [])],
                     [value("mel", T.FLOAT, [1, "frames", 80])], constants)
    vocoder = model([
        h.make_node("ReduceMean", ["mel"], ["mean"], axes=[2], keepdims=0),
        h.make_node("Mul", ["f0", "scale"], ["pitch"]),
        h.make_node("Add", ["mean", "pitch"], ["mixed"]),
        h.make_node("Unsqueeze", ["mixed", "axis"], ["one"]),
        h.make_node("Shape", ["f0"], ["f0shape"]),
        h.make_node("Concat", ["f0shape", "hop"], ["expandedshape"], axis=0),
        h.make_node("Expand", ["one", "expandedshape"], ["expanded"]),
        h.make_node("Reshape", ["expanded", "audioshape"], ["audio"])],
        [value("mel", T.FLOAT, [1, "frames", 80]), value("f0", T.FLOAT, [1, "frames"])],
        [value("audio", T.FLOAT, [1, "samples"])],
        [constant("scale", T.FLOAT, [], [0.0001]), constant("axis", T.INT64, [1], [2]),
         constant("hop", T.INT64, [1], [hop]), constant("audioshape", T.INT64, [2], [1, -1])])
    return acoustic, vocoder


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="seam-paired-runtime-") as directory:
        paths = [Path(directory) / name for name in ("acoustic.onnx", "vocoder.onnx")]
        for hop, expected_code in ((256, 0), (128, 5)):
            acoustic, vocoder = graphs(hop)
            # The wrong-hop case intentionally passes static inspection: runtime
            # output checking must catch what declarations cannot establish.
            inspect_pair(acoustic, vocoder, bins=80, layout="BTF", hop_size=256,
                         maximum_sample_frames=48000)
            for path, payload in zip(paths, (acoustic, vocoder)):
                path.write_bytes(payload)
            result = subprocess.run([binary, *(str(path) for path in paths), "--paired-profile"],
                                    capture_output=True, text=True, timeout=20)
            assert result.returncode == expected_code, (result.returncode, result.stderr)
            if expected_code == 0:
                assert json.loads(result.stdout) == {
                    "status": "PAIRED_PROFILE_INFERENCE_ONLY", "cases": 2, "releaseEligible": False}
    print("Dynamic four-input acoustic and pitch-conditioned vocoder execution passed; no singing claim.")


if __name__ == "__main__":
    main()
