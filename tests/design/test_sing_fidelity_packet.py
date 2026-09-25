"""Regression cases for the SING fidelity packet checks (no app launch, no screen capture).

Every check must fail closed: each case removes or falsifies one piece of evidence that a real
packet could lose, and the checker must report FAIL rather than skip it.
"""

import argparse
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "capture_sing_fidelity_packet", ROOT / "scripts/capture_sing_fidelity_packet.py"
)
PACKET = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKET)
CONTRACT = json.loads(PACKET.CONTRACT.read_text(encoding="utf-8"))
CANONICAL = {"viewport": [1600, 900], "mode": "emo"}


def canonical_geometry():
    regions = {item["id"]: list(item["rect"]) for item in CONTRACT["canonical"]["regions"]}
    controls = {name: [100 + 10 * i, 50, 8, 8] for i, name in enumerate(PACKET.ALWAYS_CONTROLS)}
    controls |= {f"knob{i}": [1170 + 140 * (i % 3), 530 + 100 * (i // 3), 56, 56] for i in range(6)}
    controls |= {f"workspaceTab{i}": [264 + 80 * i, 24, 80, 64] for i in range(5)}
    return {
        "logicalSize": [1600, 900], "deviceScale": 2, "mode": "emo", "workspace": "sing",
        "presented": True, "rack": "full", "compactHeader": False,
        "workspaceLabelsVisible": True, "outputMeterVisible": True,
        "regions": regions, "controls": controls,
    }


def published_tree(geometry, notes=6, state="READY"):
    regions, controls = geometry["regions"], geometry["controls"]
    nodes = [{"id": "shell", "parent": "", "bounds": [0, 0, 1600, 900], "value": ""}]

    def add(node_id, bounds, value=""):
        nodes.append({"id": node_id, "parent": "shell", "bounds": list(bounds), "value": value})

    for node_id in PACKET.ALWAYS_NODES:
        add(node_id, regions.get(PACKET.SEMANTIC_REGIONS.get(node_id, "grid"), [80, 172, 10, 10]))
    nodes[[n["id"] for n in nodes].index("shell.status")]["value"] = f"{state}: render note"
    for i, knob in enumerate(PACKET.KNOBS):
        add(f"shell.knob.{knob}", controls[f"knob{i}"])
    add("shell.style", regions["style"])
    for i, name in enumerate(PACKET.WORKSPACES):
        add(f"shell.workspace.{name}", controls[f"workspaceTab{i}"])
    listed = [{"id": f"note.{i}", "parent": "virtual-notes", "bounds": [100, 300, 20, 10]}
              for i in range(min(notes, PACKET.NOTE_LIMIT))]
    return {"nodes": nodes, "virtualizedNoteCount": notes, "notes": listed, "focused": None}


def geometry_result(geometry, expected=CANONICAL):
    return PACKET.check_geometry(geometry, CONTRACT, expected=expected)


def semantic_result(semantic, geometry, notes=6, state="ready"):
    return PACKET.check_semantics(semantic, geometry, expected_notes=notes, render_state=state)


class GeometryCheckTests(unittest.TestCase):
    def test_the_canonical_snapshot_passes(self):
        result = geometry_result(canonical_geometry())
        self.assertEqual(result["result"], "PASS", result["failures"])
        self.assertTrue(result["comparedToCanonical"])

    def test_missing_wrong_or_unrequested_evidence_fails(self):
        cases = {
            "edge beyond tolerance": lambda g: g["regions"]["status"].__setitem__(0, 19),
            "missing region": lambda g: g["regions"].pop("portraitRing"),
            "missing grid": lambda g: g["regions"].pop("grid"),
            "empty region": lambda g: g["regions"]["keyboard"].__setitem__(2, 0),
            "missing knob control": lambda g: g["controls"].pop("knob3"),
            "missing tab control": lambda g: g["controls"].pop("workspaceTab4"),
            "missing play control": lambda g: g["controls"].pop("playButton"),
            "overlap": lambda g: g["regions"]["tools"].__setitem__(3, 40),
            "outside parent": lambda g: g["regions"]["style"].__setitem__(3, 400),
            "time axis": lambda g: g["regions"]["laneTimePlot"].__setitem__(0, 81),
            "other viewport": lambda g: g.__setitem__("logicalSize", [1440, 900]),
            "other mode": lambda g: g.__setitem__("mode", "scene"),
            "other workspace": lambda g: g.__setitem__("workspace", "export"),
            "not presented": lambda g: g.__setitem__("presented", False),
            "unknown scale": lambda g: g.__setitem__("deviceScale", 3),
            "rail at full width": lambda g: g.__setitem__("rack", "rail"),
            "no size": lambda g: g.pop("logicalSize"),
        }
        for name, mutate in cases.items():
            with self.subTest(name):
                geometry = canonical_geometry()
                mutate(geometry)
                self.assertEqual(geometry_result(geometry)["result"], "FAIL")

    def test_within_tolerance_passes(self):
        geometry = canonical_geometry()
        geometry["regions"]["status"][0] += 2
        geometry["regions"]["status"][2] -= 2
        self.assertEqual(geometry_result(geometry)["result"], "PASS")

    def test_compact_presentations_follow_section_3_4(self):
        self.assertEqual([PACKET.spec_rack_presentation(w) for w in (720, 860, 1099, 1100)],
                         ["drawer", "rail", "rail", "full"])
        self.assertEqual([PACKET.spec_rack_width(w) for w in (720, 1000, 1280, 1600)],
                         [44.0, 56.0, 352.0, 440.0])
        # A 56-point rail below 860 points is a failure, not a reported deviation.
        geometry = canonical_geometry()
        geometry["logicalSize"] = [720, 480]
        geometry["rack"] = "rail"
        geometry["regions"]["rack"] = [648, 92, 56, 332]
        result = geometry_result(geometry, {"viewport": [720, 480], "mode": "emo"})
        self.assertEqual(result["result"], "FAIL")
        self.assertTrue(any("drawer" in failure for failure in result["failures"]))


class SemanticCheckTests(unittest.TestCase):
    def test_published_bounds_on_the_layout_pass(self):
        geometry = canonical_geometry()
        result = semantic_result(published_tree(geometry), geometry)
        self.assertEqual(result["result"], "PASS", result["failures"])

    def test_missing_moved_or_inconsistent_nodes_fail(self):
        def drop(prefix):
            return lambda s: s.__setitem__("nodes", [n for n in s["nodes"] if not n["id"].startswith(prefix)])

        def node(s, node_id):
            return next(n for n in s["nodes"] if n["id"] == node_id)

        cases = {
            "all knobs removed": drop("shell.knob."),
            "one knob removed": drop("shell.knob.growl"),
            "workspace tabs removed": drop("shell.workspace."),
            "status removed": drop("shell.status"),
            "style removed": drop("shell.style"),
            "moved status": lambda s: node(s, "shell.status")["bounds"].__setitem__(0, 17),
            "moved knob": lambda s: node(s, "shell.knob.air")["bounds"].__setitem__(1, 600),
            "duplicate": lambda s: s["nodes"].append(copy.deepcopy(s["nodes"][2])),
            "offscreen": lambda s: s["nodes"].append(
                {"id": "x", "parent": "shell", "bounds": [1590, 10, 40, 10], "value": ""}),
            "notes and count removed": lambda s: s.update(notes=[], virtualizedNoteCount=0),
            "notes removed": lambda s: s.update(notes=[]),
            "count wrong": lambda s: s.update(virtualizedNoteCount=5),
            "status disagrees": lambda s: node(s, "shell.status").update(value="FAILED: x"),
        }
        for name, mutate in cases.items():
            with self.subTest(name):
                geometry = canonical_geometry()
                semantic = published_tree(geometry)
                mutate(semantic)
                self.assertEqual(semantic_result(semantic, geometry)["result"], "FAIL")

    def test_rendering_needs_progress_and_long_songs_list_up_to_the_limit(self):
        geometry = canonical_geometry()
        semantic = published_tree(geometry, notes=600, state="RENDERING")
        self.assertEqual(len(semantic["notes"]), PACKET.NOTE_LIMIT)
        result = semantic_result(semantic, geometry, notes=600, state="rendering")
        self.assertEqual(result["result"], "FAIL")  # no shell.render-progress node
        semantic["nodes"].append({"id": "shell.render-progress", "parent": "shell",
                                  "bounds": geometry["regions"]["status"], "value": "40%"})
        result = semantic_result(semantic, geometry, notes=600, state="rendering")
        self.assertEqual(result["result"], "PASS", result["failures"])

    def test_compact_rack_needs_the_inspector_instead_of_knobs(self):
        geometry = canonical_geometry()
        geometry["rack"] = "drawer"
        semantic = published_tree(geometry)
        semantic["nodes"] = [n for n in semantic["nodes"]
                             if not n["id"].startswith(("shell.knob.", "shell.style"))]
        self.assertEqual(semantic_result(semantic, geometry)["result"], "FAIL")
        semantic["nodes"].append({"id": "shell.inspector", "parent": "shell",
                                  "bounds": geometry["regions"]["rack"], "value": ""})
        result = semantic_result(semantic, geometry)
        self.assertEqual(result["result"], "PASS", result["failures"])


class ParityTests(unittest.TestCase):
    def record(self, mode, geometry, state="ready"):
        return {"state": state, "viewport": [1600, 900], "mode": mode, "geometry": geometry}

    def test_identical_geometry_passes(self):
        results = PACKET.mode_parity([self.record("emo", canonical_geometry()),
                                      self.record("scene", canonical_geometry())])
        self.assertEqual([r["result"] for r in results], ["PASS"])

    def test_identity_differences_and_missing_partners_fail(self):
        cases = {
            "logical size": lambda g: g.__setitem__("logicalSize", [1000, 600]),
            "device scale": lambda g: g.__setitem__("deviceScale", 1),
            "rack": lambda g: g.__setitem__("rack", "rail"),
            "rectangle": lambda g: g["regions"]["grid"].__setitem__(0, 81),
            "no rack key": lambda g: g.pop("rack"),
        }
        for name, mutate in cases.items():
            with self.subTest(name):
                other = canonical_geometry()
                mutate(other)
                results = PACKET.mode_parity([self.record("emo", canonical_geometry()),
                                              self.record("scene", other)])
                self.assertEqual([r["result"] for r in results], ["FAIL"])
        alone = PACKET.mode_parity([self.record("emo", canonical_geometry())])
        self.assertEqual([r["result"] for r in alone], ["FAIL"])
        errored = PACKET.mode_parity([self.record("emo", canonical_geometry()),
                                      {"state": "ready", "viewport": [1600, 900], "mode": "scene"}])
        self.assertEqual([r["result"] for r in errored], ["FAIL"])


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += max(seconds, 0.001)


class CaptureDeadlineTests(unittest.TestCase):
    """The capture must end by its one deadline and reap only its own child."""

    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="seam-packet-test-"))
        self.clock = FakeClock()
        self.args = argparse.Namespace(
            app=Path("/nonexistent/app"), voicebank_root=ROOT, close_ms=10000,
            rendering_close_ms=1300, appkit_lead_ms=1200, no_appkit=False, overrun_ms=60000)
        self.deadline = (self.args.close_ms + self.args.overrun_ms) / 1000.0

    def run_capture(self, process, run=None):
        patches = [
            mock.patch.object(PACKET.subprocess, "Popen", return_value=process),
            mock.patch.object(PACKET.time, "monotonic", side_effect=self.clock.monotonic),
            mock.patch.object(PACKET.time, "sleep", side_effect=self.clock.sleep),
        ]
        if run is not None:
            patches.append(mock.patch.object(PACKET.subprocess, "run", side_effect=run))
        with patches[0], patches[1], patches[2], (patches[3] if run else mock.MagicMock()):
            return PACKET.capture(self.args, self.work, "emo", "ready", (1600, 900),
                                  ROOT / "tests/fixtures/ui-fidelity/sing-phrase.seam")

    def hung_process(self):
        process = mock.Mock()
        process.poll.return_value = None
        process.returncode = -9
        return process

    def test_an_app_that_never_opens_a_window_is_killed_at_the_deadline(self):
        process = self.hung_process()
        process.communicate.return_value = ("partial log\n", "")
        record = self.run_capture(process)
        self.assertIn("opened no window", record["error"])
        process.kill.assert_called_once()
        process.communicate.assert_called_once_with(timeout=10.0)
        self.assertLessEqual(self.clock.now, self.deadline + 0.1)

    def test_a_hung_window_capture_and_shutdown_end_at_the_deadline(self):
        process = self.hung_process()
        # The window opens as soon as the app starts.
        window = self.work / "emo-ready-1600x900" / "window-id"
        process.poll.side_effect = lambda: (window.write_text("42\n") if not window.exists() else None) and None
        calls = []

        def hung_run(command, **kwargs):
            calls.append(kwargs["timeout"])
            self.clock.now += kwargs["timeout"]
            raise subprocess.TimeoutExpired(command, kwargs["timeout"])

        def communicate(timeout=None):
            if timeout is not None and timeout != 10.0:
                self.clock.now += timeout
                raise subprocess.TimeoutExpired("app", timeout)
            return ("", "")

        process.communicate.side_effect = communicate
        record = self.run_capture(process, run=hung_run)
        self.assertEqual(record["appkit"], "screencapture timed out at the capture deadline")
        self.assertIn("did not close before the capture deadline", record["error"])
        process.kill.assert_called_once()
        self.assertEqual(len(calls), 1)
        self.assertLessEqual(self.clock.now, self.deadline + 0.2)

    def test_a_killed_app_that_cannot_be_reaped_is_reported(self):
        process = self.hung_process()
        process.communicate.side_effect = subprocess.TimeoutExpired("app", 10.0)
        record = self.run_capture(process)
        self.assertIn("could not be reaped", record["error"])
        process.kill.assert_called_once()


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

