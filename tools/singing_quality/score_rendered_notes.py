"""Compare written notes with pitch frames from SEAM's native analysers.

This is a middle-third engineering diagnostic, not phonetic steady-span or
human listening acceptance. Autocorrelation can alias; an octave discrepancy
requires investigation, not automatic dismissal or a synthesis verdict.

Mono stems: --project song.seam --audio-dir stems --cli seam_voicebank_cli
Retained stereo analysis: --project song.seam --audio song.wav
    --analysis-json analysis.json --render-offset-ticks 960 --report result.json

Stem files are mapped lexicographically to vocal tracks; counts must match.
A master must have exactly one vocal track and no backing audio. All regions,
tempo changes and FixedAudio host placement are included. FollowHost needs its
captured host timeline and is refused here. Native features supply the sample
rate; --sample-rate is an optional consistency assertion. Frame centres select
the middle third; voiced frames need confidence >= 0.5 and an in-range F0.
The default 50-cent limit applies to each note median, not the Beta frame metric.
A report records source hashes and exclusions, but does not authenticate execution.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import statistics
import struct
import subprocess
import sys


def midi_to_hz(midi: int) -> float:
    return 440.0 * (2.0 ** ((midi - 69) / 12.0))


def frames_per_tick(project: dict, sample_rate: int) -> float:
    """Local frames per tick at the first tempo; placement uses seconds_at."""
    return sample_rate * 60.0 / (int(project["ppq"]) * float(project["tempoMap"][0]["bpm"]))


def integer(value, name: str, minimum: int | None = None) -> int:
    if type(value) is not int or (minimum is not None and value < minimum):
        raise ValueError(f"{name} must be an integer" + (f" >= {minimum}" if minimum is not None else ""))
    return value


def finite(value, name: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{name} must be finite numeric data")
    return float(value)


def tempo_points(project: dict) -> tuple[int, list[tuple[int, float]]]:
    ppq = integer(project["ppq"], "ppq", 1)
    points = [(integer(p["tick"], "tempo tick", 0), finite(p["bpm"], "bpm"))
              for p in project["tempoMap"]]
    if not points or points[0][0] != 0 or any(bpm <= 0 for _, bpm in points):
        raise ValueError("tempo map must start at tick zero with positive tempos")
    if any(a[0] >= b[0] for a, b in zip(points, points[1:])):
        raise ValueError("tempo ticks must be strictly increasing")
    return ppq, points


def seconds_at(tick: int, ppq: int, points: list[tuple[int, float]]) -> float:
    if tick < 0:
        return tick * 60.0 / (ppq * points[0][1])
    seconds = 0.0
    for index, (start, bpm) in enumerate(points):
        if start >= tick:
            break
        end = min(tick, points[index + 1][0]) if index + 1 < len(points) else tick
        seconds += (end - start) * 60.0 / (ppq * bpm)
    return seconds


def extract_features(cli: pathlib.Path, wav: pathlib.Path) -> dict:
    result = subprocess.run([str(cli), "extract-pitch", str(wav)], capture_output=True,
                            text=True, check=False, timeout=120)
    if result.returncode != 0:
        raise ValueError(f"extract-pitch failed for {wav}: {result.stderr.strip()}")
    return json.loads(result.stdout)


def wav_geometry(raw: bytes) -> tuple[int, int, int]:
    """Read uncompressed RIFF geometry without decoding or trusting analysis JSON."""
    if len(raw) < 44 or raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError("pitch source must be RIFF/WAVE")
    if struct.unpack_from("<I", raw, 4)[0] + 8 != len(raw):
        raise ValueError("WAV RIFF size does not match held bytes")
    position, chunks = 12, {}
    while position < len(raw):
        if position + 8 > len(raw):
            raise ValueError("truncated WAV chunk header")
        name, size = struct.unpack_from("<4sI", raw, position)
        start, end = position + 8, position + 8 + size
        if end + size % 2 > len(raw):
            raise ValueError("truncated WAV chunk or padding")
        if name in (b"fmt ", b"data"):
            if name in chunks:
                raise ValueError("duplicate WAV format/data chunk")
            chunks[name] = (start, size)
        position = end + size % 2
    if b"fmt " not in chunks or b"data" not in chunks or chunks[b"fmt "][1] < 16:
        raise ValueError("WAV needs format and sample data")
    start, size = chunks[b"fmt "]
    fmt = raw[start:start + size]
    encoding, channels, rate, byte_rate, alignment, bits = struct.unpack_from("<HHIIHH", fmt)
    if encoding == 0xfffe:
        if (len(fmt) < 40 or struct.unpack_from("<H", fmt, 16)[0] < 22
                or len(fmt) < 18 + struct.unpack_from("<H", fmt, 16)[0]
                or fmt[26:40] != b"\0\0\0\0\x10\0\x80\0\0\xaa\0\x38\x9b\x71"):
            raise ValueError("unsupported or truncated extensible WAV format")
        encoding = struct.unpack_from("<H", fmt, 24)[0]
    if ((encoding == 1 and bits not in (8, 16, 24, 32))
            or (encoding == 3 and bits != 32) or encoding not in (1, 3)):
        raise ValueError("pitch source needs PCM8/16/24/32 or Float32 WAV")
    if (not 1 <= channels <= 8 or not 8000 <= rate <= 384000
            or alignment != channels * (bits // 8) or byte_rate != rate * alignment):
        raise ValueError("invalid WAV rate/channels/block geometry")
    data_size = chunks[b"data"][1]
    if data_size == 0 or data_size % alignment:
        raise ValueError("WAV needs nonempty complete audio frames")
    return rate, channels, data_size // alignment


def feature_metadata(features: dict, wav: pathlib.Path) -> dict:
    if not isinstance(features, dict):
        raise ValueError("native pitch features must be an object")
    integer(features["schemaVersion"], "schemaVersion", 1)
    raw = wav.read_bytes()
    if features["sourceSha256"] != hashlib.sha256(raw).hexdigest():
        raise ValueError(f"analysis source hash does not match {wav}")
    wav_rate, wav_channels, wav_frames = wav_geometry(raw)
    rate = integer(features["sampleRate"], "sampleRate", 1)
    if (features["formatId"], features["schemaVersion"]) == ("com.project-seam.training-pitch-features", 1):
        meta = {"algorithm": features["algorithm"], "coverage": features["coverage"],
                "windowFrames": features["windowFrames"], "hopFrames": features["hopSize"],
                "minimumHz": features["minimumHz"], "maximumHz": features["maximumHz"]}
        extent = integer(features["frameCount"], "frameCount", 1)
        meta["channels"] = 1  # Native extract-pitch admits only mono sources.
        expected = ("fft-autocorrelation-v3", "full-hop-zero-padded")
    elif (features["formatId"], features["schemaVersion"]) == ("com.project-seam.voicebank-analysis", 2):
        meta = dict(features["pitchAnalysis"])
        meta["channels"] = integer(features["channels"], "channels", 1)
        extent = integer(features["frames"], "frames", 1)
        expected = ("direct-autocorrelation-v3", "CompleteWindows")
    else:
        raise ValueError("unsupported native pitch feature format/version")
    if rate != wav_rate or meta["channels"] != wav_channels or extent != wav_frames:
        raise ValueError("native analysis rate/channels/frame extent does not match the hashed WAV")
    if (meta["algorithm"], meta["coverage"]) != expected:
        raise ValueError("unsupported native pitch algorithm/coverage")
    window = integer(meta["windowFrames"], "windowFrames", 1)
    hop = integer(meta["hopFrames"], "hopFrames", 1)
    minimum = finite(meta["minimumHz"], "minimumHz")
    maximum = finite(meta["maximumHz"], "maximumHz")
    if not 0 < minimum < maximum or window > extent:
        raise ValueError("invalid pitch range/window extent")
    stop = extent if expected[1] == "full-hop-zero-padded" else extent - window + 1
    count = len(range(0, stop, hop))
    frames = features["pitchFrames"]
    if len(frames) != count:
        raise ValueError("pitch frames do not cover the declared source extent")
    for index, frame in enumerate(frames):
        if integer(frame["sourceFrame"], "sourceFrame", 0) != index * hop:
            raise ValueError("pitch sourceFrame sequence does not match the declared grid")
        confidence = finite(frame["confidence"], "confidence")
        f0 = finite(frame["f0Hz"], "f0Hz")
        if type(frame["voiced"]) is not bool or not 0 <= confidence <= 1 or f0 < 0:
            raise ValueError("invalid voiced/confidence/F0 record")
    return {**meta, "sampleRate": rate, "frameCount": extent}


def note_window(frames: list[dict], hop: int, start: float, end: float,
                window: int = 0) -> list[dict]:
    low, high = start + (end - start) / 3.0, end - (end - start) / 3.0
    if frames and "sourceFrame" not in frames[0]:
        # Compatibility for the original pure helper tests; CLI protocols always
        # require explicit source positions and a declared analysis window.
        return frames[int(low / hop):int(high / hop)]
    return [f for f in frames if low <= f["sourceFrame"] + window / 2.0 < high]


def usable(frame: dict, minimum: float = 0, maximum: float = math.inf) -> bool:
    return (frame.get("voiced", False) and frame.get("confidence", 1.0) >= 0.5
            and math.isfinite(frame["f0Hz"]) and minimum <= frame["f0Hz"] < maximum
            and frame["f0Hz"] > 0)


def score_note(frames: list[dict], hop: int, start_frame: float,
               end_frame: float, window: int = 0) -> tuple[float | None, int]:
    values = [f["f0Hz"] for f in note_window(frames, hop, start_frame, end_frame, window) if usable(f)]
    return (statistics.median(values), len(values)) if values else (None, 0)


def projected_notes(project: dict, track: dict, rate: int, render_offset: int) -> list[dict]:
    ppq, points = tempo_points(project)
    settings = project.get("settings", {})
    if settings.get("bounceTimingAuthority", "fixed-audio") != "fixed-audio":
        raise ValueError("FollowHost requires a captured host timeline; document tempo is insufficient")
    offset = seconds_at(integer(settings.get("hostStartOffsetTick", 0), "hostStartOffsetTick", 0), ppq, points)
    notes = []
    for region_index, region in enumerate(track["regions"]):
        region_start = integer(region["startTick"], "region startTick", 0)
        for note in region["notes"]:
            start_tick = region_start + render_offset + integer(note["startTick"], "note startTick", 0)
            duration = integer(note["durationTick"], "note durationTick", 1)
            midi = integer(note["midiKey"], "midiKey", 0)
            if midi > 127:
                raise ValueError("midiKey must be <= 127")
            start = (seconds_at(start_tick, ppq, points) + offset) * rate
            end = (seconds_at(start_tick + duration, ppq, points) + offset) * rate
            if not math.isfinite(start) or not math.isfinite(end) or start < 0 or end <= start:
                raise ValueError("note placement is outside the supported audio timeline")
            notes.append({"noteId": note.get("id"), "regionIndex": region_index, "midiKey": midi,
                          "startFrame": start, "endFrame": end})
    notes.sort(key=lambda n: n["startFrame"])
    if not notes or any(a["endFrame"] > b["startFrame"] + 1e-6 for a, b in zip(notes, notes[1:])):
        raise ValueError("scalar F0 scoring requires nonempty, nonoverlapping notes")
    return notes


def evaluate(arguments) -> dict:
    project_bytes = arguments.project.read_bytes()
    project = json.loads(project_bytes)
    tracks = project["vocalTracks"]
    if arguments.audio is not None:
        if arguments.analysis_json is None or arguments.audio_dir is not None or arguments.cli is not None:
            raise ValueError("--audio requires --analysis-json and excludes --audio-dir/--cli")
        if len(tracks) != 1 or project.get("audioTracks"):
            raise ValueError("master F0 scoring requires exactly one vocal track and no backing audio")
        wavs = [arguments.audio]
    else:
        if arguments.audio_dir is None or arguments.cli is None or arguments.analysis_json is not None:
            raise ValueError("stem mode requires --audio-dir and --cli, without --analysis-json")
        wavs = sorted(arguments.audio_dir.rglob("*.wav"))
        if not tracks or len(wavs) != len(tracks):
            raise ValueError("stem file count must exactly match vocal track count")
    analysis_bytes = arguments.analysis_json.read_bytes() if arguments.analysis_json else None
    rows, sources, all_frame_errors = [], [], []
    for index, wav in enumerate(wavs):
        features = (json.loads(analysis_bytes) if arguments.audio is not None
                    else extract_features(arguments.cli, wav))
        if not isinstance(features, dict):
            raise ValueError("native pitch features must be an object")
        if arguments.audio is not None and features.get("formatId") != "com.project-seam.voicebank-analysis":
            raise ValueError("--analysis-json requires native analyse output, not mono training features")
        meta = feature_metadata(features, wav)
        if arguments.sample_rate is not None and arguments.sample_rate != meta["sampleRate"]:
            raise ValueError("--sample-rate does not match native analysis sampleRate")
        notes = projected_notes(project, tracks[index], meta["sampleRate"], arguments.render_offset_ticks)
        sources.append({"audio": str(wav.resolve()), "audioSha256": features["sourceSha256"],
                        "trackIndex": index, "trackId": tracks[index].get("id"), **meta})
        for note in notes:
            if note["endFrame"] > meta["frameCount"] + 1e-6:
                raise ValueError("audio ends before a written note ends")
            candidates = note_window(features["pitchFrames"], meta["hopFrames"],
                                     note["startFrame"], note["endFrame"], meta["windowFrames"])
            selected = [f for f in candidates if usable(f, meta["minimumHz"], meta["maximumHz"])]
            excluded = {"unvoiced": 0, "lowConfidence": 0, "outOfRange": 0}
            for frame in candidates:
                if not frame["voiced"]:
                    excluded["unvoiced"] += 1
                elif frame["confidence"] < 0.5:
                    excluded["lowConfidence"] += 1
                elif not meta["minimumHz"] <= frame["f0Hz"] < meta["maximumHz"] or frame["f0Hz"] <= 0:
                    excluded["outOfRange"] += 1
            hz = statistics.median(f["f0Hz"] for f in selected) if selected else None
            written = midi_to_hz(note["midiKey"])
            signed = 1200.0 * math.log2(hz / written) if hz is not None else None
            frame_errors = [abs(1200.0 * math.log2(f["f0Hz"] / written)) for f in selected]
            all_frame_errors.extend(frame_errors)
            rows.append({**note, "trackIndex": index, "writtenHz": written, "medianHz": hz,
                         "signedMedianCents": signed, "windowFrameCount": len(candidates),
                         "selectedFrameCount": len(selected), "excludedFrames": excluded,
                         "medianAbsoluteFrameCents": statistics.median(frame_errors) if selected else None,
                         "framesWithin50Cents": sum(e <= 50 for e in frame_errors),
                         "pass": signed is not None and abs(signed) <= arguments.tolerance_cents})
    return {"formatId": "com.project-seam.note-pitch-diagnostic", "schemaVersion": 1,
            "scope": "middle-third engineering diagnostic; not Beta or human listening acceptance",
            "project": str(arguments.project.resolve()), "projectSha256": hashlib.sha256(project_bytes).hexdigest(),
            "analysisSha256": hashlib.sha256(analysis_bytes).hexdigest() if analysis_bytes is not None else None,
            "renderOffsetTicks": arguments.render_offset_ticks, "confidenceMinimum": 0.5,
            "noteMedianToleranceCents": arguments.tolerance_cents, "sources": sources, "notes": rows,
            "summary": {"noteCount": len(rows), "measuredNoteCount": sum(n["medianHz"] is not None for n in rows),
                        "passedNoteCount": sum(n["pass"] for n in rows),
                        "selectedFrameCount": len(all_frame_errors),
                        "medianAbsoluteFrameCents": statistics.median(all_frame_errors) if all_frame_errors else None,
                        "framesWithin50Cents": sum(e <= 50 for e in all_frame_errors),
                        "maximumAbsoluteFrameCents": max(all_frame_errors, default=None),
                        "maximumAbsoluteNoteMedianCents": max((abs(n["signedMedianCents"]) for n in rows if n["medianHz"] is not None), default=None)},
            "pass": bool(rows) and all(n["pass"] for n in rows)}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=pathlib.Path, required=True)
    parser.add_argument("--audio-dir", type=pathlib.Path)
    parser.add_argument("--cli", type=pathlib.Path)
    parser.add_argument("--audio", type=pathlib.Path)
    parser.add_argument("--analysis-json", type=pathlib.Path)
    parser.add_argument("--sample-rate", type=int)
    parser.add_argument("--render-offset-ticks", type=int, default=0)
    parser.add_argument("--tolerance-cents", type=float, default=50.0)
    parser.add_argument("--report", type=pathlib.Path)
    arguments = parser.parse_args(argv)
    try:
        if arguments.render_offset_ticks < 0 or not math.isfinite(arguments.tolerance_cents) or arguments.tolerance_cents < 0:
            raise ValueError("render offset and finite tolerance must be nonnegative")
        report = evaluate(arguments)
        if arguments.report is not None:
            # Exclusive creation also refuses source aliases and retained reports.
            with arguments.report.open("x", encoding="utf-8") as output:
                json.dump(report, output, indent=2, allow_nan=False)
                output.write("\n")
    except (ValueError, KeyError, TypeError, OSError, subprocess.TimeoutExpired) as error:
        print(f"REFUSED: {error}")
        return 2
    for note in report["notes"]:
        value = note["signedMedianCents"]
        measured = f"{value:+.3f} cents" if value is not None else "no usable voiced frames"
        print(f"track {note['trackIndex']} region {note['regionIndex']} note {note['noteId']} MIDI {note['midiKey']}: {measured} ({note['selectedFrameCount']} frames)")
    print(json.dumps(report["summary"], sort_keys=True))
    print("PASS: every note median is within tolerance" if report["pass"] else "FAIL: unmeasured or out-of-tolerance notes")
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
