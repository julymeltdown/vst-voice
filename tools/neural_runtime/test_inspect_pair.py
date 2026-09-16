"""Structural profile tests, not learned models or inference acceptance."""
import unittest

from onnx import TensorProto as T, helper

from inspect_pair import inspect_pair
from check_paired_runtime import graphs


def fixture(acoustic=True, layout="BTF"):
    def value(name, dtype, shape):
        return helper.make_tensor_value_info(name, dtype, shape)
    mel_shape = [1, "frames", 80] if layout == "BTF" else [1, 80, "frames"]
    if acoustic:
        inputs = [value("tokens", T.INT64, [1, "phones"]),
                  value("durations", T.INT64, [1, "phones"]),
                  value("f0", T.FLOAT, [1, "frames"]), value("steps", T.INT64, [])]
        output = value("mel", T.FLOAT, mel_shape)
        constant_shape = [1, 2, 80] if layout == "BTF" else [1, 80, 2]
        constant = helper.make_tensor("constant", T.FLOAT, constant_shape, [0.0] * 160)
    else:
        inputs = [value("mel", T.FLOAT, mel_shape), value("f0", T.FLOAT, [1, "frames"])]
        output = value("audio", T.FLOAT, [1, "samples"])
        constant = helper.make_tensor("constant", T.FLOAT, [1, 512], [0.0] * 512)
    model = helper.make_model(helper.make_graph(
        [helper.make_node("Constant", [], [output.name], value=constant)],
        "schema-only-fixture", inputs, [output]), opset_imports=[helper.make_opsetid("", 17)])
    model.ir_version = 9
    return model


class PairTests(unittest.TestCase):
    def inspect(self, acoustic=None, vocoder=None, **options):
        args = dict(bins=80, layout="BTF", hop_size=256, maximum_sample_frames=48000)
        args.update(options)
        return inspect_pair((acoustic or fixture()).SerializeToString(),
                            (vocoder or fixture(False)).SerializeToString(), **args)

    def conditioned(self):
        model = fixture()
        model.graph.input.append(helper.make_tensor_value_info("breathiness", T.FLOAT, [1, "frames"]))
        model.graph.node[0].output[0] = "base"
        model.graph.node.extend([
            helper.make_node("ReduceSum", ["breathiness"], ["breathiness_sum"], keepdims=0),
            helper.make_node("Add", ["base", "breathiness_sum"], ["mel"]),
        ])
        for key, value in {
                "seam.conditioning.revision": "2",
                "seam.conditioning.breathiness.type": "float32",
                "seam.conditioning.breathiness.unit": "normalized-periodic-aperiodic-balance",
                "seam.conditioning.breathiness.minimum": "0",
                "seam.conditioning.breathiness.maximum": "1",
                "seam.conditioning.breathiness.default": "0",
                "seam.conditioning.breathiness.supported": "true",
        }.items():
            entry = model.metadata_props.add()
            entry.key, entry.value = key, value
        return model

    def test_both_layouts_and_hash_binding(self):
        for layout in ("BTF", "BFT"):
            report = self.inspect(fixture(layout=layout), fixture(False, layout), layout=layout)
            self.assertEqual(report["contract"]["maximumMelFrames"], 188)
            self.assertEqual(report["contract"]["maximumMelElements"], 15040)
            self.assertFalse(report["executionAdmitted"])
        changed = fixture(); changed.doc_string = "different immutable bytes"
        self.assertNotEqual(self.inspect()["contractHash"], self.inspect(changed)["contractHash"])

    def test_wrong_bins_and_layout(self):
        for options in ({"bins": 81}, {"layout": "BFT"}):
            with self.assertRaisesRegex(ValueError, "Mel layout"):
                self.inspect(**options)

    def test_axis_mismatch(self):
        model = fixture()
        model.graph.input[1].type.tensor_type.shape.dim[1].dim_param = "other"
        with self.assertRaisesRegex(ValueError, "duration axes"):
            self.inspect(model)

    def test_missing_condition(self):
        model = fixture(False); del model.graph.input[1]
        with self.assertRaisesRegex(ValueError, "Unexpected inputs"):
            self.inspect(vocoder=model)

    def test_dtype(self):
        model = fixture(); model.graph.input[0].type.tensor_type.elem_type = T.FLOAT
        with self.assertRaisesRegex(ValueError, "dtype"):
            self.inspect(model)

    def test_audio_axis(self):
        model = fixture(False)
        model.graph.output[0].type.tensor_type.shape.dim[1].dim_param = "frames"
        with self.assertRaisesRegex(ValueError, "Audio samples"):
            self.inspect(vocoder=model)

    def test_cross_graph_symbol_spelling_is_not_identity(self):
        model = fixture(False)
        for entry in model.graph.input:
            entry.type.tensor_type.shape.dim[1].dim_param = "vocoder_time"
        self.assertEqual(self.inspect(vocoder=model)["status"], "OFFLINE_PAIR_INSPECTED")

    def test_breathiness_requires_exact_metadata_shape_and_output_reachability(self):
        model = self.conditioned()
        report = self.inspect(model)
        self.assertEqual(report["contract"]["conditioningRevision"], 2)
        self.assertEqual(report["contract"]["conditioningControls"], ["breathiness"])
        model.metadata_props[2].value = "normalized-amplitude"
        with self.assertRaisesRegex(ValueError, "metadata"):
            self.inspect(model)
        ignored = self.conditioned()
        del ignored.graph.node[-2:]
        ignored.graph.node[0].output[0] = "mel"
        with self.assertRaisesRegex(ValueError, "does not reach"):
            self.inspect(ignored)

    def test_bounds(self):
        for options in ({"bins": True}, {"hop_size": 0}, {"maximum_sample_frames": 4194305},
                        {"bins": 512, "hop_size": 1, "maximum_sample_frames": 4194304}):
            with self.assertRaises(ValueError):
                self.inspect(**options)

    def test_explicit_openutau_steps_vector(self):
        acoustic, vocoder = graphs(steps_layout="vector1")
        args = dict(bins=80, layout="BTF", hop_size=256, maximum_sample_frames=48000)
        with self.assertRaisesRegex(ValueError, "rank"):
            inspect_pair(acoustic, vocoder, **args)
        report = inspect_pair(acoustic, vocoder, steps_layout="vector1", **args)
        self.assertEqual(report["contract"]["stepsLayout"], "vector1")
        with self.assertRaisesRegex(ValueError, "steps layout"):
            inspect_pair(acoustic, vocoder, steps_layout="guessed", **args)

    def test_explicit_exporter_waveform_output(self):
        acoustic, vocoder = graphs(vocoder_output="waveform")
        args = dict(bins=80, layout="BTF", hop_size=256, maximum_sample_frames=48000)
        with self.assertRaisesRegex(ValueError, "Unexpected outputs"):
            inspect_pair(acoustic, vocoder, **args)
        report = inspect_pair(acoustic, vocoder, vocoder_output="waveform", **args)
        self.assertEqual(report["contract"]["vocoderOutput"], "waveform")


if __name__ == "__main__":
    unittest.main()
