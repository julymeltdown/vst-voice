import hashlib
import json
import unittest

from check_paired_runtime import graphs
from inspect_bundle import inspect_bundle, parse
from convert_vocabulary import convert_vocabulary


def encode(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


class BundleTests(unittest.TestCase):
    def setUp(self):
        acoustic, vocoder = graphs()
        feature = dict(sampleRate=48000, hopSize=256, bins=80, layout="BTF",
                       amplitudeScale="ln-amplitude", multiplier=1.0, offset=0.0,
                       minimumHz=40.0, maximumHz=16000.0)
        self.configuration = dict(formatId="com.project-seam.neural-bundle-configuration",
                                  schemaVersion=1, maximumFrames=48000,
                                  acousticFeatures=feature, vocoderFeatures=dict(feature))
        self.assets = dict(acoustic=acoustic, vocoder=vocoder, configuration=encode(self.configuration),
                          vocabulary=encode(dict(formatId="com.project-seam.neural-vocabulary",
                                                 schemaVersion=1, tokens=["<PAD>", "SP", "a"])))

    def manifest(self):
        payload = encode(dict(formatId="com.project-seam.neural-data-bundle", schemaVersion=1,
                              assets=[dict(role=name, name=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
                                      for name, data in sorted(self.assets.items())]))
        return payload, hashlib.sha256(payload).hexdigest()

    def test_bound_success(self):
        manifest, digest = self.manifest()
        report = inspect_bundle(manifest, self.assets, digest)
        self.assertEqual(report["bundleHash"], digest)
        self.assertEqual(report["pair"]["contract"]["bins"], 80)
        self.assertFalse(report["executionAdmitted"])

    def test_changed_bytes(self):
        manifest, digest = self.manifest()
        self.assets["vocoder"] += b"changed"
        with self.assertRaisesRegex(ValueError, "differ"):
            inspect_bundle(manifest, self.assets, digest)

    def test_wrong_manifest_digest(self):
        manifest, _ = self.manifest()
        with self.assertRaisesRegex(ValueError, "digest"):
            inspect_bundle(manifest, self.assets, "0" * 64)

    def test_rehashed_configuration_graph_mismatch(self):
        for role in ("acousticFeatures", "vocoderFeatures"):
            self.configuration[role]["bins"] = 81
        self.assets["configuration"] = encode(self.configuration)
        manifest, digest = self.manifest()
        with self.assertRaisesRegex(ValueError, "Mel layout"):
            inspect_bundle(manifest, self.assets, digest)

    def test_unlisted_asset(self):
        manifest, digest = self.manifest()
        self.assets["extra"] = b"extra"
        with self.assertRaisesRegex(ValueError, "closure"):
            inspect_bundle(manifest, self.assets, digest)

    def test_duplicate_json(self):
        self.assets["configuration"] = b'{"schemaVersion":1,"schemaVersion":1}'
        manifest, digest = self.manifest()
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            inspect_bundle(manifest, self.assets, digest)

    def test_vocabulary_token_limits_match_native_decoder(self):
        for token in ("a" * 129, "phone\n", "phone\x7f", "\x00", "가" * 43):
            self.assets["vocabulary"] = encode(dict(formatId="com.project-seam.neural-vocabulary",
                                                    schemaVersion=1, tokens=["<PAD>", token]))
            manifest, digest = self.manifest()
            with self.assertRaises(ValueError):
                inspect_bundle(manifest, self.assets, digest)

    def test_large_vocabulary_is_not_confused_with_phone_span_limit(self):
        self.assets["vocabulary"] = encode(dict(formatId="com.project-seam.neural-vocabulary",
                                                schemaVersion=1, tokens=["<PAD>"] + [f"p{i}" for i in range(4096)]))
        manifest, digest = self.manifest()
        self.assertEqual(inspect_bundle(manifest, self.assets, digest)["status"], "OFFLINE_BUNDLE_INSPECTED")

    def test_utf8_token_byte_boundary(self):
        self.assets["vocabulary"] = encode(dict(formatId="com.project-seam.neural-vocabulary",
                                                schemaVersion=1, tokens=["<PAD>", "가" * 42 + "ab"]))
        manifest, digest = self.manifest()
        self.assertEqual(inspect_bundle(manifest, self.assets, digest)["status"], "OFFLINE_BUNDLE_INSPECTED")

    def test_depth_rejected_before_recursive_decoder(self):
        with self.assertRaisesRegex(ValueError, "nesting depth"):
            parse(b"[" * 4000 + b"0" + b"]" * 4000, 10000)

    def test_braces_and_escaped_quotes_inside_strings(self):
        value = {"text": '[{\\"quoted\\"}]'}
        self.assertEqual(parse(encode(value), 10000), value)

    def test_numeric_overflow(self):
        with self.assertRaisesRegex(ValueError, "Nonfinite"):
            parse(b'{"value":1e9999}', 10000)

    def test_collection_and_node_budgets(self):
        with self.assertRaisesRegex(ValueError, "collection"):
            parse(b'[1,2,3]', 10000, maximum_entries=2)
        with self.assertRaisesRegex(ValueError, "node"):
            parse(b'[1,2,3]', 10000, maximum_nodes=3)

    def test_version_two_binds_spectral_convention_and_steps(self):
        self.configuration.update(schemaVersion=2, stepsLayout="vector1")
        for role in ("acousticFeatures", "vocoderFeatures"):
            self.configuration[role].update(fftSize=2048, windowSize=1024, melFrequencyScale="slaney")
        self.assets["acoustic"], self.assets["vocoder"] = graphs(steps_layout="vector1")
        self.assets["configuration"] = encode(self.configuration)
        manifest, digest = self.manifest()
        report = inspect_bundle(manifest, self.assets, digest)
        self.assertEqual(report["pair"]["contract"]["stepsLayout"], "vector1")
        self.configuration["vocoderFeatures"]["melFrequencyScale"] = "htk"
        self.assets["configuration"] = encode(self.configuration)
        manifest, digest = self.manifest()
        with self.assertRaisesRegex(ValueError, "disagree"):
            inspect_bundle(manifest, self.assets, digest)

    def test_version_two_rejects_matching_invalid_windows(self):
        self.configuration.update(schemaVersion=2, stepsLayout="scalar")
        for role in ("acousticFeatures", "vocoderFeatures"):
            self.configuration[role].update(fftSize=1024, windowSize=2048, melFrequencyScale="slaney")
        self.assets["configuration"] = encode(self.configuration)
        manifest, digest = self.manifest()
        with self.assertRaisesRegex(ValueError, "spectral"):
            inspect_bundle(manifest, self.assets, digest)

    def test_version_three_binds_actual_vocoder_output(self):
        self.configuration.update(schemaVersion=3, stepsLayout="vector1", vocoderOutput="waveform")
        for role in ("acousticFeatures", "vocoderFeatures"):
            self.configuration[role].update(fftSize=2048, windowSize=1024, melFrequencyScale="slaney")
        self.assets["acoustic"], self.assets["vocoder"] = graphs(steps_layout="vector1", vocoder_output="waveform")
        self.assets["configuration"] = encode(self.configuration)
        manifest, digest = self.manifest()
        report = inspect_bundle(manifest, self.assets, digest)
        self.assertEqual(report["pair"]["contract"]["vocoderOutput"], "waveform")
        self.configuration["vocoderOutput"] = "audio"
        self.assets["configuration"] = encode(self.configuration)
        manifest, digest = self.manifest()
        with self.assertRaisesRegex(ValueError, "Unexpected outputs"):
            inspect_bundle(manifest, self.assets, digest)

    def test_vocabulary_aliases_preserve_shared_id(self):
        vocabulary = dict(formatId="com.project-seam.neural-vocabulary", schemaVersion=2,
                          tokens=["<PAD>", "SP", "ja/a"], aliases={"en/aa": 2, "ko/a": 2})
        self.assets["vocabulary"] = encode(vocabulary)
        manifest, digest = self.manifest()
        self.assertEqual(inspect_bundle(manifest, self.assets, digest)["status"], "OFFLINE_BUNDLE_INSPECTED")
        for alias, index in (("bad", 0), ("bad", 3), ("ja/a", 2), ("bad", True)):
            vocabulary["aliases"] = {alias: index}
            self.assets["vocabulary"] = encode(vocabulary)
            manifest, digest = self.manifest()
            with self.assertRaisesRegex(ValueError, "alias"):
                inspect_bundle(manifest, self.assets, digest)

    def test_converted_export_vocabulary_enters_hash_bound_bundle(self):
        self.assets["vocabulary"] = convert_vocabulary(encode({"SP": 1, "ja/a": 2, "ko/a": 2}))
        manifest, digest = self.manifest()
        report = inspect_bundle(manifest, self.assets, digest)
        self.assertEqual(report["vocabularyHash"], hashlib.sha256(self.assets["vocabulary"]).hexdigest())


if __name__ == "__main__":
    unittest.main()
