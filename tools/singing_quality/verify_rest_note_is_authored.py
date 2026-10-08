"""Prove that a silent note in a render is an authored rest, not a synthesis defect.

Why this exists
---------------
Song-004 note 74 (frames 66000..78000) rendered at exactly zero amplitude while its
neighbours sang normally, and an earlier ledger entry read that silence as an
octave-and-a-fifth pitch error. It is neither. The note carries the phonetic hint
``pau`` and a lyric whose surface is literally ``pau``; ``pau`` maps to
``PhonemeRole::Silence`` in ``libs/seam-phonemizer/src/phonemizer.cpp:51``, so a
rest is *supposed* to be silent. The renderer was never asked to sing the note.

The claim is only worth making with a control, because this ledger has already
retracted four findings that were produced by instruments too weak to answer the
question asked. So this tool runs the real renderer twice on the same project and
changes exactly one thing -- the rest hint becomes a vowel -- keeping pitch,
timing, region, recipe and every other note identical. If the silence were a
pitch or register defect, the control would still be silent. If it is an authored
rest, the control sings the written pitch to within a few cents.

Usage
-----
    python3 -m tools.singing_quality.verify_rest_note_is_authored \
        --project tests/singing_quality/corpus/song-004/project.seam \
        --cli build/release/seam_voicebank_cli

The project must sit beside a ``recipes/`` directory naming its procedural recipe;
``seam_voicebank_cli bake-project`` resolves the recipe relative to the project.
Exits non-zero unless the rest is silent AND the control is audible AND the
control's strongest partial is the written pitch.
"""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

import numpy as np

if __package__:
    from .listener_packet_contract import make_q3_control
else:
    from listener_packet_contract import make_q3_control

SILENCE_RMS = 0.002
TRACK_INDEX = 0
REGION_INDEX = 0


def read_wav(path: pathlib.Path) -> tuple[np.ndarray, int]:
    """Read 16-bit PCM or 32-bit float WAV and downmix to mono."""
    blob = path.read_bytes()
    position, fmt, data = 12, None, None
    while position + 8 <= len(blob):
        chunk_id = blob[position:position + 4]
        size = struct.unpack_from("<I", blob, position + 4)[0]
        body = blob[position + 8:position + 8 + size]
        if chunk_id == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", body, 0)
        elif chunk_id == b"data":
            data = body
        position += 8 + size + (size & 1)
    if fmt is None or data is None:
        raise SystemExit(f"{path} is not a readable WAV")
    tag, channels, rate, _, _, bits = fmt
    if bits == 32 and tag == 3:
        flat = np.frombuffer(data, dtype="<f4").astype(np.float64)
    elif bits == 16 and tag == 1:
        flat = np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0
    else:
        raise SystemExit(f"unsupported WAV tag={tag} bits={bits}")
    if channels > 1:
        flat = flat.reshape(-1, channels).mean(axis=1)
    return flat, rate


def rest_notes(project: dict) -> list[dict]:
    """Return every note whose lyric surface is an authored rest marker."""
    region = project["vocalTracks"][TRACK_INDEX]["regions"][REGION_INDEX]
    surfaces = {item["id"]: item.get("surface") for item in region.get("lyrics", [])}
    found = []
    for note in region["notes"]:
        if note.get("phoneticHint") == "pau" or surfaces.get(note.get("lyricId")) == "pau":
            found.append(note)
    return found


def note_span(note: dict, rate: int) -> tuple[int, int]:
    ticks_per_second = 120.0 * 960.0
    per_second = rate * 60.0 / ticks_per_second
    return (int(note["startTick"] * per_second),
            int((note["startTick"] + note["durationTick"]) * per_second))


def segment_rms(mono: np.ndarray, rate: int, note: dict) -> float:
    start, end = note_span(note, rate)
    segment = mono[start:end]
    return float(np.sqrt((segment ** 2).mean())) if len(segment) else 0.0


