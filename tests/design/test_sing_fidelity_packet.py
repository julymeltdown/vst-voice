"""Regression cases for the SING fidelity packet checks (no app launch, no screen capture)."""

import copy
import importlib.util
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "capture_sing_fidelity_packet", ROOT / "scripts/capture_sing_fidelity_packet.py"
)
PACKET = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKET)


def canonical_geometry(contract):
    regions = {item["id"]: list(item["rect"]) for item in contract["canonical"]["regions"]}
    return {
        "logicalSize": list(contract["canonical"]["logicalSize"]),
        "rack": "full",
        "regions": regions,
        "controls": {"knob0": [1170, 530, 56, 56]},
    }


class GeometryCheckTests(unittest.TestCase):
    def setUp(self):
        self.contract = json.loads(PACKET.CONTRACT.read_text(encoding="utf-8"))

    def test_the_canonical_regions_pass(self):
        result = PACKET.check_geometry(canonical_geometry(self.contract), self.contract)
        self.assertEqual(result["result"], "PASS", result["failures"])
        self.assertTrue(result["comparedToCanonical"])
        self.assertTrue(result["rackWidthMatchesSpec"])

    def test_drift_overlap_containment_and_time_axis_fail(self):
        cases = {
            "edge beyond tolerance": lambda g: g["regions"]["status"].__setitem__(0, 19),
            "missing region": lambda g: g["regions"].pop("portraitRing"),
            "overlap": lambda g: g["regions"]["tools"].__setitem__(3, 40),
            "outside parent": lambda g: g["regions"]["style"].__setitem__(3, 400),
            "time axis": lambda g: g["regions"]["laneTimePlot"].__setitem__(0, 81),
        }
        for name, mutate in cases.items():
            with self.subTest(name):
                geometry = copy.deepcopy(canonical_geometry(self.contract))
                mutate(geometry)
                self.assertEqual(PACKET.check_geometry(geometry, self.contract)["result"], "FAIL")

    def test_within_tolerance_passes(self):
        geometry = canonical_geometry(self.contract)
        geometry["regions"]["status"][0] += 2
        geometry["regions"]["status"][2] -= 2
        self.assertEqual(PACKET.check_geometry(geometry, self.contract)["result"], "PASS")

    def test_responsive_rule_is_reported_not_failed(self):
        self.assertEqual(PACKET.spec_rack_width(720), 44.0)
        self.assertEqual(PACKET.spec_rack_width(1000), 56.0)
        self.assertEqual(PACKET.spec_rack_width(1280), 352.0)
        self.assertEqual(PACKET.spec_rack_width(1600), 440.0)


class SemanticCheckTests(unittest.TestCase):
    def geometry_and_tree(self):
        contract = json.loads(PACKET.CONTRACT.read_text(encoding="utf-8"))
        geometry = canonical_geometry(contract)
        regions = geometry["regions"]
        nodes = [{"id": "shell", "parent": "", "bounds": [0, 0, 1600, 900]}]
        for node_id, region in PACKET.SEMANTIC_REGIONS.items():
            nodes.append({"id": node_id, "parent": "shell", "bounds": list(regions[region])})
        nodes.append({"id": "shell.knob.formant", "parent": "shell", "bounds": [1170, 530, 56, 56]})
        semantic = {"nodes": nodes, "virtualizedNoteCount": 6, "focused": None}
        return geometry, semantic

    def test_published_bounds_on_the_layout_pass(self):
        geometry, semantic = self.geometry_and_tree()
        result = PACKET.check_semantics(semantic, geometry)
        self.assertEqual(result["result"], "PASS", result["failures"])

    def test_moved_missing_duplicate_or_offscreen_nodes_fail(self):
        cases = {
            "moved": lambda s: s["nodes"][1]["bounds"].__setitem__(0, 17),
            "missing": lambda s: s["nodes"].pop(1),
            "duplicate": lambda s: s["nodes"].append(copy.deepcopy(s["nodes"][2])),
            "offscreen": lambda s: s["nodes"].append(
                {"id": "x", "parent": "shell", "bounds": [1590, 10, 40, 10]}),
            "knob": lambda s: s["nodes"][-1]["bounds"].__setitem__(1, 600),
        }
        for name, mutate in cases.items():
            with self.subTest(name):
                geometry, semantic = self.geometry_and_tree()
                mutate(semantic)
                self.assertEqual(PACKET.check_semantics(semantic, geometry)["result"], "FAIL")


class FixtureTests(unittest.TestCase):
    def setUp(self):
        self.base = json.loads(PACKET.DEFAULT_FIXTURE.read_text(encoding="utf-8"))

    def notes(self, project):
        return PACKET.region_of(project)["notes"]

    def test_states_derive_from_the_committed_phrase(self):
        self.assertEqual(len(self.notes(self.base)), 6)
        self.assertEqual(self.notes(PACKET.derive_fixture(self.base, "empty")), [])
        self.assertEqual(len(self.notes(PACKET.derive_fixture(self.base, "rendering"))), 600)
        failed = PACKET.region_of(PACKET.derive_fixture(self.base, "failed"))
        self.assertIn("\u3089", [lyric["surface"] for lyric in failed["lyrics"]])
        self.assertEqual(self.notes(PACKET.derive_fixture(self.base, "ready")), self.notes(self.base))

    def test_every_derived_note_has_its_own_ids_and_lyric(self):
        for state in ("rendering", "dense-overlap"):
            with self.subTest(state):
                region = PACKET.region_of(PACKET.derive_fixture(self.base, state))
                note_ids = [note["id"] for note in region["notes"]]
                lyric_ids = {lyric["id"] for lyric in region["lyrics"]}
                self.assertEqual(len(note_ids), len(set(note_ids)))
                self.assertTrue(all(note["lyricId"] in lyric_ids for note in region["notes"]))
                end = max(n["startTick"] + n["durationTick"] for n in region["notes"])
                self.assertLessEqual(end, region["durationTick"])

    def test_dense_overlap_really_overlaps(self):
        notes = self.notes(PACKET.derive_fixture(self.base, "dense-overlap"))
        spans = sorted((n["startTick"], n["startTick"] + n["durationTick"]) for n in notes)
        overlapping = sum(1 for a, b in zip(spans, spans[1:]) if b[0] < a[1])
        self.assertGreaterEqual(overlapping, 3)


if __name__ == "__main__":
    unittest.main()
