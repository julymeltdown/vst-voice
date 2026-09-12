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


def graph(path, input_name, output_name, scale):
    model = helper.make_model(helper.make_graph([
        helper.make_node("Mul", [input_name, "scale"], [output_name])],
        "seam-arithmetic-fixture",
        [helper.make_tensor_value_info(input_name, TensorProto.FLOAT, [1, 4])],
        [helper.make_tensor_value_info(output_name, TensorProto.FLOAT, [1, 4])],
        [helper.make_tensor("scale", TensorProto.FLOAT, [1], [scale])]),
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
        for _ in range(2):
            result = subprocess.run([binary, str(acoustic), str(vocoder)], check=True,
                                    capture_output=True, text=True, timeout=20)
            assert json.loads(result.stdout) == {
                "status": "ARITHMETIC_INFERENCE_ONLY", "samples": 4, "releaseEligible": False}
        graph(vocoder, "mel", "audio", 0.5)
        assert subprocess.run([binary, str(acoustic), str(vocoder)], capture_output=True, timeout=20).returncode == 6
        assert subprocess.run([binary, str(vocoder), str(acoustic)], capture_output=True, timeout=20).returncode == 1
    print("Native two-session ONNX inference passed; this is not a singing model.")


if __name__ == "__main__":
    main()
