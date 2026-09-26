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
    controls["singerChange"] = [1468, 438, 100, 22]
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
    add("shell.change-voice", controls["singerChange"])
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
    def test_a_covering_workspace_publishes_its_own_nodes_and_no_score(self):
        def body_tree(geometry, workspace):
            semantic = published_tree(geometry)
            semantic["nodes"] = [n for n in semantic["nodes"]
                                 if not n["id"].startswith("shell.lane") and n["id"] != "shell.waveform"]
            editor = geometry["regions"]["editor"]
            ids = [f"shell.{workspace}.panel"]
            if workspace == "tune":
                ids += ["shell.tune.graph"] + [f"shell.tune.knob.{k}" for k in PACKET.KNOBS]
            elif workspace == "voice":
                ids += ["shell.voice.envelope"] + [f"shell.voice.{m}" for m in PACKET.VOICE_MODULES]
            else:
                ids += ["shell.mix.master", "shell.mix.audio-settings", "shell.mix.track.1a"]
                ids += [f"shell.mix.track.1a.{c}" for c in PACKET.MIX_STRIP_CONTROLS]
            for i, node_id in enumerate(ids):
                semantic["nodes"].append({"id": node_id, "parent": "shell", "value": "",
                                          "bounds": [editor[0] + 8 + 20 * i, editor[1] + 8, 16, 16]})
            semantic.update(notes=[], virtualizedNoteCount=0)
            return semantic

        for workspace in PACKET.WORKSPACE_STATES:
            with self.subTest(workspace):
                geometry = canonical_geometry()
                geometry["workspace"] = workspace
                result = PACKET.check_semantics(body_tree(geometry, workspace), geometry,
                                                expected_notes=6, render_state="ready",
                                                workspace=workspace, expected_tracks=1)
                self.assertEqual(result["result"], "PASS", result["failures"])
                self.assertEqual(geometry_result(geometry, {**CANONICAL, "workspace": workspace})["result"],
                                 "PASS")
                self.assertEqual(geometry_result(geometry)["result"], "FAIL")

        def extra(node_id):
            return lambda s: s["nodes"].append(
                {"id": node_id, "parent": "shell", "bounds": [80, 200, 10, 10], "value": ""})

        def drop(prefix):
            return lambda s: s.__setitem__("nodes", [n for n in s["nodes"] if not n["id"].startswith(prefix)])

        cases = {
            ("tune", "no body node"): drop("shell.tune."),
            ("tune", "only one knob"): lambda s: s.__setitem__("nodes", [
                n for n in s["nodes"] if not n["id"].startswith("shell.tune.")
                or n["id"] == "shell.tune.knob.formant"]),
            ("tune", "graph missing"): drop("shell.tune.graph"),
            ("tune", "body node outside its area"): lambda s: s["nodes"][-1].update(bounds=[1300, 20, 40, 20]),
            ("tune", "empty bounds"): lambda s: s["nodes"][-1].update(bounds=[0, 0, 0, 0]),
            ("tune", "lane still published"): extra("shell.lane"),
            ("tune", "vibrato handle still published"): extra("editor.vibrato.handle.1"),
            ("tune", "overlap group still published"): extra("overlap-group.1"),
            ("tune", "notes still listed"): lambda s: s.update(virtualizedNoteCount=6),
            ("voice", "no body node"): drop("shell.voice."),
            ("voice", "envelope editor missing"): drop("shell.voice.envelope"),
            ("voice", "a module missing"): drop("shell.voice.noise"),
            ("voice", "tune node under voice"): extra("shell.tune.graph"),
            ("voice", "lane still published"): extra("shell.lane"),
            ("voice", "body node outside its area"): lambda s: s["nodes"][-1].update(bounds=[1300, 20, 40, 20]),
            ("tune", "voice node under tune"): extra("shell.voice.source"),
            ("mix", "other workspace's node"): extra("shell.tune.graph"),
            ("mix", "export node"): extra("shell.export.run"),
            ("mix", "no strip"): drop("shell.mix.track."),
            ("mix", "strip without a fader"): drop("shell.mix.track.1a.gain"),
            ("mix", "more strips than tracks"): extra("shell.mix.track.2b"),
            ("mix", "master missing"): drop("shell.mix.master"),
        }
        for (workspace, name), mutate in cases.items():
            with self.subTest(name):
                geometry = canonical_geometry()
                geometry["workspace"] = workspace
                semantic = body_tree(geometry, workspace)
                mutate(semantic)
                result = PACKET.check_semantics(semantic, geometry, expected_notes=6,
                                                render_state="ready", workspace=workspace,
                                                expected_tracks=1)
                self.assertEqual(result["result"], "FAIL")

        # A compact VOICE body shows one module behind tabs: a module's tab stands for it.
        geometry = canonical_geometry()
        geometry["workspace"] = "voice"
        semantic = body_tree(geometry, "voice")
        semantic["nodes"] = [n for n in semantic["nodes"]
                             if n["id"] not in ("shell.voice.source", "shell.voice.noise")]
        editor = geometry["regions"]["editor"]
        for i, module in enumerate(("source", "noise")):
            semantic["nodes"].append({"id": f"shell.voice.view.{module}", "parent": "shell", "value": "",
                                      "bounds": [editor[0] + 8 + 20 * i, editor[1] + 40, 16, 16]})
        result = PACKET.check_semantics(semantic, geometry, expected_notes=6, render_state="ready",
                                        workspace="voice", expected_tracks=1)
        self.assertEqual(result["result"], "PASS", result["failures"])

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
            "all notes clipped out": lambda s: [n.update(bounds=[100, 300, 0, 0]) for n in s["notes"]],
            "count wrong": lambda s: s.update(virtualizedNoteCount=5),
            "status disagrees": lambda s: node(s, "shell.status").update(value="FAILED: x"),
            "frame later than the log": lambda s: node(s, "shell.status").update(value="READY: x"),
        }
        for name, mutate in cases.items():
            with self.subTest(name):
                geometry = canonical_geometry()
                semantic = published_tree(geometry)
                if name == "frame later than the log":
                    self.assertEqual(semantic_result(semantic, geometry, state="rendering")["result"], "FAIL")
                    continue
                mutate(semantic)
                self.assertEqual(semantic_result(semantic, geometry)["result"], "FAIL")

    def test_a_render_that_finishes_after_the_frame_is_a_legal_progression(self):
        geometry = canonical_geometry()
        semantic = published_tree(geometry, state="RENDERING")
        semantic["nodes"].append({"id": "shell.render-progress", "parent": "shell",
                                  "bounds": geometry["regions"]["status"], "value": "90%"})
        for logged in ("rendering", "ready", "failed", "cancelled"):
            with self.subTest(logged):
                result = semantic_result(semantic, geometry, state=logged)
                self.assertEqual(result["result"], "PASS", result["failures"])
                self.assertEqual(result["frameRenderState"], "rendering")
        # A frame reporting READY while the app logged RENDERING later is impossible.
        ready = published_tree(geometry, state="READY")
        self.assertEqual(semantic_result(ready, geometry, state="rendering")["result"], "FAIL")

    def test_progress_is_required_by_the_frame_whatever_the_exit_state(self):
        # Developer 2's I2: a RENDERING frame without its progress node fails for every legal
        # exit state, not only when the app also logged rendering.
        geometry = canonical_geometry()
        semantic = published_tree(geometry, state="RENDERING")
        for logged in ("rendering", "ready", "failed", "cancelled"):
            with self.subTest(logged):
                result = semantic_result(semantic, geometry, state=logged)
                self.assertEqual(result["result"], "FAIL")
                self.assertIn("shell.render-progress: not published", result["failures"])

    def modal_tree(self, geometry):
        regions, controls = geometry["regions"], geometry["controls"]
        nodes = [{"id": "shell", "parent": "", "bounds": [0, 0, 720, 480], "value": ""},
                 {"id": "shell.inspector", "parent": "shell", "bounds": controls["inspectorButton"], "value": "Open"},
                 {"id": "voice.identity", "parent": "shell", "bounds": regions["singer"], "value": ""},
                 {"id": "shell.change-voice", "parent": "shell", "bounds": controls["singerChange"], "value": ""},
                 {"id": "shell.style", "parent": "shell", "bounds": regions["style"], "value": ""},
                 {"id": "shell.status", "parent": "shell", "bounds": regions["status"], "value": "READY: done"}]
        nodes += [{"id": f"shell.knob.{knob}", "parent": "shell", "bounds": controls[f"knob{i}"], "value": ""}
                  for i, knob in enumerate(PACKET.KNOBS)]
        return {"nodes": nodes, "virtualizedNoteCount": 0, "notes": [], "focused": "shell.inspector"}

    def test_the_open_inspector_publishes_only_itself_and_the_status(self):
        geometry = compact_geometry(True)
        result = semantic_result(self.modal_tree(geometry), geometry)
        self.assertEqual(result["result"], "PASS", result["failures"])
        cases = {
            "score notes published": lambda s: s.update(virtualizedNoteCount=6, notes=[{"id": "n"}] * 6),
            "timeline published": lambda s: s["nodes"].append(
                {"id": "timeline", "parent": "shell", "bounds": [80, 156, 540, 144], "value": ""}),
            "lane tab published": lambda s: s["nodes"].append(
                {"id": "shell.lane-tab.air", "parent": "shell", "bounds": [24, 328, 60, 28], "value": ""}),
            "header control published": lambda s: s["nodes"].append(
                {"id": "shell.settings", "parent": "shell", "bounds": [648, 32, 32, 32], "value": ""}),
            "knob missing": lambda s: s.update(nodes=[n for n in s["nodes"] if n["id"] != "shell.knob.air"]),
            "button missing": lambda s: s.update(nodes=[n for n in s["nodes"] if n["id"] != "shell.inspector"]),
        }
        for name, mutate in cases.items():
            with self.subTest(name):
                semantic = self.modal_tree(geometry)
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
        geometry["controls"]["inspectorButton"] = [1540, 108, 44, 44]
        semantic = published_tree(geometry)
        # Closed: knobs, Change voice and style are not on screen, so publishing them fails.
        self.assertEqual(semantic_result(semantic, geometry)["result"], "FAIL")
        semantic["nodes"] = [n for n in semantic["nodes"]
                             if not n["id"].startswith(("shell.knob.", "shell.style", "shell.change-voice"))]
        self.assertEqual(semantic_result(semantic, geometry)["result"], "FAIL")
        semantic["nodes"].append({"id": "shell.inspector", "parent": "shell",
                                  "bounds": [1540, 108, 44, 44], "value": "Closed"})
        result = semantic_result(semantic, geometry)
        self.assertEqual(result["result"], "PASS", result["failures"])
        # Open: they must be published again.
        geometry["inspectorOpen"] = True
        self.assertEqual(semantic_result(semantic, geometry)["result"], "FAIL")


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




