"""Every voicebank manifest's declared root pitch must match the audio it names.

The renderer treats rootMidi as the pitch of the recording: it resamples each
placement by targetHz / rootHz (libs/seam-synthesis/src/spectral_classic.cpp). A
unit whose declared root does not match its audio therefore renders every note
transposed by a constant amount, and every pitch-accuracy figure measured against
the score describes that offset rather than the renderer.

This is not hypothetical. The demo bank's recording is a 0.55-second spoken
utterance whose median pitch inside its declared loop span is 695.4 Hz while all
eight of its units declare rootMidi 67 (392.0 Hz), a 992-cent discrepancy.
Recorded in
docs/implementation/LISTENING_PACKET_ROOT_MIDI_MISMATCH_2026-10-03.md.

The check is deliberately a REPORT, not a gate that fails the build. The demo bank
is a known-unsuitable technical fixture by its own README and provenance, so failing
on it would make the mismatch permanently invisible by being permanently red. What
the check does is make every bank's measured offset an explicit, asserted fact, so
that no intonation number is quoted from any of them without it, and so that a NEW
bank with a wrong root cannot be added without the suite noticing.

test_declared_root_mismatches_are_all_known is the assertion that carries weight:
it pins the current classification of every shipped bank, so adding a bank whose
root does not match its audio fails that case.
"""

from __future__ import annotations

import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]

# Every voicebank manifest the repository ships, by path relative to the root. Each
# entry records the cents of offset that bank is KNOWN to have, so a change in either
# direction fails the suite and has to be looked at. The phase7 bank is the positive
# control: its audio is a real 440 Hz tone and it matches its declared root exactly,
# which is what proves the measurement can report a match and not only a mismatch.
KNOWN_BANKS: dict[str, dict[str, object]] = {
    "assets/demo-human-voicebank-public-domain/manifest.json": {
        # Measured +998 cents over the whole file: this manifest declares no loop span, so
        # every voiced frame counts. The bound sits below the measured value, not above a
        # round number, so a fixture that moved most of the way to correct would fail here.
        "tolerance_cents": 500.0,
        "expect_match": False,
        "why": "spoken utterance reused under one phoneme label; see the listening-packet entry",
    },
    "assets/demo-human-voicebank-public-domain/production-bank/manifest.json": {
        # Measured +992 cents inside the declared loop span of frames 5292 to 21609.
        "tolerance_cents": 500.0,
        "expect_match": False,
        "why": "same spoken utterance under eight phoneme labels",
    },
    "out/phase7/source/manifest.json": {
        "tolerance_cents": 50.0,
        "expect_match": True,
        "why": "a 440 Hz tone declared at rootMidi 69; the positive control",
    },
    "out/phase7/installed/official.voice.01.demo/0.7.0/manifest.json": {
        "tolerance_cents": 50.0,
        "expect_match": True,
        "why": "installed copy of the phase7 source bank",
    },
}

# Directories that hold only local build or evidence artifacts. A manifest copied into one
# of these is a copy of a bank that is already listed, and out/ is gitignored, so requiring
# those copies to be classified would make this case depend on whatever the machine happens
# to have built. Shipped banks are the ones that can reach a user.
ARTIFACT_PREFIXES = ("out/",)


def _is_shipped(rel: str) -> bool:
    return not rel.startswith(ARTIFACT_PREFIXES)


def _extractor() -> str | None:
    for candidate in ("build/release/seam_voicebank_cli", "build/dev/seam_voicebank_cli"):
        path = ROOT / candidate
        if path.exists():
            return str(path)
    return shutil.which("seam_voicebank_cli")


