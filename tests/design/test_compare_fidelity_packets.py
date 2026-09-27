"""A repeatability gate must inspect real pixels and reject missing or failed evidence."""
import copy
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("compare_fidelity_packets", ROOT / "scripts/compare_fidelity_packets.py")
COMPARE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COMPARE)


class PixelEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        image = Image.new("RGBA", (4, 3), (20, 40, 60, 255))
        image.save(self.root / "frame.png")
        self.frame = dict(id="frame", exitCode=0, stateReached=True,
                          softwarePng="frame.png", softwarePixels=[4, 3],
                          softwarePixelSha256=hashlib.sha256(image.tobytes()).hexdigest(),
                          **{check: {"result": "PASS"} for check in
                             ("geometryCheck", "semanticCheck", "imageCheck")})

    def test_verifies_actual_pixels(self):
        manifest = {"result": "PASS", "captures": [self.frame]}
        self.assertEqual(len(COMPARE.frames(manifest, self.root)), 1)
        Image.new("RGB", (4, 3), "red").save(self.root / "frame.png")
        with self.assertRaisesRegex(ValueError, "declared hash"):
            COMPARE.frames(manifest, self.root)

    def test_missing_partial_duplicate_or_failed_frames_never_pass(self):
        for mutate in (lambda m: m.update(captures=[]), lambda m: m.update(result="FAIL"),
                       lambda m: m["captures"].append(copy.deepcopy(self.frame)),
                       lambda m: m["captures"][0].pop("softwarePixelSha256"),
                       lambda m: m["captures"][0].update(stateReached=False),
                       lambda m: m["captures"][0].update(exitCode=1),
                       lambda m: m["captures"][0].update(softwarePixels=[3, 4]),
                       lambda m: m["captures"][0].update(softwarePng="absent.png"),
                       lambda m: m["captures"][0].update(geometryCheck={"result": "FAIL"}),
                       lambda m: m["captures"][0].pop("semanticCheck"),
                       lambda m: m["captures"][0].update(imageCheck={"result": "FAIL"}),
                       lambda m: m.update(requestedMatrix={"captureCount": 2})):
            manifest = {"result": "PASS", "captures": [copy.deepcopy(self.frame)]}
            mutate(manifest)
            with self.subTest(manifest), self.assertRaises(ValueError):
                COMPARE.frames(manifest, self.root)

    def test_matrix_coverage_is_not_a_self_declared_capture_count(self):
        cases = [dict(mode=m, state=s, viewport=v, contrast=c, requestedDeviceScale=z)
                 for m in ("emo", "scene") for s in ("empty", "dense-overlap", "selection", "rendering", "failed")
                 for v in ([1600, 900], [1100, 720], [860, 640], [720, 480])
                 for c in ("standard", "high") for z in (1, 2)]
        self.assertTrue(COMPARE.plan_cells(cases)["complete"])
        self.assertEqual(COMPARE.plan_cells(cases[:-1])["covered"], 159)
        self.assertFalse(COMPARE.plan_cells(cases[:-1] + [cases[0]])["complete"])

    def test_a_packet_cannot_establish_repetition_against_itself_or_a_copy(self):
        a, b = {"captureRunId": "run-a"}, {"captureRunId": "run-b"}
        self.assertTrue(COMPARE.independent_runs(a, b, self.root / "a", self.root / "b"))
        self.assertFalse(COMPARE.independent_runs(a, b, self.root, self.root))
        self.assertFalse(COMPARE.independent_runs(a, a, self.root / "a", self.root / "b"))
        self.assertFalse(COMPARE.independent_runs({}, b, self.root / "a", self.root / "b"))


if __name__ == "__main__":
    unittest.main()
