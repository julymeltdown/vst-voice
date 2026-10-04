"""Score a rendered project's notes with SEAM's shipped pitch extractor.

Why this exists
---------------
Measuring note pitch by hand went wrong five separate ways in this project, each
time producing a confident false finding: wrong tick-to-frame arithmetic,
windows that landed on silence, onset detectors that merged legato notes, and a
reimplemented tracker that was simply less accurate than the shipped one. An
exact 1200-cent "octave error" is nearly always an analysis artefact.

This tool removes the two mistakes that actually happened:

- frames per tick is ``sampleRate / (ppq * bpm / 60)``. Its reciprocal makes
  every window sub-hop wide, which yields one or two frames and pure noise.
- note spans come from the project, not from assumed layout, and the sampled
  window is the middle third of the note so the attack transient is excluded.

The pitch reading itself is delegated to ``seam_voicebank_cli extract-pitch``,
which this project already proved correct to a quarter of a cent on a
five-song corpus. Nothing here re-derives a pitch estimate.

Usage
-----
    python3 -m tools.singing_quality.score_rendered_notes \
        --project path/to/project.seam \
        --audio-dir path/to/render/candidates \
        --cli build/release/seam_voicebank_cli

Exits non-zero when any note is more than --tolerance-cents from its written
pitch, so a regression fails instead of waiting to be noticed by ear.
"""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import subprocess
import sys

TRACK_INDEX = 0
REGION_INDEX = 0


def midi_to_hz(midi: int) -> float:
    return 440.0 * (2.0 ** ((midi - 69) / 12.0))


def frames_per_tick(project: dict, sample_rate: int) -> float:
    """Frames per project tick.

    tickRate = ppq * bpm / 60, so framesPerTick = sampleRate / tickRate. This
    is the quantity the earlier hand-written scorer inverted.
    """
    ppq = int(project["ppq"])
    tempo = project["tempoMap"][0]
    return sample_rate / (ppq * float(tempo["bpm"]) / 60.0)


def extract_features(cli: pathlib.Path, wav: pathlib.Path) -> dict:
    result = subprocess.run([str(cli), "extract-pitch", str(wav)],
                            capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise SystemExit(f"extract-pitch failed for {wav}: {result.stderr.strip()}")
    return json.loads(result.stdout)


def score_note(frames: list[dict], hop: int, start_frame: float,
               end_frame: float) -> tuple[float | None, int]:
    """Median voiced f0 over the middle third of the note, in hertz."""
    margin = (end_frame - start_frame) / 3.0
    low = int((start_frame + margin) / hop)
    high = int((end_frame - margin) / hop)
    window = [item for item in frames[low:high] if item.get("voiced")]
    if not window:
        return None, 0
    values = sorted(item["f0Hz"] for item in window)
    return values[len(values) // 2], len(window)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=pathlib.Path, required=True)
    parser.add_argument("--audio-dir", type=pathlib.Path, required=True)
    parser.add_argument("--cli", type=pathlib.Path, required=True)
    parser.add_argument("--sample-rate", type=int, default=48000)
    parser.add_argument("--tolerance-cents", type=float, default=50.0)
    arguments = parser.parse_args(argv)

    project = json.loads(arguments.project.read_text())
    per_tick = frames_per_tick(project, arguments.sample_rate)
    wavs = sorted(arguments.audio_dir.rglob("*.wav"))
    if not wavs:
        print(f"FAIL: no audio found under {arguments.audio_dir}")
        return 1

    tracks = project["vocalTracks"]
    if len(wavs) != len(tracks):
        print(f"note: {len(wavs)} audio file(s) for {len(tracks)} track(s); "
              "scoring them in order")

    errors: list[float] = []
    failures: list[str] = []
    for index, wav in enumerate(wavs):
        if index >= len(tracks):
            break
        features = extract_features(arguments.cli, wav)
        hop = int(features["hopSize"])
        frames = features["pitchFrames"]
        region = tracks[index]["regions"][REGION_INDEX]
        notes = sorted(region["notes"], key=lambda note: note["startTick"])
        print(f"--- {wav.name} : {len(notes)} note(s) ---")
        for position, note in enumerate(notes):
            start = note["startTick"] * per_tick
            end = (note["startTick"] + note["durationTick"]) * per_tick
            median, count = score_note(frames, hop, start, end)
            if median is None:
                failures.append(f"note {position} (MIDI {note['midiKey']}) has no voiced frames")
                print(f"  note {position} MIDI {note['midiKey']}: no voiced frames")
                continue
            written = midi_to_hz(note["midiKey"])
            cents = 1200.0 * math.log2(median / written)
            errors.append(abs(cents))
            flag = "" if abs(cents) <= arguments.tolerance_cents else "  OUT OF TOLERANCE"
            print(f"  note {position} MIDI {note['midiKey']:2d}  written {written:7.2f} Hz  "
                  f"median {median:7.2f} Hz  {cents:+6.2f} cents  ({count} frames){flag}")
            if abs(cents) > arguments.tolerance_cents:
                failures.append(f"note {position} (MIDI {note['midiKey']}) is {cents:+.2f} cents")

    if not errors:
        print("FAIL: no note was measured")
        return 1
    ordered = sorted(errors)
    print()
    print(f"median |error|: {ordered[len(ordered) // 2]:.3f} cents")
    print(f"max |error|:    {max(errors):.3f} cents")
    print(f"within {arguments.tolerance_cents:g}c:     "
          f"{sum(1 for value in errors if value <= arguments.tolerance_cents)} of {len(errors)}")
    if failures:
        for item in failures:
            print(f"FAIL: {item}")
        return 1
    print("PASS: every note is within tolerance of its written pitch")
    return 0


if __name__ == "__main__":
    sys.exit(main())
