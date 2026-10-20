"""Tests for the PJS engineering-bank converter and the installed-bank export command.

The converter is exercised on a synthetic four-syllable "corpus" so the test owns every
boundary. When CTest supplies the native tools, the converted bank is also validated by
the engine, packed, installed with a trusted key and exported through
export-installed-bank-project from a fixture project re-pinned to it.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import numpy as np

from tools.voicebank_source_intake import pjs_engineering_bank as bank

CLI = os.environ.get("SEAM_SOURCE_INTAKE_CLI")
BANK_TOOL = os.environ.get("SEAM_SOURCE_INTAKE_BANK_TOOL")
REPO = Path(__file__).resolve().parents[1]
RATE = bank.SAMPLE_RATE
UNITS = bank.LABEL_UNITS_PER_SECOND

# (phone, seconds, midi or None). Vowels are harmonic tones; consonants are quiet noise.
SCRIPT = [("pau", 0.20, None), ("k", 0.06, None), ("i", 0.40, 64), ("pau", 0.15, None),
          ("m", 0.06, None), ("i", 0.40, 66), ("pau", 0.15, None), ("h", 0.06, None),
          ("a", 0.40, 67), ("pau", 0.15, None), ("a", 0.45, 69), ("pau0", 0.20, None)]


def synthetic_song() -> tuple[np.ndarray, str]:
    generator = np.random.default_rng(7)
    pieces, lines, cursor = [], [], 0
    for phone, seconds, midi in SCRIPT:
        frames = int(round(seconds * RATE))
        if midi is None:
            level = 0.0 if phone.startswith("pau") else 0.05
            piece = generator.uniform(-level, level, frames)
        else:
            hz = 440.0 * 2.0 ** ((midi - 69) / 12.0)
            t = np.arange(frames) / RATE
            piece = 0.3 * np.sin(2 * np.pi * hz * t) + 0.1 * np.sin(4 * np.pi * hz * t)
        pieces.append(piece)
        start = cursor * UNITS // RATE
        cursor += frames
        lines.append(f"{start} {cursor * UNITS // RATE} {phone}")
    return np.concatenate(pieces), "\n".join(lines) + "\n"


class LabelAndWriterTests(unittest.TestCase):
    def test_labels_convert_100ns_units_and_map_pjs_symbols(self) -> None:
        phones = bank.parse_labels("0 2000000 pau0\n2000000 2500000 k\n2500000 7000000 U\n", RATE)
        self.assertEqual([(p.symbol, p.source_symbol) for p in phones],
                         [("pau", "pau0"), ("k", "k"), ("u", "U")])
        self.assertEqual((phones[1].start, phones[1].end), (9600, 12000))

    def test_labels_past_the_audio_are_refused(self) -> None:
        with self.assertRaisesRegex(bank.IntakeError, "ends after the audio"):
            bank.parse_labels("0 20000000 a\n", RATE)

    def test_overlapping_labels_are_refused(self) -> None:
        with self.assertRaisesRegex(bank.IntakeError, "overlaps"):
            bank.parse_labels("0 5000000 a\n2000000 6000000 i\n", RATE)

    def test_writer_pads_an_odd_data_chunk(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "one.wav"
            bank.write_mono_pcm(path, np.array([1000.0]), 3)
            data = path.read_bytes()
            self.assertEqual(len(data) % 2, 0)
            self.assertEqual(int.from_bytes(data[4:8], "little"), len(data) - 8)
            samples, width = bank.read_mono_pcm(path)
            self.assertEqual((samples.tolist(), width), ([1000], 3))
            if CLI:
                inspected = subprocess.run([CLI, "inspect-wav", str(path)], capture_output=True, text=True)
                self.assertEqual(inspected.returncode, 0, inspected.stderr)

    def test_proposals_are_cv_and_sustain_with_ordered_markers(self) -> None:
        _, labels = synthetic_song()
        phones = bank.parse_labels(labels, 10 * RATE)
        proposals = bank.propose_units("pjs001", phones)
        self.assertEqual([(c.alias, c.kind) for c in proposals],
                         [("k i", "cv"), ("m i", "cv"), ("h a", "cv"), ("a", "sustain")])
        for candidate in proposals:
            m = candidate.markers
            self.assertTrue(0 <= m["consonantEnd"] <= m["vowelOnset"] <= m["stableStart"]
                            <= m["loopStart"] < m["loopEnd"] <= m["releaseStart"] < m["audioEnd"])
            self.assertEqual(m["audioEnd"], candidate.cut_end - candidate.cut_start)


@unittest.skipUnless(CLI and BANK_TOOL, "native seam_voicebank_cli and seam_bank_tool are required")
class NativeBankTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        corpus = self.root / "corpus" / "pjs001"
        labels = self.root / "labels" / "lab"
        corpus.mkdir(parents=True)
        labels.mkdir(parents=True)
        samples, text = synthetic_song()
        bank.write_mono_pcm(corpus / "pjs001_song.wav", samples * 8388607.0, 3)
        (labels / "pjs001.lab").write_text(text, encoding="utf-8")
        (labels.parent / "LICENSE").write_text("Synthetic fixture licence text.\n", encoding="utf-8")
        self.output = self.root / "bank"
        self.summary = bank.build(corpus.parent, labels, Path(CLI), self.output, 2, None)

    def run_tool(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(list(arguments), capture_output=True, text=True)

    def test_converted_bank_has_measured_pitch_and_attribution(self) -> None:
        manifest = json.loads((self.output / "manifest.json").read_text(encoding="utf-8"))
        roots = {unit["alias"]: unit["rootMidi"] for unit in manifest["units"]}
        self.assertEqual(roots, {"k i": 64, "m i": 66, "h a": 67, "a": 69})
        self.assertEqual(manifest["language"], "ja")
        self.assertTrue(manifest["id"].startswith("engineering."))
        self.assertEqual(self.summary["status"], "ENGINEERING_ONLY_NOT_RELEASE_ELIGIBLE")
        for name in ("LICENSE", "NOTICE", "provenance.json"):
            self.assertTrue((self.output / name).is_file(), name)
        validated = self.run_tool(CLI, "validate", str(self.output / "manifest.json"), str(self.output))
        self.assertEqual(validated.returncode, 0, validated.stdout + validated.stderr)
        self.assertEqual(json.loads(validated.stdout)["errors"], 0)

    def test_an_existing_output_is_refused(self) -> None:
        exit_code = bank.main(["--corpus", str(self.root / "corpus"), "--labels", str(self.root / "labels" / "lab"),
                               "--pitch-tool", CLI, "--output", str(self.output)])
        self.assertEqual(exit_code, 2)

    def test_installed_bank_exports_a_pinned_project_and_refuses_when_absent(self) -> None:
        keys = self.root / "keys"
        keys.mkdir()
        self.assertEqual(self.run_tool(BANK_TOOL, "keygen", str(keys / "private.json"),
                                       str(keys / "public.json")).returncode, 0)
        package = self.root / "bank.seambank"
        packed = self.run_tool(BANK_TOOL, "pack", str(self.output), str(package), str(keys / "private.json"))
        self.assertEqual(packed.returncode, 0, packed.stderr)
        installed_root = self.root / "Voicebanks"
        installed = self.run_tool(BANK_TOOL, "install", str(package), str(installed_root),
                                  "--public-key", str(keys / "public.json"))
        self.assertEqual(installed.returncode, 0, installed.stderr)
        receipt = next(installed_root.glob("*/*/install-receipt.json"))
        content_hash = json.loads(receipt.read_text(encoding="utf-8"))["contentHash"]

        project = json.loads((REPO / "tests/fixtures/projects/schema-3-phase3-demo.seam").read_text(encoding="utf-8"))
        manifest = json.loads((self.output / "manifest.json").read_text(encoding="utf-8"))
        project["vocalTracks"][0]["voicebank"] = {"id": manifest["id"], "version": manifest["version"],
                                                   "contentHash": content_hash}
        project_path = self.root / "song.seam"
        project_path.write_text(json.dumps(project, ensure_ascii=False), encoding="utf-8")

        master = self.root / "master.wav"
        exported = self.run_tool(CLI, "export-installed-bank-project", str(project_path), str(master),
                                 str(installed_root))
        self.assertEqual(exported.returncode, 0, exported.stderr)
        report = json.loads(exported.stdout)
        self.assertEqual(report["state"], "Committed")
        self.assertEqual(report["voicebanks"][0]["trust"], "trusted-installed")
        self.assertEqual(report["evidenceScope"], "ENGINEERING_ONLY")
        self.assertGreater(report["masterPeakAbs"], 0.01)
        self.assertTrue(master.is_file())

        empty = self.root / "EmptyRoot"
        empty.mkdir()
        refused = self.run_tool(CLI, "export-installed-bank-project", str(project_path),
                                str(self.root / "refused.wav"), str(empty))
        self.assertEqual(refused.returncode, 1)
        self.assertIn("voicebank", refused.stderr)
        self.assertFalse((self.root / "refused.wav").exists())


if __name__ == "__main__":
    unittest.main()