def _measure(extractor: str, audio: Path, loop_start, loop_end):
    """Return (median Hz inside the loop span, voiced frame count, octave spread)."""
    result = subprocess.run([extractor, "extract-pitch", str(audio)],
                            capture_output=True, text=True, check=True)
    frames = json.loads(result.stdout)["pitchFrames"]
    inside = [f["f0Hz"] for f in frames
              if f["voiced"] and f["f0Hz"] > 0
              and (loop_start is None or f["sourceFrame"] >= loop_start)
              and (loop_end is None or f["sourceFrame"] < loop_end)]
    if not inside:
        return 0.0, 0, 0.0
    inside.sort()
    spread = inside[-1] / max(inside[0], 1e-9)
    return inside[len(inside) // 2], len(inside), spread


class BankRootPitchTest(unittest.TestCase):
    def test_every_shipped_bank_manifest_is_listed(self) -> None:
        """No manifest carrying units may exist outside KNOWN_BANKS.

        This is what generalises the original single-bank check: a new voicebank
        added anywhere in the repository has to be classified here, so a wrong root
        cannot be introduced by shipping a new manifest.

        The search is by CONTENT over every .json under these roots, not by the filename
        "manifest.json". The first version globbed for that exact name and a probe bank
        deliberately saved under a different filename sailed straight through the guard,
        which is the failure this case exists to prevent; it was caught by trying to break
        the guard rather than by trusting it.
        """
        found = {}
        for pattern in ("assets/**/*.json", "tests/**/*.json", "out/**/*.json"):
            for path in ROOT.glob(pattern):
                rel = str(path.relative_to(ROOT))
                if not _is_shipped(rel):
                    continue
                try:
                    data = json.loads(path.read_text())
                except (OSError, ValueError):
                    continue
                if not isinstance(data, dict):
                    continue
                units = data.get("units")
                if not isinstance(units, list) or not units:
                    continue
                if not any(isinstance(unit, dict) and "rootMidi" in unit for unit in units):
                    continue
                found[rel] = len(units)
        unlisted = sorted(set(found) - set(KNOWN_BANKS))
        self.assertEqual(unlisted, [],
                         "voicebank manifests not classified in KNOWN_BANKS: " + str(unlisted))

    def test_nonbank_json_does_not_hide_an_unlisted_shipped_bank(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'assets').mkdir()
            (root / 'out').mkdir()
            (root / 'assets' / 'report.json').write_text('["not a bank"]')
            (root / 'out' / 'retention-index.json').write_text('[{"sha256": "receipt"}]')
            with mock.patch.dict(globals(), {'ROOT': root}):
                self.test_every_shipped_bank_manifest_is_listed()
                # Discovery is still content-based, and a malformed first unit
                # cannot hide the actual bank unit that follows it.
                (root / 'assets' / 'unexpected.json').write_text(
                    '{"units": [null, {"rootMidi": 69}]}')
                with self.assertRaisesRegex(AssertionError, 'unexpected.json'):
                    self.test_every_shipped_bank_manifest_is_listed()

    def test_declared_root_mismatches_are_all_known(self) -> None:
        """Measure each listed bank and assert its offset matches the recorded class."""
        extractor = _extractor()
        if extractor is None:
            self.skipTest("seam_voicebank_cli is not built")
        for rel, expect in KNOWN_BANKS.items():
            with self.subTest(bank=rel):
                manifest_path = ROOT / rel
                if not manifest_path.exists():
                    self.skipTest(rel + " is not present in this checkout")
                manifest = json.loads(manifest_path.read_text())
                unit = manifest["units"][0]
                audio = manifest_path.parent / unit["audio"]
                self.assertTrue(audio.exists(), rel + " names missing audio " + str(unit["audio"]))
                median_hz, frames, _ = _measure(
                    extractor, audio, unit.get("loopStartFrame"), unit.get("loopEndFrame"))
                self.assertGreater(frames, 0, rel + " audio has no voiced frames to measure")
                declared_hz = 440.0 * (2.0 ** ((unit["rootMidi"] - 69) / 12.0))
                cents = 1200 * math.log2(median_hz / declared_hz)
                tolerance = float(expect["tolerance_cents"])
                if bool(expect["expect_match"]):
                    self.assertLess(abs(cents), tolerance,
                                    rel + " should match its declared root within "
                                    + str(tolerance) + " cents but reads " + format(cents, "+.0f")
                                    + " (" + str(expect["why"]) + ")")
                else:
                    self.assertGreater(
                        abs(cents), tolerance,
                        rel + " was recorded as NOT matching its declared root but now reads "
                        + format(cents, "+.0f") + " cents. If the fixture was replaced with a "
                        "correctly pitched vowel, move it to expect_match with a tight tolerance "
                        "instead of widening this one. Context: " + str(expect["why"]))

    def test_a_matching_bank_really_is_measurable_as_matching(self) -> None:
        """The positive control must pass, or the case above proves nothing.

        Without this, a broken measurement returning 0 Hz could make every bank look
        mismatched and the classification would hold vacuously. The control is a tone
        generated here rather than a checked-in bank, because every bank this repository
        ships has the wrong root and out/ is a gitignored artifact directory, so relying on
        one would make this case depend on what the machine happens to have built.
        """
        extractor = _extractor()
        if extractor is None:
            self.skipTest("seam_voicebank_cli is not built")
        rate = 48000
        root_midi = 69
        declared_hz = 440.0
        frames_written = rate  # one second
        with tempfile.TemporaryDirectory() as tmp:
            wav = Path(tmp) / "control.wav"
            samples = bytearray()
            for index in range(frames_written):
                value = int(12000 * math.sin(2 * math.pi * declared_hz * index / rate))
                samples += struct.pack("<h", value)
            header = b"RIFF" + struct.pack("<I", 36 + len(samples)) + b"WAVEfmt "
            header += struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
            header += b"data" + struct.pack("<I", len(samples))
            wav.write_bytes(header + bytes(samples))
            median_hz, frames, spread = _measure(extractor, wav, None, None)
        cents = 1200 * math.log2(median_hz / declared_hz)
        self.assertGreater(frames, 0)
        self.assertLess(abs(cents), 50.0, "control bank reads " + format(cents, "+.0f") + " cents")
        # A sustained tone occupies a narrow range; a spoken utterance spans octaves.
        # The spread is what tells the two apart and is why each bank carries its own tolerance.
        self.assertLess(spread, 1.5, "a sustained tone should not span octaves, got "
                        + format(spread, ".2f"))


if __name__ == "__main__":
    unittest.main()