def compact_geometry(open_inspector):
    """A 720x480 drawer snapshot shaped like the solver's output."""
    g = canonical_geometry()
    g.update(logicalSize=[720, 480], rack="drawer", inspectorOpen=open_inspector)
    r = g["regions"]
    r.update(header=[16, 16, 688, 64], wordmark=[32, 24, 72, 48], modeSwitch=[200, 34, 120, 28],
             transport=[336, 26, 248, 44], settings=[648, 32, 32, 32], editor=[16, 92, 612, 216],
             tools=[24, 100, 596, 28], ruler=[80, 132, 540, 24], keyboard=[24, 156, 56, 144],
             grid=[80, 156, 540, 144], lane=[16, 320, 612, 104], laneTabs=[24, 328, 596, 28],
             lanePlot=[56, 364, 564, 52], laneTimePlot=[80, 364, 540, 52], rack=[660, 92, 44, 332],
             portraitRing=[660, 92, 44, 44], status=[16, 436, 688, 28])
    for name in ("workspaceTabs", "outputMeter", "singer", "expression", "style"):
        r[name] = [0, 0, 0, 0]
    c = g["controls"]
    for name in ["singerChange"] + [f"knob{i}" for i in range(6)] + [f"workspaceTab{i}" for i in range(5)]:
        c[name] = [0, 0, 0, 0]
    c["inspectorButton"] = [660, 92, 44, 44]
    c["workspaceMenuButton"] = [128, 32, 60, 32]
    if open_inspector:
        r.update(inspector=[312, 92, 340, 316], singer=[324, 104, 316, 56],
                 portraitRing=[324, 110, 44, 44], style=[380, 138, 260, 18],
                 expression=[324, 172, 316, 224])
        c["singerChange"] = [540, 110, 100, 22]
        for i in range(6):
            c[f"knob{i}"] = [324 + 105 * (i % 3), 216 + 90 * (i // 3), 105, 90]
    return g


class CompactInspectorGeometryTests(unittest.TestCase):
    EXPECTED = {"viewport": [720, 480], "mode": "emo"}

    def test_closed_and_open_drawer_pass_when_requested(self):
        closed = geometry_result(compact_geometry(False), self.EXPECTED)
        self.assertEqual(closed["result"], "PASS", closed["failures"])
        opened = geometry_result(compact_geometry(True), self.EXPECTED | {"inspectorOpen": True})
        self.assertEqual(opened["result"], "PASS", opened["failures"])

    def test_inspector_state_and_contents_are_enforced(self):
        # Open when closed was requested, and the reverse.
        self.assertEqual(geometry_result(compact_geometry(True), self.EXPECTED)["result"], "FAIL")
        self.assertEqual(geometry_result(compact_geometry(False),
                                         self.EXPECTED | {"inspectorOpen": True})["result"], "FAIL")
        cases = {
            "no button": lambda g: g["controls"].pop("inspectorButton"),
            "knob outside": lambda g: g["controls"]["knob5"].__setitem__(0, 100),
            "missing knob": lambda g: g["controls"].pop("knob2"),
            "no inspector region": lambda g: g["regions"].pop("inspector"),
            "change voice outside": lambda g: g["controls"]["singerChange"].__setitem__(1, 20),
        }
        for name, mutate in cases.items():
            with self.subTest(name):
                geometry = compact_geometry(True)
                mutate(geometry)
                result = geometry_result(geometry, self.EXPECTED | {"inspectorOpen": True})
                self.assertEqual(result["result"], "FAIL")





class BuildBindingTests(unittest.TestCase):
    def setUp(self):
        self.build = Path(tempfile.mkdtemp(prefix="seam-build-test-"))
        (self.build / "build.ninja").write_text("")
        self.app = self.build / "Project SEAM.app/Contents/MacOS/Project SEAM"

    def test_no_build_is_recorded_as_unverified(self):
        result = PACKET.build_app(self.app, skip=True)
        self.assertFalse(result["builtBeforeCapture"])
        self.assertIn("unverified", result["sourceBinding"])

    def test_the_app_is_built_from_the_tree_before_capture(self):
        done = subprocess.CompletedProcess([], 0, "[1/2] Building CXX object a.o\n[2/2] Linking CXX executable app\n", "")
        with mock.patch.object(PACKET.subprocess, "run", return_value=done) as run:
            result = PACKET.build_app(self.app, skip=False)
        self.assertEqual(run.call_args.args[0], ["ninja", "-C", str(self.build)])
        self.assertTrue(result["builtBeforeCapture"])
        self.assertEqual(result["stepsPerformed"], 2)

    def test_a_failed_build_stops_the_packet(self):
        failed = subprocess.CompletedProcess([], 1, "FAILED: a.o\n", "error")
        with mock.patch.object(PACKET.subprocess, "run", return_value=failed):
            with self.assertRaises(PACKET.PacketError):
                PACKET.build_app(self.app, skip=False)



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
