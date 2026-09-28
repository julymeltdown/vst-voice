"""The Python mirror of the C++ generation request registry: the same records, admissions and refusals.

The workspace is a synthetic engineering fixture: a generated draft inventory, the committed procedural "ma"
recipe and bake, and fixture operators. No listening result, review decision, singer qualification or release
claim is made or implied.
"""
from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any, Callable

from tools.external_beta._production_generation import RegistryError, decode_request, encode_record, inspect_generation_requests
from tools.external_beta.voicebank_production import prepare_production_draft_definition, validate_production_draft_workspace
from tools.voicebank_script_generator.draft_inventory import generate_draft_inventory

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "build/release/seam_voicebank_cli"
BAKE = ROOT / "docs/implementation/evidence/nasal-consonant-2026-09-09/ma-bake"
CANDIDATE = BAKE / "candidates/00000000000153d9-00000000000153da"
RECIPE = BAKE / "recipes/7361e7c1d112ba26cf07284548c14866a865195f52d7737e4352b38382d8d2d2.json"


def _run(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([str(CLI), *arguments], capture_output=True, text=True, timeout=180)


def _ok(*arguments: str) -> dict[str, Any]:
    done = _run(*arguments)
    if done.returncode != 0:
        raise AssertionError(f"{arguments[0]} failed ({done.returncode}): {done.stderr}")
    return json.loads(done.stdout)


class RecordCodecTest(unittest.TestCase):
    """Field-level refusals that need no workspace."""

    def test_duplicate_fields_and_non_integer_numbers_are_refused(self) -> None:
        with self.assertRaises(RegistryError):
            decode_request(b'{"formatId": "a", "formatId": "b"}\n')
        with self.assertRaises(RegistryError):
            decode_request(b'{"formatId": NaN}\n')
        with self.assertRaises(RegistryError):
            decode_request(encode_record({"formatId": "com.project-seam.generation-request", "schemaVersion": 1.0}))


@unittest.skipUnless(CLI.is_file(), "requires build/release/seam_voicebank_cli")
class CppGenerationRequestParityTest(unittest.TestCase):
    """One workspace driven through the real CLI to a completed, an exhausted and a stale request."""

    root: Path
    workspace: Path
    outcomes: dict[str, tuple[int, dict[str, Any]]]
    requests: dict[str, str]

    @classmethod
    def setUpClass(cls) -> None:
        cls._temporary = tempfile.TemporaryDirectory(prefix="seam-generation-requests-")
        cls.root = Path(cls._temporary.name)
        inventory = generate_draft_inventory({"profileId": "generation-request-parity", "vowels": ["a"],
            "consonants": ["m"], "includeKinds": ["cv"], "alternateTakes": 1, "pitchLayers": [60, 64, 69],
            "requestedRange": {"minMidi": 60, "maxMidi": 72}})
        draft = prepare_production_draft_definition(inventory, None, project_id="generation-request-parity",
                                                    operator_id="producer")
        definition = cls.root / "definition.json"
        definition.write_bytes((json.dumps(draft, sort_keys=True, indent=2) + "\n").encode())
        cls.workspace = cls.root / "workspace"
        created = _ok("init-production", str(cls.workspace), str(definition),
                      hashlib.sha256(definition.read_bytes()).hexdigest(), "producer", "2026-09-28T06:00:00Z")
        license_path = cls.root / "license.txt"
        license_path.write_text("SYNTHETIC TEST ONLY: procedural fixture, no singer qualification")
        _ok("register-source", str(cls.workspace), created["projectSha256"], "procedural-a", "procedural", "pass",
            "yes", "yes", "no", "no", str(license_path), hashlib.sha256(license_path.read_bytes()).hexdigest(),
            "producer", "2026-09-28T06:01:00Z")
        units = {row["pitchLayer"]: row for row in cls._project(cls.workspace)["unitAssignments"]}
        cls.outcomes, cls.requests = {}, {}
        campaign = cls._campaign("completed", units[60]["plannedTakeId"])
        cls.outcomes["completed"] = cls._advance(campaign, "2026-09-28T06:02:00Z")
        campaign = cls._campaign("exhausted", units[64]["plannedTakeId"])
        with (campaign.parent / "retained-canary").open("wb") as stream:
            stream.truncate(8 * 1024 * 1024 * 1024 + 1)  # sparse: exceeds the campaign's admitted retention budget
        cls.outcomes["exhausted"] = cls._advance(campaign, "2026-09-28T06:03:00Z")
        campaign = cls._campaign("stale", units[69]["plannedTakeId"])
        _ok("submit-generation-campaign", str(cls.workspace), str(campaign), cls.requests["stale"], "producer",
            "2026-09-28T06:04:00Z")
        row = units[69]
        _ok("import-procedural", str(cls.workspace), str(CANDIDATE) + ".json", str(CANDIDATE) + ".wav", str(RECIPE),
            row["plannedTakeId"], row["promptId"], row["coverageKey"], "69", "producer", "2026-09-28T06:05:00Z")
        cls.outcomes["stale"] = cls._advance(campaign, "2026-09-28T06:06:00Z")

    @classmethod
    def tearDownClass(cls) -> None:
        cls._temporary.cleanup()

    @staticmethod
    def _project(workspace: Path) -> dict[str, Any]:
        return json.loads((workspace / "project.json").read_bytes())

    @classmethod
    def _campaign(cls, name: str, take_id: str) -> Path:
        plan = cls.root / f"{name}-plan.json"
        sha = _ok("draft-generation-campaign", str(cls.workspace), str(RECIPE), str(plan), take_id)["campaignSha256"]
        directory = cls.root / f"{name}-campaign"
        _ok("plan-generation-campaign", str(cls.workspace), str(plan), sha, str(directory))
        _ok("preflight-generation-campaign", str(directory / "campaign.json"), sha)
        cls.requests[name] = sha
        return directory / "campaign.json"

    @classmethod
    def _advance(cls, campaign: Path, at: str) -> tuple[int, dict[str, Any]]:
        done = _run("advance-generation-campaign", str(cls.workspace), str(campaign), cls.requests[campaign.parent.name[:-9]],
                    "producer", at)
        return done.returncode, json.loads(done.stdout)

    def _copy(self, name: str) -> Path:
        target = self.root / f"forged-{name}"
        shutil.copytree(self.workspace, target, symlinks=True)
        return target

    def _record(self, workspace: Path, name: str, file: str) -> tuple[Path, dict[str, Any]]:
        path = workspace / "generation-requests" / self.requests[name] / file
        return path, json.loads(path.read_bytes())

    def _generation_sha(self, workspace: Path, generation: int) -> str:
        return hashlib.sha256((workspace / "generations" / f"{generation:020}.json").read_bytes()).hexdigest()

    def _refused(self, name: str, forge: Callable[[Path], None]) -> None:
        workspace = self._copy(name)
        forge(workspace)
        self.assertNotEqual(0, _run("list-generation-requests", str(workspace)).returncode, f"C++ admitted {name}")
        self.assertTrue(inspect_generation_requests(workspace)[1], f"Python admitted {name}")

    def test_each_outcome_is_reported_with_its_exit_status(self) -> None:
        code, report = self.outcomes["completed"]
        self.assertEqual((0, "COLLECTED_UNREVIEWED", True, True),
                         (code, report["status"], report["registered"], report["terminalRecorded"]))
        code, report = self.outcomes["exhausted"]
        self.assertEqual((4, "BUDGET_EXHAUSTED", True), (code, report["status"], report["terminalRecorded"]))
        self.assertGreater(report["retainedBytes"], 8 * 1024 * 1024 * 1024)
        code, report = self.outcomes["stale"]
        self.assertEqual((3, "STALE", True), (code, report["status"], report["terminalRecorded"]))
        for _, report in self.outcomes.values():
            self.assertIs(False, report["releaseEligible"])

    def test_python_and_cpp_agree_on_every_record(self) -> None:
        listed = _ok("list-generation-requests", str(self.workspace))
        summaries, errors = inspect_generation_requests(self.workspace)
        self.assertEqual([], errors)
        self.assertEqual(listed["requests"], summaries)
        self.assertEqual(["COMPLETED", "BUDGET_EXHAUSTED", "STALE"], [summary["state"] for summary in summaries])
        inspected = _ok("inspect-generation-request", str(self.workspace), self.requests["stale"])
        self.assertEqual(summaries[2], {key: value for key, value in inspected.items() if key != "releaseEligible"})
        for name in self.requests:
            path, _ = self._record(self.workspace, name, "request.json")
            self.assertEqual(path.read_bytes(), encode_record(decode_request(path.read_bytes())))
        # The registry is part of the ordinary draft-workspace check, and the Python CLI reports the same records.
        self.assertTrue(validate_production_draft_workspace(self.workspace).passed)
        done = subprocess.run([sys.executable, "-m", "tools.external_beta.voicebank_production", "inspect-generation-requests",
                               "--workspace", str(self.workspace)], capture_output=True, text=True, cwd=ROOT, timeout=120)
        self.assertEqual(0, done.returncode, done.stderr)
        self.assertEqual(summaries, json.loads(done.stdout)["requests"])

    def test_a_retry_reports_the_recorded_outcome_without_work(self) -> None:
        before = (self.workspace / "project.json").read_bytes()
        campaign = self.root / "exhausted-campaign" / "campaign.json"
        self.assertEqual(4, self._advance(campaign, "2026-09-28T06:07:00Z")[0])
        self.assertEqual(before, (self.workspace / "project.json").read_bytes())

    def test_forged_records_are_refused_by_both_readers(self) -> None:
        def rewrite(path: Path, record: dict[str, Any]) -> None:
            path.write_bytes(encode_record(record))

        def completed_by_a_manual_import(workspace: Path) -> None:
            path, terminal = self._record(workspace, "stale", "terminal.json")
            _, request = self._record(workspace, "stale", "request.json")
            generation = request["expectedGeneration"] + 1
            rewrite(path, terminal | {"outcome": "COMPLETED", "completedBatches": 1, "observedGeneration": generation,
                                      "observedProjectSha256": self._generation_sha(workspace, generation)})

        def stale_at_its_own_expected_state(workspace: Path) -> None:
            path, terminal = self._record(workspace, "exhausted", "terminal.json")
            _, request = self._record(workspace, "exhausted", "request.json")
            rewrite(path, terminal | {"outcome": "STALE", "retainedBytes": 0,
                                      "observedGeneration": request["expectedGeneration"],
                                      "observedProjectSha256": request["expectedProjectSha256"]})

        def exhausted_within_budget(workspace: Path) -> None:
            path, terminal = self._record(workspace, "exhausted", "terminal.json")
            _, request = self._record(workspace, "exhausted", "request.json")
            rewrite(path, terminal | {"retainedBytes": request["budget"]["maximumBytes"]})

        def bound_to_other_bytes(workspace: Path) -> None:
            path, terminal = self._record(workspace, "completed", "terminal.json")
            rewrite(path, terminal | {"requestSha256": "0" * 64})

        def extra_request_field(workspace: Path) -> None:
            path, request = self._record(workspace, "completed", "request.json")
            rewrite(path, request | {"approved": True})

        def completed_later_than_its_collection(workspace: Path) -> None:
            path, terminal = self._record(workspace, "completed", "terminal.json")
            generation = terminal["observedGeneration"] + 1
            rewrite(path, terminal | {"observedGeneration": generation,
                                      "observedProjectSha256": self._generation_sha(workspace, generation)})

        def request_for_an_existing_take(workspace: Path) -> None:
            # A consistent pair: the stale request renamed to the completed request's take, its terminal rebound.
            path, request = self._record(workspace, "stale", "request.json")
            _, completed = self._record(workspace, "completed", "request.json")
            job = request["jobs"][0] | {key: completed["jobs"][0][key] for key in ("takeId", "pitchLayer", "coverageKey", "style")}
            payload = encode_record(request | {"jobs": [job]})
            path.write_bytes(payload)
            terminal_path, terminal = self._record(workspace, "stale", "terminal.json")
            rewrite(terminal_path, terminal | {"requestSha256": hashlib.sha256(payload).hexdigest()})

        def compact_request_bytes(workspace: Path) -> None:
            # Same fields, other bytes: only canonical encoding refuses it once the terminal is rebound.
            path, request = self._record(workspace, "completed", "request.json")
            payload = json.dumps(request, sort_keys=True).encode()
            path.write_bytes(payload)
            terminal_path, terminal = self._record(workspace, "completed", "terminal.json")
            rewrite(terminal_path, terminal | {"requestSha256": hashlib.sha256(payload).hexdigest()})

        for forge in (completed_by_a_manual_import, stale_at_its_own_expected_state, exhausted_within_budget,
                      bound_to_other_bytes, extra_request_field, completed_later_than_its_collection,
                      request_for_an_existing_take, compact_request_bytes):
            with self.subTest(forge.__name__):
                self._refused(forge.__name__, forge)


if __name__ == "__main__":
    unittest.main()
