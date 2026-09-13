"""Run with the pinned fixture environment; no inference required."""
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import onnx
from onnx import TensorProto, helper

from check_runtime import graph
from inspect_graph import inspect_bytes


class InspectionTests(unittest.TestCase):
    def setUp(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.onnx"
            graph(path, "f0", "mel", 2.0)
            self.payload = path.read_bytes()
        self.model = onnx.load_model_from_string(self.payload)

    def reject(self, pattern):
        with self.assertRaisesRegex(ValueError, pattern):
            inspect_bytes(self.model.SerializeToString())

    def test_hash_and_actual_interface(self):
        report = inspect_bytes(self.payload)
        self.assertEqual(report["sha256"], hashlib.sha256(self.payload).hexdigest())
        self.assertEqual(report["inputs"], [{"name": "f0", "dtype": 1, "shape": [1, 4]}])
        self.assertEqual(report["nodes"], 1)
        self.assertEqual(report["operators"], {"Mul": 1})
        self.assertEqual(report["declaredTensorBytes"], 4)
        self.assertFalse(report["releaseEligible"])

    def test_external_initializer(self):
        tensor = self.model.graph.initializer[0]
        tensor.data_location = TensorProto.EXTERNAL
        tensor.external_data.add(key="location", value="/must/not/be/opened")
        self.reject("External tensor")

    def test_external_attribute_tensor(self):
        tensor = helper.make_tensor("hidden", TensorProto.FLOAT, [1], [0.0])
        tensor.external_data.add(key="location", value="../../outside")
        self.model.graph.node[0].attribute.append(helper.make_attribute("hidden", tensor))
        self.reject("External tensor")

    def test_nested_graph_custom_operator(self):
        nested = helper.make_graph([helper.make_node("RunCode", [], [], domain="custom")], "nested", [], [])
        self.model.graph.node[0].attribute.append(helper.make_attribute("body", nested))
        self.reject("Custom operator")

    def test_unknown_standard_operator(self):
        self.model.graph.node[0].op_type = "ExecuteArbitraryCode"
        self.reject("Unknown standard")

    def test_shape_product(self):
        self.model.graph.initializer[0].dims[:] = [65536, 65536]
        self.reject("element bound")

    def test_custom_import(self):
        self.model.opset_import.add(domain="custom", version=1)
        self.reject("standard ONNX")

    def test_aggregate_tensor_storage(self):
        # Each tensor fits individually; combined decoded storage does not.
        self.model.graph.initializer.append(helper.make_tensor("other", TensorProto.DOUBLE, [1], [1.0]))
        with patch("inspect_graph.MAX_TENSOR_BYTES", 8):
            self.reject("Aggregate declared tensor")

    def test_interface_static_product(self):
        shape = self.model.graph.input[0].type.tensor_type.shape
        shape.dim[0].dim_value = 65536
        shape.dim[1].dim_value = 65536
        self.reject("Interface static dimension product")

    def test_interface_type_rejected(self):
        # Valid ONNX string interface, but outside this numeric intake family.
        model = helper.make_model(helper.make_graph(
            [helper.make_node("Identity", ["x"], ["y"])], "strings",
            [helper.make_tensor_value_info("x", TensorProto.STRING, [1])],
            [helper.make_tensor_value_info("y", TensorProto.STRING, [1])]),
            opset_imports=[helper.make_opsetid("", 17)], ir_version=9)
        self.model = model
        self.reject("Unsupported interface")

    def test_dynamic_interface_reported_not_execution_admitted(self):
        dim = self.model.graph.input[0].type.tensor_type.shape.dim[1]
        dim.dim_param = "frames"
        report = inspect_bytes(self.model.SerializeToString())
        self.assertEqual(report["inputs"][0]["shape"], [1, "frames"])
        self.assertFalse(report["releaseEligible"])

    def test_empty(self):
        with self.assertRaisesRegex(ValueError, "empty"):
            inspect_bytes(b"")


if __name__ == "__main__":
    unittest.main()
