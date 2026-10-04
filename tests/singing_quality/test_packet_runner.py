from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools.singing_quality.contract_types import CorpusError
from tools.singing_quality.runner import RunSettings, check_frozen_limits, run_corpus

ROOT = Path(__file__).resolve().parents[2]
CORPUS = ROOT / "tests/singing_quality/corpus/corpus.json"


class PacketRunnerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.build = self.directory / "build.txt"
        self.build.write_text("test build evidence", encoding="utf-8")
        self.source = self.directory / "source.txt"
        self.source.write_text("test source evidence", encoding="utf-8")
        self.settings = RunSettings(ROOT, CORPUS, self.directory, Path(sys.executable),
                                    Path(sys.executable), self.build, self.source)

    def _write_case(self, packet: Path, case: str, **fields: object) -> None:
        directory = packet / case
        directory.mkdir(parents=True, exist_ok=True)
        # The real packet nests the verdict under the pitch key; timing and
        # duration carry separate within_frozen_limit keys.
        body = dict(fields)
        within = body.pop("within_frozen_limits")
        body["pitch"] = {"within_frozen_limits": within}
        (directory / "measurement.json").write_text(
            json.dumps({"case_id": case, **body}), encoding="utf-8")

    def test_frozen_limit_gate_reports_a_case_that_missed_its_criteria(self) -> None:
        packet = self.directory / "packet"
        self._write_case(packet, "good-bank", within_frozen_limits=True)
        self._write_case(packet, "good-raw", within_frozen_limits=True)
        self._write_case(packet, "bad-bank", within_frozen_limits=False)
        verdict = check_frozen_limits(packet)
        self.assertFalse(verdict.ok)
        self.assertEqual(verdict.failing, ("bad-bank",))
        self.assertEqual(verdict.missing, ())

    def test_frozen_limit_gate_passes_when_every_case_is_inside_its_limits(self) -> None:
        packet = self.directory / "packet"
        self._write_case(packet, "a-bank", within_frozen_limits=True)
        self._write_case(packet, "a-raw", within_frozen_limits=True)
        verdict = check_frozen_limits(packet)
        self.assertTrue(verdict.ok)

    def test_frozen_limit_gate_refuses_to_pass_a_case_with_no_verdict(self) -> None:
        # A case whose measurement errored must not read as a pass. Treating an
        # absent verdict as success would make an unmeasurable case quieter than
        # a measurable one, which is the opposite of what a gate should do.
        packet = self.directory / "packet"
        self._write_case(packet, "ok-bank", within_frozen_limits=True)
        (packet / "broken-raw").mkdir(parents=True)
        (packet / "broken-raw" / "measurement-error.json").write_text(
            json.dumps({"error": "no pitch"}), encoding="utf-8")
        verdict = check_frozen_limits(packet)
        self.assertFalse(verdict.ok)
        self.assertEqual(verdict.missing, ("broken-raw",))

    def test_require_limits_flag_fails_a_run_whose_cases_missed(self) -> None:
        # The regression this guards: the packet records within_frozen_limits per
        # case and nothing read it, so a corpus that failed every case still
        # exited zero. Assert the real exit codes of the entry point, not just
        # that the flag is mentioned in help text.
        packet = self.directory / "packet"
        self._write_case(packet, "bad-bank", within_frozen_limits=False)
        self._write_case(packet, "good-raw", within_frozen_limits=True)
        driver = self.directory / "entry.py"
        driver.write_text(
            "import sys\n"
            "from pathlib import Path\n"
            f"sys.path.insert(0, {str(ROOT)!r})\n"
            "import tools.singing_quality.__main__ as entry\n"
            f"entry.run_corpus = lambda settings: Path({str(packet)!r})\n"
            "sys.argv = ['singing-quality', '--root', '.', '--corpus', 'unused',\n"
            "            '--output-parent', 'unused', '--driver', 'unused',\n"
            "            '--analyzer', 'unused', '--build-evidence', 'unused',\n"
            "            '--source-evidence', 'unused'] + sys.argv[1:]\n"
            "raise SystemExit(entry.main())\n",
            encoding="utf-8")
        without = subprocess.run([sys.executable, str(driver)], cwd=ROOT,
                                 capture_output=True, text=True, timeout=60)
        self.assertEqual(without.returncode, 0, without.stderr)
        gated = subprocess.run([sys.executable, str(driver), "--require-limits"], cwd=ROOT,
                               capture_output=True, text=True, timeout=60)
        self.assertEqual(gated.returncode, 1, gated.stderr)
        self.assertIn("bad-bank", gated.stderr)

    def test_require_limits_reports_each_failing_case_by_name(self) -> None:
        packet = self.directory / "packet"
        self._write_case(packet, "original-melody-bank", within_frozen_limits=False)
        verdict = check_frozen_limits(packet)
        self.assertIn("original-melody-bank", verdict.failing)

    def test_failed_renderer_preserves_execution_record(self) -> None:
        with patch("subprocess.run", return_value=subprocess.CompletedProcess([], 7)) as process:
            with self.assertRaises(CorpusError) as caught:
                run_corpus(self.settings)
        self.assertEqual("process_exit", caught.exception.code)
        self.assertEqual(1, process.call_count)
        records = list(self.directory.glob("u1-*/commands/*.json"))
        self.assertEqual(1, len(records))
        self.assertEqual(7, json.loads(records[0].read_text())["exit_code"])
        self.assertIn(str(records[0].resolve()), caught.exception.detail)
        self.assertIn(str(records[0].with_suffix(".stderr").resolve()), caught.exception.detail)

    def test_success_exit_without_audio_is_rejected(self) -> None:
        with patch("subprocess.run", return_value=subprocess.CompletedProcess([], 0)) as process:
            with self.assertRaises(CorpusError) as caught:
                run_corpus(self.settings)
        self.assertEqual("asset_missing", caught.exception.code)
        self.assertEqual(1, process.call_count)

    def test_staging_preserves_layout_and_uses_verified_source_bytes(self) -> None:
        with patch("subprocess.run", return_value=subprocess.CompletedProcess([], 9)):
            with self.assertRaises(CorpusError):
                run_corpus(self.settings)
        packet = next(self.directory.glob("u1-*/"))
        spec = json.loads(CORPUS.read_text())
        for asset in spec["assets"]:
            self.assertEqual((ROOT / asset["path"]).read_bytes(),
                             (packet / "inputs" / asset["path"]).read_bytes())
        self.assertEqual(CORPUS.read_bytes(), (packet / "corpus.json").read_bytes())
        self.assertEqual(self.source.read_bytes(), (packet / "source-evidence").read_bytes())
        self.assertEqual(self.build.read_bytes(), (packet / "build-evidence").read_bytes())

    def test_repeated_runs_create_distinct_packets(self) -> None:
        with patch("subprocess.run", return_value=subprocess.CompletedProcess([], 9)):
            for _ in range(2):
                with self.assertRaises(CorpusError):
                    run_corpus(self.settings)
        self.assertEqual(2, len(list(self.directory.glob("u1-*/"))))


if __name__ == "__main__":
    unittest.main()
