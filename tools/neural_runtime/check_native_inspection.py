"""Native schema parser differential smoke checks; not graph admission."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import onnx
import numpy as np
from onnx import helper, TensorProto
from check_paired_runtime import graphs
from inspect_graph import inspect_bytes


def main():
    executable = str(Path(sys.argv[1]).resolve())
    acoustic, vocoder = graphs()
    with tempfile.TemporaryDirectory(prefix="seam-native-parser-") as directory:
        path = Path(directory) / "graph.onnx"
        def run(payload, code):
            path.write_bytes(payload)
            result = subprocess.run([executable, str(path)], capture_output=True, timeout=10)
            assert result.returncode == code, (result.returncode, result.stderr)
            return result
        for payload in (acoustic, vocoder):
            expected = inspect_bytes(payload)
            actual = json.loads(run(payload, 0).stdout)
            assert actual["nodes"] == expected["nodes"]
            assert actual["tensors"] == expected["tensors"]
            assert actual["declaredTensorBytes"] == expected["declaredTensorBytes"]
            for key in ("inputs", "outputs", "irVersion", "opset"):
                assert actual[key] == expected[key], (key, actual[key], expected[key])
            assert not actual["executionAdmitted"]
        run(b"", 3)
        escaped = onnx.load_model_from_string(acoustic)
        old_name = escaped.graph.input[0].name
        new_name = 'tokens"\\\n測試'
        escaped.graph.input[0].name = new_name
        for node in escaped.graph.node:
            for index, name in enumerate(node.input):
                if name == old_name:
                    node.input[index] = new_name
        escaped_payload = escaped.SerializeToString()
        assert json.loads(run(escaped_payload, 0).stdout)["inputs"] == inspect_bytes(escaped_payload)["inputs"]
        run(acoustic[:-1], 4)
        run(acoustic + b"\xa0\x06\x01", 7)  # Unknown ModelProto field 100.
        model = onnx.load_model_from_string(acoustic)
        model.graph.node[0].domain = "custom"
        run(model.SerializeToString(), 8)
        model = onnx.load_model_from_string(acoustic)
        hidden = helper.make_tensor("hidden", TensorProto.FLOAT, [1], [0.0])
        hidden.external_data.add(key="location", value="/must/not/open")
        model.graph.node[0].attribute.append(helper.make_attribute("hidden", hidden))
        run(model.SerializeToString(), 9)
        for dims, dtype, code in (([65536, 65536], TensorProto.FLOAT, 10),
                                 ([-1], TensorProto.FLOAT, 10),
                                 ([1], TensorProto.STRING, 10)):
            model = onnx.load_model_from_string(acoustic)
            tensor = model.graph.initializer[0]
            tensor.dims[:] = dims
            tensor.data_type = dtype
            run(model.SerializeToString(), code)
        model = onnx.load_model_from_string(acoustic)
        for name in ("large-a", "large-b"):
            tensor = model.graph.initializer.add(name=name, data_type=TensorProto.DOUBLE)
            tensor.dims.append(64 * 1024 * 1024)
        run(model.SerializeToString(), 13)  # Missing data is rejected before the second tensor.
        model = onnx.load_model_from_string(acoustic)
        dimensions = model.graph.input[0].type.tensor_type.shape.dim
        dimensions[0].dim_value = 65536
        dimensions[1].dim_value = 65536
        run(model.SerializeToString(), 12)
        for mutation in ("short", "mixed", "wrong-field", "segment"):
            model = onnx.load_model_from_string(acoustic)
            tensor = model.graph.initializer[0]
            tensor.ClearField("raw_data")
            tensor.ClearField("float_data")
            tensor.ClearField("int64_data")
            tensor.ClearField("int32_data")
            if mutation == "short":
                tensor.raw_data = b"x"
            elif mutation == "mixed":
                tensor.raw_data = b"\0" * 4
                tensor.float_data.append(1.0)
            elif mutation == "wrong-field":
                tensor.string_data.append(b"wrong")
            else:
                tensor.segment.begin = 0
                tensor.segment.end = 1
            run(model.SerializeToString(), 13)
        for dtype, numpy_type in ((TensorProto.FLOAT, np.float32), (TensorProto.DOUBLE, np.float64),
                                 (TensorProto.FLOAT16, np.float16), (TensorProto.INT32, np.int32),
                                 (TensorProto.INT64, np.int64), (TensorProto.INT8, np.int8),
                                 (TensorProto.UINT8, np.uint8), (TensorProto.BOOL, np.bool_)):
            for raw in (False, True):
                tensor = (onnx.numpy_helper.from_array(np.array([0, 1], dtype=numpy_type), "values")
                          if raw else helper.make_tensor("values", dtype, [2], [0, 1]))
                model = helper.make_model(helper.make_graph(
                    [helper.make_node("Identity", ["values"], ["output"])], "storage", [],
                    [helper.make_tensor_value_info("output", dtype, [2])], [tensor]),
                    opset_imports=[helper.make_opsetid("", 17)], ir_version=9)
                payload = model.SerializeToString()
                assert json.loads(run(payload, 0).stdout)["declaredTensorBytes"] == inspect_bytes(payload)["declaredTensorBytes"]
        for dtype, field, value in ((TensorProto.FLOAT, "float_data", float("nan")),
                                    (TensorProto.DOUBLE, "double_data", float("inf")),
                                    (TensorProto.FLOAT16, "int32_data", 0x7c00),
                                    (TensorProto.FLOAT16, "int32_data", 65536),
                                    (TensorProto.BOOL, "int32_data", 2),
                                    (TensorProto.INT8, "int32_data", -129),
                                    (TensorProto.UINT8, "int32_data", 256)):
            model = onnx.load_model_from_string(acoustic)
            tensor = model.graph.initializer.add(name="invalid-value", data_type=dtype)
            tensor.dims.append(1)
            getattr(tensor, field).append(value)
            run(model.SerializeToString(), 14)
        for dtype, raw in ((TensorProto.FLOAT, b"\0\0\x80\x7f"),
                           (TensorProto.DOUBLE, b"\0\0\0\0\0\0\xf0\x7f"),
                           (TensorProto.FLOAT16, b"\0\x7c"), (TensorProto.BOOL, b"\x02")):
            model = onnx.load_model_from_string(acoustic)
            tensor = model.graph.initializer.add(name="invalid-raw", data_type=dtype, raw_data=raw)
            tensor.dims.append(1)
            run(model.SerializeToString(), 14)
        for mutation in ("unknown-operator", "missing-input", "unknown-attribute"):
            model = onnx.load_model_from_string(acoustic)
            node = model.graph.node[0]
            if mutation == "unknown-operator":
                node.op_type = "NotAnOnnxOperator"
            elif mutation == "missing-input":
                node.input[0] = "undeclared-source"
            else:
                node.attribute.append(helper.make_attribute("not_a_supported_attribute", 1))
            run(model.SerializeToString(), 15)
        acoustic_path, vocoder_path = Path(directory) / "acoustic", Path(directory) / "vocoder"
        private_name = "MODEL_PRIVATE_NAME_" + "x" * 128 + "\nFORGED_LOG_ENTRY"
        model = onnx.load_model_from_string(acoustic)
        model.graph.node[0].input[0] = private_name
        diagnostic = run(model.SerializeToString(), 15)
        assert diagnostic.stderr == b"Native ONNX inspection failed (code 15)\n"
        assert not diagnostic.stdout
        model.graph.node[0].input[0] = "x" * 4097
        run(model.SerializeToString(), 19)
        model = onnx.load_model_from_string(acoustic)
        model.doc_string = "x" * 4096
        run(model.SerializeToString(), 0)
        for index in range(2049):
            model.metadata_props.add(key=str(index), value="x" * 4096)
        run(model.SerializeToString(), 19)
        for steps, output in (("scalar", "audio"), ("vector1", "waveform")):
            a, v = graphs(steps_layout=steps, vocoder_output=output)
            acoustic_path.write_bytes(a)
            vocoder_path.write_bytes(v)
            command = [executable, "--pair", str(acoustic_path), str(vocoder_path), "80", "BTF", steps, output, "256", "48000"]
            accepted = subprocess.run(command, capture_output=True, timeout=10)
            assert accepted.returncode == 0, accepted.stderr
            assert json.loads(accepted.stdout)["status"] == "NATIVE_PAIR_STRUCTURE_CHECKED"
            for index, bad in ((4, "79"), (5, "BFT"), (6, "vector1" if steps == "scalar" else "scalar"),
                               (7, "waveform" if output == "audio" else "audio"), (8, "0"), (9, "4194305")):
                changed = command.copy()
                changed[index] = bad
                rejected = subprocess.run(changed, capture_output=True, timeout=10)
                assert rejected.returncode == 17, (changed, rejected.returncode, rejected.stderr)
    print("Native checker: tensor regressions and two pair profiles with 12 mismatch rejections passed. No execution admission.")


if __name__ == "__main__":
    main()
