from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools.singing_quality.contract_types import CorpusError
from tools.singing_quality.runner import RunSettings, run_corpus

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
