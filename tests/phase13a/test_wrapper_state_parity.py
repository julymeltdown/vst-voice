import json
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))

import distribution_manifest  # noqa: E402
import wrapper_state  # noqa: E402


class WrapperStateParityTests(unittest.TestCase):
    def test_projected_state_is_opaque_but_bound_to_canonical_state(self):
        canonical = {"parameters": {"gain": 0.5}, "noteExpression": {"pitch": True}, "audioBuses": {"main": 2}, "gui": {"width": 900}, "transport": {"tempo": 120}, "bankIdentity": {"id": "beta.voicebank"}, "engine": {"voices": 32}}
        projected = wrapper_state.project_state(canonical, "VST3")
        self.assertEqual([], wrapper_state.validate_projected_state(projected, canonical, "VST3"))
        projected["state"]["parameters"]["gain"] = 0.4
        self.assertTrue(wrapper_state.validate_projected_state(projected, canonical, "VST3"))

    def test_manifest_keeps_canonical_identity_and_file_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bundle = root / "ProjectSEAMEditor.vst3"
            binary = bundle / "Contents" / "x86_64-linux" / "ProjectSEAMEditor.so"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"ELF canonical projection")
            manifest = distribution_manifest.build_wrapper_manifest("VST3", "linux", "x86_64", "0.14.0", "com.project-seam.editor.vst3", "c" * 64, bundle)
            (bundle / "wrapper-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
            self.assertEqual([], distribution_manifest.validate_wrapper_bundle("vst3", bundle, "linux", "c" * 64))
            binary.write_bytes(b"changed")
            self.assertTrue(distribution_manifest.validate_wrapper_bundle("vst3", bundle, "linux", "c" * 64))


if __name__ == "__main__":
    unittest.main()
