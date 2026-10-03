"""A bank unit's declared rootMidi must match the pitch its audio actually has.

The renderer treats `rootMidi` as the pitch of the recording: it resamples each
placement by `targetHz / rootHz` (`libs/seam-synthesis/src/spectral_classic.cpp`).
A unit whose declared root does not match its audio therefore renders every note
transposed by a constant amount, and every pitch-accuracy figure measured against
the score describes that offset rather than the renderer.

That is not hypothetical. The demo bank's recording is a 0.55-second SPOKEN
utterance whose median pitch inside its declared loop span is 695.4 Hz while all
eight of its units declare `rootMidi: 67` (392.0 Hz), a 992-cent discrepancy.
Recorded in
`docs/implementation/LISTENING_PACKET_ROOT_MIDI_MISMATCH_2026-10-03.md`.

This guard is deliberately a REPORT, not a gate that fails the build. The demo
bank is a known-unsuitable technical fixture by its own README and provenance, so
failing on it would make the mismatch permanently invisible by being permanently
red. What the guard does is make the measured offset an explicit, checked fact so
that no intonation number is quoted from this bank without it.
"""

from __future__ import annotations

import json
import math
from pathlib import Path
import shutil
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
BANK = ROOT / "assets/demo-human-voicebank-public-domain/production-bank"
MANIFEST = BANK / "manifest.json"

# A unit whose audio is a sustained vowel can be measured to within 50 cents. The demo
# bank's audio is a spoken utterance whose frames scatter across several octaves, so a
# loose bound is the honest one here; see the module docstring.
SPOKEN_TOLERANCE_CENTS = 1200.0


def _extractor() -> str | None:
    for candidate in ("build/release/seam_voicebank_cli", "build/dev/seam_voicebank_cli"):
        path = ROOT / candidate
        if path.exists():
            return str(path)
    return shutil.which("seam_voicebank_cli")


class BankRootPitchTest(unittest.TestCase):
    def test_manifest_units_declare_a_consistent_root(self) -> None:
        """Every unit in the bank declares the same root, so one measurement describes them all."""
        manifest = json.loads(MANIFEST.read_text())
        roots = {unit.get("rootMidi") for unit in manifest["units"]}
        self.assertEqual(len(roots), 1, f"units declare differing roots: {sorted(roots)}")
        self.assertIsNotNone(manifest["units"][0].get("rootMidi"), "a unit declares no rootMidi")

    def test_declared_root_is_measured_against_the_unit_audio(self) -> None:
        """The declared root and the audio's own pitch are reported together, always.

        The point of this case is that the comparison exists and runs, not that it
        passes: the demo bank's offset is large and known, and a guard that skipped
        the measurement would let the next reader assume the manifest is right.
        """
        extractor = _extractor()
        if extractor is None:
            self.skipTest("seam_voicebank_cli is not built")
        manifest = json.loads(MANIFEST.read_text())
        unit = manifest["units"][0]
        audio = BANK / unit["audio"]
        result = subprocess.run([extractor, "extract-pitch", str(audio)],
                                capture_output=True, text=True, check=True)
        frames = json.loads(result.stdout)["pitchFrames"]
        loop_start = unit.get("loopStartFrame")
        loop_end = unit.get("loopEndFrame")
        inside = [f for f in frames
                  if f["voiced"] and f["f0Hz"] > 0
                  and (loop_start is None or f["sourceFrame"] >= loop_start)
                  and (loop_end is None or f["sourceFrame"] < loop_end)]
        self.assertTrue(inside, "the unit's loop span holds no voiced pitch to measure")
        measured = sorted(f["f0Hz"] for f in inside)
        median_hz = measured[len(measured) // 2]
        declared_hz = 440.0 * (2.0 ** ((unit["rootMidi"] - 69) / 12.0))
        cents = 1200 * math.log2(median_hz / declared_hz)

        # A spoken utterance has no single pitch, so its spread is the honest statement
        # of why the declared root cannot be verified against it at all.
        spread = measured[-1] / max(measured[0], 1e-9)
        self.assertGreater(
            spread, 2.0,
            "a unit whose audio spans less than an octave could be checked tightly; "
            "this one does not, which is the finding")
        self.assertGreater(
            abs(cents), SPOKEN_TOLERANCE_CENTS,
            f"declared rootMidi {unit['rootMidi']} vs measured {median_hz:.1f} Hz "
            f"({cents:+.0f} cents) moved inside the spoken tolerance; if the fixture was "
            "replaced with a real sustained vowel, tighten this bound instead of widening it")


if __name__ == "__main__":
    unittest.main()
