"""The exported acoustic graph must sample reproducibly."""
import importlib.util
import unittest

from tools.voice_model_training.onnx_acoustic import (
    MAXIMUM_SAMPLING_SEED, RANDOM_OPS, SAMPLING_SEED, pin_sampling_seed,
)

# ONNX is an optional export dependency; the seeding helper itself only needs the
# protobuf message types, so the skip covers both the helper and the checker.
HAVE_ONNX = importlib.util.find_spec("onnx") is not None


def graph_with(op_type="RandomNormalLike", **attributes):
    """A minimal graph holding one sampling node and one honest tensor output."""
    from onnx import TensorProto, helper

    node = helper.make_node(op_type, ["x"], ["y"], **attributes)
    value = helper.make_tensor_value_info("x", TensorProto.FLOAT, [1, 4])
    out = helper.make_tensor_value_info("y", TensorProto.FLOAT, [1, 4])
    model = helper.make_model(helper.make_graph([node], "g", [value], [out]),
                              opset_imports=[helper.make_opsetid("", 17)])
    model.ir_version = 8
    return model


@unittest.skipUnless(HAVE_ONNX, "Optional ONNX export environment not installed")
class SamplingSeedTests(unittest.TestCase):
    def test_an_unseeded_sampling_node_is_pinned(self):
        import onnx

        model = graph_with()
        self.assertEqual(pin_sampling_seed(model), 1)
        attributes = {attribute.name: attribute for attribute in model.graph.node[0].attribute}
        self.assertIn("seed", attributes)
        self.assertEqual(attributes["seed"].f, float(SAMPLING_SEED))
        # The graph must still be a valid model after the edit, not merely edited.
        onnx.checker.check_model(model, full_check=True)

    def test_every_random_op_type_is_covered(self):
        for op_type in RANDOM_OPS:
            with self.subTest(op_type=op_type):
                model = graph_with(op_type)
                self.assertEqual(pin_sampling_seed(model), 1)

    def test_an_already_matching_seed_is_left_alone(self):
        import onnx

        model = graph_with(seed=float(SAMPLING_SEED))
        self.assertEqual(pin_sampling_seed(model), 0)
        onnx.checker.check_model(model, full_check=True)

    def test_a_conflicting_seed_is_refused_rather_than_rewritten(self):
        # Two exports of the same checkpoint must not disagree about sampling simply
        # because one of them was assembled after the seed was already present.
        model = graph_with(seed=7.0)
        with self.assertRaises(ValueError):
            pin_sampling_seed(model)

    def test_a_graph_without_sampling_is_reported_as_unpinned(self):
        from onnx import TensorProto, helper

        node = helper.make_node("Identity", ["x"], ["y"])
        value = helper.make_tensor_value_info("x", TensorProto.FLOAT, [1, 4])
        out = helper.make_tensor_value_info("y", TensorProto.FLOAT, [1, 4])
        model = helper.make_model(helper.make_graph([node], "g", [value], [out]),
                                  opset_imports=[helper.make_opsetid("", 17)])
        model.ir_version = 8
        self.assertEqual(pin_sampling_seed(model), 0)

    def test_invalid_seeds_are_refused(self):
        for seed in (-1, MAXIMUM_SAMPLING_SEED, 2**31, 1.5, True, "7"):
            with self.subTest(seed=seed), self.assertRaises(ValueError):
                pin_sampling_seed(graph_with(), seed)

    def test_the_shipped_seed_survives_a_float32_round_trip(self):
        # ONNX stores the seed as float32. A constant that is not exactly
        # representable is silently rounded, so the value written is not the value
        # compared later; this pins the shipped constant against that trap.
        import struct
        self.assertLess(SAMPLING_SEED, MAXIMUM_SAMPLING_SEED)
        self.assertEqual(struct.unpack("<f", struct.pack("<f", float(SAMPLING_SEED)))[0],
                         float(SAMPLING_SEED))
        model = graph_with()
        pin_sampling_seed(model)
        # Re-pinning the same graph must be idempotent rather than a conflict.
        self.assertEqual(pin_sampling_seed(model), 0)

    @unittest.skipUnless(__import__("importlib.util", fromlist=["x"]).find_spec("numpy"),
                         "optional NumPy required")
    def test_a_pinned_graph_repeats_across_processes(self):
        import subprocess
        import sys
        import tempfile
        from pathlib import Path
        import numpy as np
        import onnxruntime as ort
        model = graph_with()
        pin_sampling_seed(model)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "acoustic.onnx"
            path.write_bytes(model.SerializeToString())
            # A fresh session per process is what the worker actually ships: one
            # request per process, so cross-process agreement is the requirement.
            script = ("import numpy, onnxruntime as ort, sys\n"
                      "s = ort.InferenceSession(sys.argv[1], providers=['CPUExecutionProvider'])\n"
                      "print(float(s.run(None, {'x': numpy.zeros((1, 4), numpy.float32)})[0][0, 0]))\n")
            probe = Path(directory) / "probe.py"
            probe.write_text(script)
            runs = [subprocess.run([sys.executable, str(probe), str(path)], capture_output=True,
                                   text=True, timeout=120) for _ in range(3)]
            for run in runs:
                self.assertEqual(run.returncode, 0, run.stderr[-200:])
            self.assertEqual(len({run.stdout.strip() for run in runs}), 1)
            # A same-session repeat is not the shipped contract, but the seeded value
            # must at least be a real sample rather than a constant zero.
            session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
            first = session.run(None, {"x": np.zeros((1, 4), np.float32)})[0]
            self.assertNotEqual(float(np.abs(first).sum()), 0.0)


if __name__ == "__main__":
    unittest.main()
