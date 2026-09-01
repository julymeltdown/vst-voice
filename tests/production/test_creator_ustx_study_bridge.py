from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest

import yaml


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
BRIDGE = REPOSITORY_ROOT / "scripts/creator_ustx_study_bridge.py"
USTX_FIXTURE = (
    REPOSITORY_ROOT / "tests/fixtures/creator-study/minimal-openutau-0.9.ustx"
)


def run_bridge(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(BRIDGE), *arguments],
        cwd=REPOSITORY_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )


class CreatorUstxStudyBridgeTests(unittest.TestCase):
    def test_import_creates_schema7_project_and_explicit_loss_report(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            project_path = root / "study.seam"
            report_path = root / "import-report.json"

            completed = run_bridge(
                "import-ustx",
                str(USTX_FIXTURE),
                str(project_path),
                "--report",
                str(report_path),
                "--voicebank-id",
                "study.bank",
                "--voicebank-version",
                "0.1.0-study",
                "--character-id",
                "study.character",
            )

            self.assertEqual(0, completed.returncode, completed.stderr)
            project = json.loads(project_path.read_text(encoding="utf-8"))
            report = json.loads(report_path.read_text(encoding="utf-8"))

        self.assertEqual("com.project-seam.project", project["formatId"])
        self.assertEqual(7, project["schemaVersion"])
        self.assertEqual(960, project["ppq"])
        self.assertEqual([0, 960], [event["tick"] for event in project["tempoMap"]])
        track = project["vocalTracks"][0]
        self.assertEqual("Lead", track["name"])
        self.assertEqual("study.bank", track["voicebank"]["id"])
        region = track["regions"][0]
        self.assertEqual(2880, region["durationTick"])
        self.assertEqual([0, 960], [note["startTick"] for note in region["notes"]])
        self.assertEqual([960, 960], [note["durationTick"] for note in region["notes"]])
        self.assertEqual(["あ", "い"], [lyric["surface"] for lyric in region["lyrics"]])
        self.assertEqual(
            [0, 480, 960, 1440, 1920],
            [point["tick"] for point in region["pitchAutomation"]],
        )
        self.assertEqual(
            [0.0, 50.0, -200.0, -30.0, -30.0],
            [point["cents"] for point in region["pitchAutomation"]],
        )
        self.assertTrue(report["studyOnly"])
        self.assertEqual("USTX_TO_SEAM", report["direction"])
        self.assertFalse(report["unsafeReferencesResolved"])
        loss_codes = {loss["code"] for loss in report["losses"]}
        self.assertIn("USTX_SINGER_REFERENCE_NOT_IMPORTED", loss_codes)
        self.assertIn("USTX_NOTE_VIBRATO_NOT_REPRESENTABLE", loss_codes)
        self.assertIn("USTX_PITCH_SHAPE_APPROXIMATED", loss_codes)
        self.assertIn("USTX_PART_DURATION_EXTENDED_BY_OPENUTAU", loss_codes)
        self.assertIn("USTX_SNAP_FIRST_MATERIALIZED", loss_codes)
        self.assertIn("USTX_PITCH_POINT_COLLISION", loss_codes)

    def test_supported_subset_round_trips_through_both_cli_directions(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            seam_path = root / "study.seam"
            import_report = root / "import-report.json"
            exported_path = root / "exported.ustx"
            export_report = root / "export-report.json"

            imported = run_bridge(
                "import-ustx",
                str(USTX_FIXTURE),
                str(seam_path),
                "--report",
                str(import_report),
            )
            project = json.loads(seam_path.read_text(encoding="utf-8"))
            project["vocalTracks"][0]["regions"][0]["durationTick"] = 1920
            seam_path.write_text(
                json.dumps(project, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
            exported = run_bridge(
                "export-ustx",
                str(seam_path),
                str(exported_path),
                "--report",
                str(export_report),
            )

            self.assertEqual(0, imported.returncode, imported.stderr)
            self.assertEqual(0, exported.returncode, exported.stderr)
            payload = yaml.safe_load(exported_path.read_text(encoding="utf-8"))
            report = json.loads(export_report.read_text(encoding="utf-8"))

        self.assertEqual("0.9", str(payload["ustx_version"]))
        self.assertEqual([0, 480], [event["position"] for event in payload["tempos"]])
        self.assertEqual("Lead", payload["tracks"][0]["track_name"])
        part = payload["voice_parts"][0]
        self.assertEqual(1440, part["duration"])
        notes = part["notes"]
        self.assertEqual([0, 480], [note["position"] for note in notes])
        self.assertEqual([480, 480], [note["duration"] for note in notes])
        self.assertEqual([60, 62], [note["tone"] for note in notes])
        self.assertEqual(["あ", "い"], [note["lyric"] for note in notes])
        self.assertEqual(
            [[0.0, 250.0, 500.0], [0.0, 200.0, 400.0]],
            [[point["x"] for point in note["pitch"]["data"]] for note in notes],
        )
        self.assertEqual(
            [[0.0, 5.0, -20.0], [-20.0, -3.0, -3.0]],
            [[point["y"] for point in note["pitch"]["data"]] for note in notes],
        )
        self.assertEqual(
            [False, False], [note["pitch"]["snap_first"] for note in notes]
        )
        self.assertTrue(report["studyOnly"])
        self.assertEqual("SEAM_TO_USTX", report["direction"])
        self.assertFalse(report["unsafeReferencesResolved"])
        loss_codes = {loss["code"] for loss in report["losses"]}
        self.assertIn("SEAM_BANK_REFERENCE_NOT_EXPORTED", loss_codes)
        self.assertIn("SEAM_CHARACTER_REFERENCE_NOT_EXPORTED", loss_codes)
        self.assertIn("SEAM_OUTPUT_ROUTE_NOT_EXPORTED", loss_codes)
        self.assertIn("SEAM_PART_DURATION_EXTENDED_FOR_OPENUTAU", loss_codes)

    def test_note_tuning_is_materialized_in_imported_pitch(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            source_path = root / "tuned.ustx"
            project_path = root / "tuned.seam"
            report_path = root / "report.json"
            payload = yaml.safe_load(USTX_FIXTURE.read_text(encoding="utf-8"))
            part = payload["voice_parts"][0]
            first_note = part["notes"][0]
            first_note["tuning"] = 25
            part["notes"] = [first_note]
            source_path.write_text(
                yaml.safe_dump(payload, allow_unicode=True, sort_keys=False),
                encoding="utf-8",
            )

            completed = run_bridge(
                "import-ustx",
                str(source_path),
                str(project_path),
                "--report",
                str(report_path),
            )

            self.assertEqual(0, completed.returncode, completed.stderr)
            project = json.loads(project_path.read_text(encoding="utf-8"))
            points = project["vocalTracks"][0]["regions"][0]["pitchAutomation"]

        self.assertEqual([0, 480, 960], [point["tick"] for point in points])
        self.assertEqual([25.0, 75.0, 75.0], [point["cents"] for point in points])

    def test_pitchless_note_resets_the_region_curve_to_zero_cents(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            source_path = root / "pitchless-second-note.ustx"
            project_path = root / "pitchless-second-note.seam"
            report_path = root / "report.json"
            payload = yaml.safe_load(USTX_FIXTURE.read_text(encoding="utf-8"))
            notes = payload["voice_parts"][0]["notes"]
            notes[0]["pitch"]["snap_first"] = False
            _ = notes[1].pop("pitch")
            source_path.write_text(
                yaml.safe_dump(payload, allow_unicode=True, sort_keys=False),
                encoding="utf-8",
            )

            completed = run_bridge(
                "import-ustx",
                str(source_path),
                str(project_path),
                "--report",
                str(report_path),
            )

            self.assertEqual(0, completed.returncode, completed.stderr)
            project = json.loads(project_path.read_text(encoding="utf-8"))
            points = project["vocalTracks"][0]["regions"][0]["pitchAutomation"]
            cents_by_tick = {point["tick"]: point["cents"] for point in points}

        self.assertEqual(0.0, cents_by_tick[960])
        self.assertEqual(0.0, cents_by_tick[1920])

    def test_known_ustx_external_and_unsupported_fields_are_reported(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            source_path = root / "references.ustx"
            project_path = root / "references.seam"
            report_path = root / "report.json"
            payload = yaml.safe_load(USTX_FIXTURE.read_text(encoding="utf-8"))
            payload["wave_parts"] = [{"name": "Backing", "file_path": "audio.wav"}]
            track = payload["tracks"][0]
            track["phonemizer"] = "External.Phonemizer"
            track["renderer_settings"] = {
                "renderer": "External.Renderer",
                "resampler": "external-resampler",
                "wavtool": "external-wavtool",
            }
            track["mix_fx"] = {"reverb": {"enabled": True}}
            track["voice_color_names"] = ["Soft"]
            track["track_expressions"] = [{"abbr": "dyn", "name": "Dynamics"}]
            payload["expressions"] = {"dyn": {"name": "Dynamics"}}
            source_path.write_text(
                yaml.safe_dump(payload, allow_unicode=True, sort_keys=False),
                encoding="utf-8",
            )

            completed = run_bridge(
                "import-ustx",
                str(source_path),
                str(project_path),
                "--report",
                str(report_path),
            )

            self.assertEqual(0, completed.returncode, completed.stderr)
            report = json.loads(report_path.read_text(encoding="utf-8"))
            loss_codes = {loss["code"] for loss in report["losses"]}

        self.assertFalse(report["unsafeReferencesResolved"])
        self.assertTrue(
            {
                "USTX_WAVE_PARTS_NOT_IMPORTED",
                "USTX_PHONEMIZER_REFERENCE_NOT_IMPORTED",
                "USTX_RENDERER_REFERENCE_NOT_IMPORTED",
                "USTX_RESAMPLER_REFERENCE_NOT_IMPORTED",
                "USTX_WAVTOOL_REFERENCE_NOT_IMPORTED",
                "USTX_TRACK_MIX_FX_NOT_IMPORTED",
                "USTX_VOICE_COLORS_NOT_IMPORTED",
                "USTX_TRACK_EXPRESSIONS_NOT_IMPORTED",
                "USTX_EXPRESSIONS_NOT_IMPORTED",
            }.issubset(loss_codes)
        )


if __name__ == "__main__":
    _ = unittest.main()
