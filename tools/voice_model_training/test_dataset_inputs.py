import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

from tools.voice_model_training.__main__ import encode_report, load_dataset_inputs, publish_new


class DatasetInputTests(unittest.TestCase):
    def test_capture_is_read_only_and_every_reference_is_hash_bound(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            references = {}
            for name in ("permissionConfig", "labelConfig", "rightsReview", "rightsPolicy", "labelReview", "labelPolicy"):
                value = {"fixture": name}
                publish_new(root / f"{name}.json", value)
                references[name] = dict(path=f"{name}.json", sha256=hashlib.sha256(encode_report(value)).hexdigest())
            config = dict(formatId="com.project-seam.training-dataset-config", schemaVersion=1,
                          seed="fixture", heldOutSongs=["test"], **references)
            publish_new(root / "dataset.json", config)
            digest = hashlib.sha256(encode_report(config)).hexdigest()
            before = {path.name: path.read_bytes() for path in root.iterdir()}
            def load():
                return load_dataset_inputs(root / "dataset.json", digest, root,
                                           rights_anchor="a" * 64, label_anchor="b" * 64)
            result = load()
            self.assertEqual(len(result), 14)
            self.assertEqual(result["rights_anchor"], "a" * 64)
            self.assertEqual(result["label_anchor"], "b" * 64)
            self.assertEqual(result["permission_config"], root.resolve() / "permissionConfig.json")
            self.assertEqual({path.name: path.read_bytes() for path in root.iterdir()}, before)
            for name in references:
                path = root / f"{name}.json"
                path.write_bytes(b"{}")
                with self.assertRaises(ValueError): load()
                path.write_bytes(before[path.name])

    def test_schema_two_captures_derived_segment_configuration(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            references = {}
            for name in ("permissionConfig", "labelConfig", "rightsReview", "rightsPolicy", "labelReview", "labelPolicy"):
                value = {"fixture": name}
                publish_new(root / f"{name}.json", value)
                references[name] = dict(path=f"{name}.json", sha256=hashlib.sha256(encode_report(value)).hexdigest())
            segment = {"formatId": "com.project-seam.voice-training-segment-config", "schemaVersion": 3}
            publish_new(root / "segment.json", segment)
            segment_hash = hashlib.sha256(encode_report(segment)).hexdigest()
            config = dict(formatId="com.project-seam.training-dataset-config", schemaVersion=2,
                          seed="fixture", heldOutSongs=["test"], **references,
                          derivedSegments=[dict(configuration=dict(path="segment.json", sha256=segment_hash),
                                                artifactDirectory="derived-child")])
            publish_new(root / "dataset.json", config)
            digest = hashlib.sha256(encode_report(config)).hexdigest()
            result = load_dataset_inputs(root / "dataset.json", digest, root,
                                         rights_anchor="a" * 64, label_anchor="b" * 64)
            self.assertEqual(result["derived_segments"], config["derivedSegments"])

    def test_schema_three_binds_fresh_pitch_executable(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            references = {}
            for name in ("permissionConfig", "labelConfig", "rightsReview", "rightsPolicy", "labelReview", "labelPolicy"):
                value = {"fixture": name}
                publish_new(root / f"{name}.json", value)
                references[name] = dict(path=f"{name}.json", sha256=hashlib.sha256(encode_report(value)).hexdigest())
            segment = {"formatId": "com.project-seam.voice-training-segment-config", "schemaVersion": 3}
            publish_new(root / "segment.json", segment)
            segment_hash = hashlib.sha256(encode_report(segment)).hexdigest()
            extractor = root / "pitch-extractor"
            extractor.write_text(f"#!{sys.executable}\nprint('{{}}')\n")
            extractor.chmod(0o700)
            extractor_hash = hashlib.sha256(extractor.read_bytes()).hexdigest()
            config = dict(formatId="com.project-seam.training-dataset-config", schemaVersion=3,
                          seed="fixture", heldOutSongs=["test"], **references,
                          derivedSegments=[dict(configuration=dict(path="segment.json", sha256=segment_hash),
                                                artifactDirectory="derived-child")],
                          freshPitchExtractor=dict(path="pitch-extractor", sha256=extractor_hash))
            publish_new(root / "dataset.json", config)
            digest = hashlib.sha256(encode_report(config)).hexdigest()
            result = load_dataset_inputs(root / "dataset.json", digest, root,
                                         rights_anchor="a" * 64, label_anchor="b" * 64)
            self.assertEqual(result["fresh_pitch_extractor"], extractor.resolve())
            self.assertEqual(result["fresh_pitch_extractor_sha256"], extractor_hash)
            extractor.write_text(extractor.read_text() + "# changed\n")
            with self.assertRaisesRegex(ValueError, "captured digest"):
                load_dataset_inputs(root / "dataset.json", digest, root,
                                    rights_anchor="a" * 64, label_anchor="b" * 64)


if __name__ == "__main__":
    unittest.main()
