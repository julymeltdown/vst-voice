import hashlib
from pathlib import Path
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
            self.assertEqual(len(result), 13)
            self.assertEqual(result["rights_anchor"], "a" * 64)
            self.assertEqual(result["label_anchor"], "b" * 64)
            self.assertEqual(result["permission_config"], root.resolve() / "permissionConfig.json")
            self.assertEqual({path.name: path.read_bytes() for path in root.iterdir()}, before)
            for name in references:
                path = root / f"{name}.json"
                path.write_bytes(b"{}")
                with self.assertRaises(ValueError): load()
                path.write_bytes(before[path.name])


if __name__ == "__main__":
    unittest.main()