def strongest_partial(mono: np.ndarray, rate: int, note: dict) -> float:
    start, end = note_span(note, rate)
    segment = mono[start:end]
    if len(segment) < 64:
        return 0.0
    segment = segment - segment.mean()
    size = 1
    while size < 8 * len(segment):
        size *= 2
    spectrum = np.abs(np.fft.rfft(segment * np.hanning(len(segment)), size))
    return float(np.fft.rfftfreq(size, 1.0 / rate)[int(np.argmax(spectrum))])


def midi_to_hz(midi: int) -> float:
    return 440.0 * (2.0 ** ((midi - 69) / 12.0))


def write_variant(project: dict, note: dict, destination: pathlib.Path) -> None:
    """Emit the project with exactly one change: the rest hint becomes a vowel."""
    variant = make_q3_control(project, note["id"])
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "project.seam").write_text(
        json.dumps(variant, ensure_ascii=False, indent=2))


def render(cli: pathlib.Path, project_dir: pathlib.Path, output: pathlib.Path) -> pathlib.Path:
    result = subprocess.run(
        [str(cli), "bake-project", str(project_dir / "project.seam"), str(output), "48000"],
        capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise SystemExit(f"bake-project failed: {result.stderr.strip()}")
    candidates = sorted(output.glob("candidates/*.wav"))
    if not candidates:
        raise SystemExit(f"bake-project produced no candidate audio in {output}")
    return candidates[0]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=pathlib.Path, required=True,
                        help="project.seam containing an authored pau rest")
    parser.add_argument("--cli", type=pathlib.Path, required=True,
                        help="path to seam_voicebank_cli")
    parser.add_argument("--workdir", type=pathlib.Path, default=None,
                        help="scratch directory (default: a temporary one)")
    arguments = parser.parse_args(argv)

    project = json.loads(arguments.project.read_text())
    notes = rest_notes(project)
    if not notes:
        print("FAIL: no authored pau rest found in the project")
        return 1
    note = notes[0]
    written = midi_to_hz(note["midiKey"])
    print(f"authored rest: note {note['id']} midi {note['midiKey']} "
          f"= {written:.2f} Hz, hint {note.get('phoneticHint')!r}")

    scratch = arguments.workdir or pathlib.Path(tempfile.mkdtemp(prefix="seam-rest-"))
    try:
        as_authored = scratch / "authored"
        shutil.copytree(arguments.project.parent, as_authored)
        as_rendered = render(arguments.cli, as_authored, scratch / "out-authored")
        write_variant(project, note, scratch / "control")
        shutil.copytree(arguments.project.parent / "recipes", scratch / "control" / "recipes")
        control_rendered = render(arguments.cli, scratch / "control", scratch / "out-control")

        authored_mono, authored_rate = read_wav(as_rendered)
        control_mono, control_rate = read_wav(control_rendered)
        authored_rms = segment_rms(authored_mono, authored_rate, note)
        control_rms = segment_rms(control_mono, control_rate, note)
        control_hz = strongest_partial(control_mono, control_rate, note)
        cents = 1200.0 * math.log2(control_hz / written) if control_hz > 0 else float("inf")
    finally:
        if arguments.workdir is None:
            shutil.rmtree(scratch, ignore_errors=True)

    print(f"  as authored (pau)      rms {authored_rms:.6f}")
    print(f"  control    (vowel)     rms {control_rms:.6f}")
    print(f"  control strongest partial {control_hz:.2f} Hz "
          f"({cents:+.1f} cents from written)")

    failures = []
    if authored_rms >= SILENCE_RMS:
        failures.append(f"the authored rest is not silent (rms {authored_rms:.6f})")
    if control_rms < SILENCE_RMS:
        failures.append(f"the control is also silent (rms {control_rms:.6f}), "
                        "so silence is not caused by the rest hint")
    if abs(cents) > 50.0:
        failures.append(f"the control does not sing the written pitch ({cents:+.1f} cents)")
    if failures:
        for item in failures:
            print(f"FAIL: {item}")
        return 1
    print("PASS: silence follows the authored rest, not the pitch; "
          "the same pitch sings correctly once it is voiced")
    return 0


if __name__ == "__main__":
    sys.exit(main())
