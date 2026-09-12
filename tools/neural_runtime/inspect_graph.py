"""Offline, data-only ONNX intake inspection; not production authorization.

No inference sessions, external-data loading, custom libraries or model code.
The byte cap bounds parser input, not the Python process's resident memory.
"""
import hashlib
import json
from pathlib import Path
import sys

import onnx
from onnx import TensorProto

MAX_BYTES = 256 * 1024 * 1024
MAX_ELEMENTS = 64 * 1024 * 1024
MAX_MESSAGES = 200_000


def inspect_bytes(payload: bytes) -> dict:
    if not payload or len(payload) > MAX_BYTES:
        raise ValueError("Graph byte bound exceeded or empty input")
    # Parse bytes directly: onnx.load(path) can resolve external tensor files.
    model = onnx.ModelProto()
    model.ParseFromString(payload)
    if model.functions or model.training_info:
        raise ValueError("Local functions and training graphs are not admitted")
    if not model.HasField("graph") or not 1 <= model.ir_version <= 10:
        raise ValueError("Missing graph or unsupported IR version")
    if len(model.opset_import) != 1 or model.opset_import[0].domain != "":
        raise ValueError("Only the standard ONNX operator domain is admitted")
    opset = model.opset_import[0].version
    if not 13 <= opset <= 21:
        raise ValueError("Unsupported opset revision")
    stack = [(model, 0)]
    visited = nodes = tensors = 0
    while stack:
        message, depth = stack.pop()
        visited += 1
        if visited > MAX_MESSAGES or depth > 64:
            raise ValueError("Graph structure bound exceeded")
        if isinstance(message, onnx.NodeProto):
            nodes += 1
            if message.domain or message.overload:
                raise ValueError("Custom operator domain or overload rejected")
            try:
                onnx.defs.get_schema(message.op_type, opset, "")
            except onnx.defs.SchemaError as error:
                raise ValueError("Unknown standard operator") from error
        if isinstance(message, TensorProto):
            tensors += 1
            if message.external_data or message.data_location == TensorProto.EXTERNAL:
                raise ValueError("External tensor references rejected")
            if message.data_type not in (TensorProto.FLOAT, TensorProto.FLOAT16,
                    TensorProto.DOUBLE, TensorProto.INT32, TensorProto.INT64,
                    TensorProto.INT8, TensorProto.UINT8, TensorProto.BOOL):
                raise ValueError("Unsupported tensor element type")
            elements = 1
            if len(message.dims) > 8:
                raise ValueError("Tensor rank bound exceeded")
            for dimension in message.dims:
                if dimension < 0 or dimension > MAX_ELEMENTS:
                    raise ValueError("Tensor dimension bound exceeded")
                elements *= dimension
                if elements > MAX_ELEMENTS:
                    raise ValueError("Tensor element bound exceeded")
        for field, value in message.ListFields():
            if field.message_type is not None:
                children = value if field.is_repeated else (value,)
                if len(stack) + len(children) + visited > MAX_MESSAGES:
                    raise ValueError("Graph structure bound exceeded")
                stack.extend((child, depth + 1) for child in children)
    # Structural/type consistency only. Does not execute operators or open files.
    onnx.checker.check_model(model, full_check=False)

    def interface(values):
        result = []
        for value in values:
            if not value.type.HasField("tensor_type"):
                raise ValueError("Non-tensor graph interface rejected")
            tensor = value.type.tensor_type
            if not tensor.HasField("shape") or len(tensor.shape.dim) > 8:
                raise ValueError("Unspecified or excessive interface rank")
            shape = []
            for dimension in tensor.shape.dim:
                if dimension.HasField("dim_value"):
                    if not 0 <= dimension.dim_value <= MAX_ELEMENTS:
                        raise ValueError("Interface dimension bound exceeded")
                    shape.append(dimension.dim_value)
                elif dimension.HasField("dim_param") and dimension.dim_param:
                    shape.append(dimension.dim_param)
                else:
                    raise ValueError("Unnamed dynamic dimension rejected")
            result.append({"name": value.name, "dtype": tensor.elem_type, "shape": shape})
        return result

    return {"status": "OFFLINE_GRAPH_INSPECTED", "sha256": hashlib.sha256(payload).hexdigest(),
            "bytes": len(payload), "irVersion": model.ir_version, "opset": opset,
            "nodes": nodes, "tensors": tensors, "inputs": interface(model.graph.input),
            "outputs": interface(model.graph.output), "releaseEligible": False}


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: inspect_graph.py GRAPH.onnx")
    with Path(sys.argv[1]).open("rb") as stream:
        payload = stream.read(MAX_BYTES + 1)
    print(json.dumps(inspect_bytes(payload), sort_keys=True))


if __name__ == "__main__":
    main()
