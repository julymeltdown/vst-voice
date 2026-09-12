"""Create deterministic arithmetic graphs and exercise the native ORT probe.

Requires onnx==1.19.1 in an isolated fixture environment. No pretrained weights,
voice material, graph downloads or musical-quality claim are involved.
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import onnx
from onnx import TensorProto, helper
from inspect_graph import inspect_bytes


def graph(path, input_name, output_name, scale, *, shape=(1, 4), dtype=TensorProto.FLOAT):
    model = helper.make_model(helper.make_graph([
        helper.make_node("Mul", [input_name, "scale"], [output_name])],
        "seam-arithmetic-fixture",
        [helper.make_tensor_value_info(input_name, dtype, shape)],
        [helper.make_tensor_value_info(output_name, dtype, shape)],
        [helper.make_tensor("scale", dtype, [1], [scale])]),
        opset_imports=[helper.make_opsetid("", 17)])
    model.ir_version = 9
    onnx.checker.check_model(model)
    onnx.save(model, path)


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="seam-ort-probe-") as temporary:
        root = Path(temporary)
        acoustic, vocoder = root / "acoustic.onnx", root / "vocoder.onnx"
        graph(acoustic, "f0", "mel", 2.0)
        graph(vocoder, "mel", "audio", 0.25)
        for path in (acoustic, vocoder):
            report = inspect_bytes(path.read_bytes())
            assert report["status"] == "OFFLINE_GRAPH_INSPECTED"
            assert report["inputs"][0]["shape"] == [1, 4]
        for _ in range(2):
            result = subprocess.run([binary, str(acoustic), str(vocoder)], check=True,
                                    capture_output=True, text=True, timeout=20)
            assert json.loads(result.stdout) == {
                "status": "ARITHMETIC_INFERENCE_ONLY", "samples": 4, "releaseEligible": False}
        graph(vocoder, "mel", "audio", 0.5)
        assert subprocess.run([binary, str(acoustic), str(vocoder)], capture_output=True, timeout=20).returncode == 6
        assert subprocess.run([binary, str(vocoder), str(acoustic)], capture_output=True, timeout=20).returncode == 3
        for shape, dtype in [((1, 5), TensorProto.FLOAT), ((4,), TensorProto.FLOAT),
                             ((1, "frames"), TensorProto.FLOAT), ((1, 4), TensorProto.DOUBLE)]:
            graph(vocoder, "mel", "audio", 0.25, shape=shape, dtype=dtype)
            assert subprocess.run([binary, str(acoustic), str(vocoder)], capture_output=True, timeout=20).returncode == 3
        for payload in (b"", b"x" * (16 * 1024 * 1024 + 1)):
            vocoder.write_bytes(payload)
            assert subprocess.run([binary, str(acoustic), str(vocoder)], capture_output=True, timeout=20).returncode == 7
    print("Native two-session ONNX inference passed; this is not a singing model.")


if __name__ == "__main__":
    main()
