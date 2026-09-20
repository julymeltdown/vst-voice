import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from tools.voice_model_training.prepare_bundle import (BREATHINESS_CONTROL,
    SUPPORTED_PROFILE, canonical_json, configuration_asset, load_breathiness_prior,
    prepare, sha256)

TOKENS = ["<PAD>", "SP", "a", "z"]


def row(symbol, breathiness, windows, source_mean, measured, periodic):
    return dict(symbol=symbol, breathiness=breathiness, windows=windows,
                sourceMean=source_mean, measured=measured, periodic=periodic)


def prior_document(symbols, **overrides):
    document = dict(formatId="com.project-seam.breathiness-prior", schemaVersion=1,
                    revision=1, estimator="aperiodicity-r1", minimumWindows=1,
                    symbols=symbols, supervisionAdmitted=False,
                    singerQualified=False, releaseEligible=False)
    document.update(overrides)
    return document


class BreathinessPriorTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "prior.json"

    def tearDown(self):
        self.directory.cleanup()

    def write(self, document):
        payload = json.dumps(document, sort_keys=True).encode()
        self.path.write_bytes(payload)
        return sha256(payload)

    def load(self, document, tokens=TOKENS):
        return load_breathiness_prior(self.path, self.write(document), tokens)

    def test_measured_rows_become_defaults(self):
        document = prior_document([
            row("z", 0.4, 12, 0.4, True, "MEASURED"),
            row("a", 0.0, 30, 0.0005, True, "PINNED_PERIODIC"),
            row("SP", 0.0, 0, 0.0, False, "PINNED_PERIODIC")])
        self.assertEqual(self.load(document), {"z": 0.4})

    def test_unmeasured_row_cannot_carry_a_value(self):
        document = prior_document([row("z", 0.4, 0, 0.0, False, "PINNED_PERIODIC")])
        with self.assertRaisesRegex(ValueError, "measured mean"):
            self.load(document)

    def test_duplicate_symbol_rejected(self):
        document = prior_document([row("z", 0.4, 12, 0.4, True, "MEASURED"),
                                   row("z", 0.8, 9, 0.8, True, "MEASURED")])
        with self.assertRaisesRegex(ValueError, "repeats a symbol"):
            self.load(document)

    def test_unknown_symbol_rejected_even_at_zero(self):
        document = prior_document([row("q", 0.0, 5, 0.0, True, "PINNED_PERIODIC")])
        with self.assertRaisesRegex(ValueError, "outside the vocabulary"):
            self.load(document)

    def test_measurement_flag_must_match_windows(self):
        document = prior_document([row("z", 0.4, 0, 0.4, True, "MEASURED")],
                                  minimumWindows=1)
        with self.assertRaisesRegex(ValueError, "disagrees with its windows"):
            self.load(document)

    def test_periodic_flag_must_match_value(self):
        document = prior_document([row("z", 0.4, 12, 0.4, True, "PINNED_PERIODIC")])
        with self.assertRaisesRegex(ValueError, "periodic flag"):
            self.load(document)

    def test_row_shape_is_closed(self):
        for bad in (row("z", 0.4, 12, 0.4, True, "MEASURED") | {"extra": 1},
                    {k: v for k, v in row("z", 0.4, 12, 0.4, True, "MEASURED").items()
                     if k != "windows"}):
            with self.assertRaisesRegex(ValueError, "row shape"):
                self.load(prior_document([bad]))

    def test_digest_and_schema_enforced(self):
        self.write(prior_document([]))
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            load_breathiness_prior(self.path, "0" * 64, TOKENS)
        with self.assertRaisesRegex(ValueError, "unqualified receipt"):
            self.load(prior_document([], revision=2))
        with self.assertRaisesRegex(ValueError, "window bound"):
            self.load(prior_document([], minimumWindows=0))


class ConfigurationAssetTests(unittest.TestCase):
    def test_schema_four_emitted_only_with_defaults(self):
        plain = json.loads(configuration_asset(SUPPORTED_PROFILE, 48000, "scalar", "audio"))
        self.assertEqual(plain["schemaVersion"], 3)
        self.assertNotIn("conditioningDefaults", plain)
        conditioned = json.loads(configuration_asset(
            SUPPORTED_PROFILE, 48000, "scalar", "audio", {"z": 0.4}))
        self.assertEqual(conditioned["schemaVersion"], 4)
        self.assertEqual(conditioned["conditioningDefaults"], {"breathiness": {"z": 0.4}})


class PrepareTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)

    def tearDown(self):
        self.directory.cleanup()

    def export_directory(self, name, format_id, path_field, sha_field, bytes_field,
                         graph, extra):
        directory = self.root / name
        directory.mkdir()
        graph_name = path_field.split(".")[0].replace("Path", "") + ".onnx"
        (directory / graph_name).write_bytes(graph)
        profile = dict(SUPPORTED_PROFILE)
        report = dict(formatId=format_id, profile=profile,
                      profileSha256=sha256(canonical_json(profile, compact=True)),
                      checkpointReceiptSha256="0" * 64, releaseEligible=False,
                      singerQualified=False)
        report[path_field] = graph_name
        report[sha_field] = sha256(graph)
        report[bytes_field] = len(graph)
        report.update(extra)
        (directory / "export.json").write_bytes(json.dumps(report).encode())
        return directory

    def conditioned_acoustic(self, graph):
        return self.export_directory(
            "acoustic", "com.project-seam.acoustic-export",
            "acousticPath", "acousticSha256", "acousticBytes", graph,
            dict(schemaVersion=2, conditioningRevision=2, runtimeSmokePassed=True,
                 vocabulary=TOKENS, conditioningControls=[BREATHINESS_CONTROL],
                 encoderRuntimeCheck=dict(breathinessConditionEffectPassed=True,
                                          maximumBreathinessConditionEffect=0.5),
                 deploymentBridgeCheck=dict(breathinessConditionEffectPassed=True,
                                            maximumBreathinessMelEffect=0.25),
                 inspection=dict(inputs=[dict(name="breathiness", dtype=1,
                                              shape=[1, "n_frames"])])))

    def vocoder_export(self, graph):
        return self.export_directory(
            "vocoder", "com.project-seam.vocoder-export",
            "vocoderPath", "vocoderSha256", "vocoderBytes", graph, dict())

    def test_conditioned_bundle_carries_schema_four_defaults(self):
        from check_paired_runtime import graphs
        acoustic_graph, vocoder_graph = graphs(conditioned=True)
        acoustic = self.conditioned_acoustic(acoustic_graph)
        vocoder = self.vocoder_export(vocoder_graph)
        prior_path = self.root / "prior.json"
        prior = prior_document([row("z", 0.4, 12, 0.4, True, "MEASURED")])
        payload = json.dumps(prior, sort_keys=True).encode()
        prior_path.write_bytes(payload)
        output = self.root / "bundle"
        report = prepare(acoustic, vocoder, output, 48000, "scalar", "audio",
                         breathiness_prior=prior_path,
                         breathiness_prior_sha256=sha256(payload))
        self.assertEqual(report["breathinessDefaults"], {"z": 0.4})
        configuration = json.loads((output / "configuration").read_bytes())
        self.assertEqual(configuration["schemaVersion"], 4)
        self.assertEqual(configuration["conditioningDefaults"],
                         {"breathiness": {"z": 0.4}})
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "neural_runtime"))
        from inspect_bundle import inspect_bundle
        manifest = (output / "manifest.json").read_bytes()
        assets = {name: (output / name).read_bytes()
                  for name in ("acoustic", "vocoder", "vocabulary", "configuration")}
        inspected = inspect_bundle(manifest, assets, sha256(manifest))
        self.assertEqual(inspected["status"], "OFFLINE_BUNDLE_INSPECTED")
        self.assertEqual(inspected["pair"]["contract"]["conditioningControls"],
                         ["breathiness"])

    def test_prior_refused_for_unconditioned_export(self):
        from check_paired_runtime import graphs
        acoustic_graph, vocoder_graph = graphs()
        acoustic = self.export_directory(
            "acoustic", "com.project-seam.acoustic-export",
            "acousticPath", "acousticSha256", "acousticBytes", acoustic_graph,
            dict(schemaVersion=1, runtimeSmokePassed=True, vocabulary=TOKENS,
                 conditioningControls=[]))
        vocoder = self.vocoder_export(vocoder_graph)
        prior_path = self.root / "prior.json"
        payload = json.dumps(prior_document([]), sort_keys=True).encode()
        prior_path.write_bytes(payload)
        with self.assertRaisesRegex(ValueError, "conditioned acoustic export"):
            prepare(acoustic, vocoder, self.root / "bundle", 48000, "scalar",
                    "audio", breathiness_prior=prior_path,
                    breathiness_prior_sha256=sha256(payload))


if __name__ == "__main__":
    unittest.main()
