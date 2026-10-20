#!/usr/bin/env python3
"""Build an engineering-only Japanese sample bank from the PJS 1.1 singing corpus.

The bank exists so the shipped renderer can sing Japanese lyrics with a human-recorded
voice before a rights-cleared production bank exists. It is not a release resource:
PJS and its manual relabel are CC BY-SA 4.0, which conflicts with the product EULA's
ban on bank redistribution. The output carries that licence, an attribution notice and
per-unit provenance, and its identity starts with "engineering.".

Inputs are the unpacked corpus, the pinned manual HTK labels (100 ns units) and the
engine's own pitch extractor (seam_voicebank_cli extract-pitch). Units are copied from
the original PCM without resampling. Each consonant+vowel pair becomes a "cv" unit and
each vowel or moraic nasal that follows silence or another vowel becomes a "sustain"
unit, the two unit shapes the timing solver treats as simple.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import subprocess
import sys
import wave
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

BANK_ID = "engineering.pjs.male.01"
BANK_VERSION = "0.1.0-engineering"
DISPLAY_NAME = "PJS Engineering Male (CC BY-SA 4.0, not for release)"
SAMPLE_RATE = 48000
LABEL_UNITS_PER_SECOND = 10_000_000

VOWELS = {"a", "i", "u", "e", "o"}
# PJS marks devoiced vowels in upper case and uses pau0 for a short pause; the editor's
# Japanese phonemizer has neither, so they map onto its symbols.
PHONE_MAP = {"I": "i", "U": "u", "O": "o", "E": "e", "A": "a", "pau0": "pau"}
SILENCES = {"pau", "sil", "br", "cl"}
SUSTAINABLE = VOWELS | {"N"}

MIN_SUSTAIN_FRAMES = int(0.12 * SAMPLE_RATE)   # shorter vowels cannot hold a loop
MIN_LOOP_FRAMES = int(0.05 * SAMPLE_RATE)
EDGE_MIN_FRAMES = int(0.03 * SAMPLE_RATE)
EDGE_MAX_FRAMES = int(0.10 * SAMPLE_RATE)
PRE_ROLL_AFTER_SILENCE = int(0.015 * SAMPLE_RATE)
PRE_ROLL_AFTER_VOICE = int(0.005 * SAMPLE_RATE)
FADE_IN_FRAMES = int(0.003 * SAMPLE_RATE)
FADE_OUT_FRAMES = int(0.008 * SAMPLE_RATE)
LABEL_TOLERANCE_FRAMES = int(0.01 * SAMPLE_RATE)
TARGET_RMS = 0.1
MAX_PITCH_SPREAD_SEMITONES = 0.5
MIN_VOICED_FRACTION = 0.7
MIN_PITCH_CONFIDENCE = 0.5
CLIP_FRACTION = 0.9999  # the engine validator counts |x| >= 0.9999 of full scale as clipped


class IntakeError(Exception):
    """A refusal: the inputs do not support a trustworthy unit."""


@dataclass(frozen=True)
class Phone:
    symbol: str
    source_symbol: str
    start: int
    end: int


@dataclass
class Candidate:
    song: str
    alias: str
    phones: list[str]
    kind: str
    cut_start: int
    cut_end: int
    markers: dict[str, int]
    root_midi: int = 0
    cents_off: float = 0.0
    spread: float = 0.0
    gain_db: float = 0.0
    source_symbols: list[str] = field(default_factory=list)

    def score(self) -> float:
        loop = (self.markers["loopEnd"] - self.markers["loopStart"]) / SAMPLE_RATE
        return self.spread + abs(self.cents_off) / 100.0 - min(loop, 1.0) * 0.2


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def label_time_to_frame(value: int) -> int:
    return (value * SAMPLE_RATE + LABEL_UNITS_PER_SECOND // 2) // LABEL_UNITS_PER_SECOND


def parse_labels(text: str, total_frames: int) -> list[Phone]:
    phones: list[Phone] = []
    previous_end = 0
    for number, line in enumerate(text.splitlines(), start=1):
        if not line.strip():
            continue
        parts = line.split()
        if len(parts) != 3:
            raise IntakeError(f"label line {number} must be 'start end phone'")
        try:
            start_units, end_units = int(parts[0]), int(parts[1])
        except ValueError as error:
            raise IntakeError(f"label line {number} has non-integer times") from error
        if start_units < 0 or end_units <= start_units:
            raise IntakeError(f"label line {number} has an empty or negative interval")
        start, end = label_time_to_frame(start_units), label_time_to_frame(end_units)
        if start < previous_end - LABEL_TOLERANCE_FRAMES:
            raise IntakeError(f"label line {number} overlaps the previous label")
        if end > total_frames + LABEL_TOLERANCE_FRAMES:
            raise IntakeError(f"label line {number} ends after the audio")
        source = parts[2]
        phones.append(Phone(PHONE_MAP.get(source, source), source, start, min(end, total_frames)))
        previous_end = end
    if not phones:
        raise IntakeError("label file has no phones")
    return phones


def read_mono_pcm(path: Path) -> tuple[np.ndarray, int]:
    with wave.open(str(path), "rb") as handle:
        if handle.getnchannels() != 1 or handle.getframerate() != SAMPLE_RATE:
            raise IntakeError(f"{path.name} must be mono {SAMPLE_RATE} Hz")
        width = handle.getsampwidth()
        if width not in (2, 3):
            raise IntakeError(f"{path.name} must be 16- or 24-bit PCM")
        raw = handle.readframes(handle.getnframes())
    if width == 2:
        return np.frombuffer(raw, dtype="<i2").astype(np.int32), width
    bytes3 = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
    values = bytes3[:, 0] | (bytes3[:, 1] << 8) | (bytes3[:, 2] << 16)
    values = np.where(values >= 1 << 23, values - (1 << 24), values)
    return values, width


def write_mono_pcm(path: Path, samples: np.ndarray, width: int) -> None:
    """Write canonical RIFF/WAVE PCM, including the pad byte that Python's wave module omits."""
    limit = (1 << (8 * width - 1)) - 1
    clipped = np.clip(np.rint(samples), -limit - 1, limit).astype(np.int32)
    if width == 2:
        data = clipped.astype("<i2").tobytes()
    else:
        as_unsigned = (clipped & 0xFFFFFF).astype(np.uint32)
        data = np.stack([(as_unsigned >> shift) & 0xFF for shift in (0, 8, 16)], axis=1)
        data = data.astype(np.uint8).tobytes()
    pad = b"\x00" if len(data) % 2 else b""
    fmt = struct.pack("<HHIIHH", 1, 1, SAMPLE_RATE, SAMPLE_RATE * width, width, 8 * width)
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
    body += b"data" + struct.pack("<I", len(data)) + data + pad
    path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)


def edge(length: int) -> int:
    return max(EDGE_MIN_FRAMES, min(EDGE_MAX_FRAMES, int(0.15 * length)))


def propose_units(song: str, phones: list[Phone]) -> list[Candidate]:
    """Find cv and sustain spans whose sustained part is long enough to loop."""
    candidates: list[Candidate] = []
    for index, phone in enumerate(phones):
        if phone.symbol not in SUSTAINABLE:
            continue
        length = phone.end - phone.start
        if length < MIN_SUSTAIN_FRAMES:
            continue
        previous = phones[index - 1] if index > 0 else None
        consonant = None
        if (phone.symbol in VOWELS and previous is not None and previous.symbol not in SILENCES
                and previous.symbol not in SUSTAINABLE and previous.end >= phone.start - 1):
            consonant = previous
        onset = consonant.start if consonant else phone.start
        before = phones[index - 2] if consonant and index > 1 else previous
        voiced_before = before is not None and before.symbol not in SILENCES
        pre_roll = PRE_ROLL_AFTER_VOICE if voiced_before else PRE_ROLL_AFTER_SILENCE
        cut_start = max(0, onset - pre_roll)
        cut_end = phone.end
        vowel_onset = phone.start - cut_start
        audio_end = cut_end - cut_start
        stable_start = vowel_onset + edge(length)
        release_start = audio_end - edge(length)
        if release_start - stable_start < MIN_LOOP_FRAMES:
            continue
        markers = {
            "audioOffset": 0,
            "consonantEnd": (consonant.end - cut_start) if consonant else vowel_onset,
            "vowelOnset": vowel_onset,
            "stableStart": stable_start,
            "loopStart": stable_start,
            "loopEnd": release_start,
            "releaseStart": release_start,
            "audioEnd": audio_end,
        }
        if consonant:
            symbols = [consonant.symbol, phone.symbol]
            sources = [consonant.source_symbol, phone.source_symbol]
            kind = "cv"
        else:
            symbols = [phone.symbol]
            sources = [phone.source_symbol]
            kind = "sustain"
        candidates.append(Candidate(song=song, alias=" ".join(symbols), phones=symbols, kind=kind,
                                    cut_start=cut_start, cut_end=cut_end, markers=markers,
                                    source_symbols=sources))
    return candidates


def load_pitch(pitch_tool: Path, wav: Path) -> tuple[list[dict], str]:
    completed = subprocess.run([str(pitch_tool), "extract-pitch", str(wav)], capture_output=True,
                               text=True, check=False)
    if completed.returncode != 0:
        raise IntakeError(f"pitch extraction failed for {wav.name}: {completed.stderr.strip()}")
    document = json.loads(completed.stdout)
    if document.get("sampleRate") != SAMPLE_RATE:
        raise IntakeError(f"pitch extraction for {wav.name} reports another sample rate")
    return document["pitchFrames"], document.get("sourceSha256", "")


def measure_pitch(candidate: Candidate, frames: list[dict]) -> bool:
    start = candidate.cut_start + candidate.markers["loopStart"]
    end = candidate.cut_start + candidate.markers["loopEnd"]
    window = [frame for frame in frames if start <= frame["sourceFrame"] < end]
    if not window:
        return False
    voiced = [frame["f0Hz"] for frame in window
              if frame["voiced"] and frame["confidence"] >= MIN_PITCH_CONFIDENCE and frame["f0Hz"] > 0]
    if len(voiced) < MIN_VOICED_FRACTION * len(window):
        return False
    semitones = 69.0 + 12.0 * np.log2(np.asarray(voiced, dtype=np.float64) / 440.0)
    median = float(np.median(semitones))
    spread = float(np.median(np.abs(semitones - median)))
    if spread > MAX_PITCH_SPREAD_SEMITONES:
        return False
    candidate.root_midi = int(round(median))
    candidate.cents_off = (median - candidate.root_midi) * 100.0
    candidate.spread = spread
    return 0 <= candidate.root_midi <= 127


def snap_loop(candidate: Candidate, cut: np.ndarray) -> None:
    """Make the loop a whole number of pitch periods and match its ends."""
    period = SAMPLE_RATE / (440.0 * 2.0 ** ((candidate.root_midi + candidate.cents_off / 100.0 - 69) / 12))
    start = candidate.markers["loopStart"]
    end = candidate.markers["loopEnd"]
    periods = max(1, int((end - start) / period))
    target = int(round(start + periods * period))
    span = max(1, int(period / 2))
    probe = 32
    reference = cut[start:start + probe]
    best, best_error = target, math.inf
    for position in range(max(start + 1, target - span), min(len(cut) - probe, target + span) + 1):
        if position > end:
            break
        error = float(np.sum((cut[position:position + probe] - reference) ** 2))
        if error < best_error:
            best, best_error = position, error
    if best - start >= MIN_LOOP_FRAMES // 2:
        candidate.markers["loopEnd"] = best
        candidate.markers["releaseStart"] = max(candidate.markers["releaseStart"], best)


def shape_and_level(candidate: Candidate, cut: np.ndarray, full_scale: float) -> np.ndarray:
    shaped = cut.astype(np.float64)
    fade_in = min(FADE_IN_FRAMES, candidate.markers["vowelOnset"], len(shaped))
    if fade_in > 0:
        shaped[:fade_in] *= np.linspace(0.0, 1.0, fade_in, endpoint=False)
    fade_out = min(FADE_OUT_FRAMES, len(shaped) - candidate.markers["releaseStart"])
    if fade_out > 0:
        shaped[-fade_out:] *= np.linspace(1.0, 0.0, fade_out)
    loop = shaped[candidate.markers["loopStart"]:candidate.markers["loopEnd"]] / full_scale
    rms = float(np.sqrt(np.mean(loop ** 2))) if loop.size else 0.0
    candidate.gain_db = 0.0 if rms <= 0 else max(-12.0, min(12.0, 20.0 * math.log10(TARGET_RMS / rms)))
    return shaped


def file_stem(alias: str) -> str:
    return alias.replace(" ", "-")


def build(corpus: Path, labels: Path, pitch_tool: Path, output: Path, max_takes: int,
          songs: list[str] | None) -> dict:
    if output.exists():
        raise IntakeError(f"output already exists: {output}")
    song_dirs = sorted(path for path in corpus.iterdir() if path.is_dir() and path.name.startswith("pjs"))
    if songs:
        wanted = set(songs)
        song_dirs = [path for path in song_dirs if path.name in wanted]
        missing = wanted - {path.name for path in song_dirs}
        if missing:
            raise IntakeError(f"songs not in corpus: {sorted(missing)}")
    if not song_dirs:
        raise IntakeError("no songs found")
    license_path = labels.parent / "LICENSE"
    if not license_path.is_file():
        raise IntakeError("the manual-label LICENSE file is required next to the lab directory")

    accepted: list[tuple[Candidate, np.ndarray, int]] = []
    sources: dict[str, dict] = {}
    rejected = {"short": 0, "pitch": 0, "clipping": 0}
    for song_dir in song_dirs:
        song = song_dir.name
        wav = song_dir / f"{song}_song.wav"
        lab = labels / f"{song}.lab"
        if not wav.is_file() or not lab.is_file():
            raise IntakeError(f"{song} needs {wav.name} and {lab.name}")
        samples, width = read_mono_pcm(wav)
        phones = parse_labels(lab.read_text(encoding="utf-8"), len(samples))
        wav_sha = sha256_file(wav)
        pitch_input, canonical = wav, None
        try:
            frames, pitch_source_sha = load_pitch(pitch_tool, wav)
        except IntakeError:
            # Some corpus files carry RIFF chunks the engine's strict reader refuses. The
            # pitch is then measured on a canonical container holding the same PCM.
            canonical = output.parent / f".{output.name}-{song}-canonical.wav"
            write_mono_pcm(canonical, samples.astype(np.float64), width)
            pitch_input = canonical
            try:
                frames, pitch_source_sha = load_pitch(pitch_tool, canonical)
            finally:
                canonical_sha = sha256_file(canonical)
                canonical.unlink()
        expected_sha = wav_sha if canonical is None else canonical_sha
        if pitch_source_sha and pitch_source_sha != expected_sha:
            raise IntakeError(f"pitch extraction for {song} read different bytes")
        sources[song] = {"audio": wav.name, "audioSha256": wav_sha,
                         "labels": lab.name, "labelsSha256": sha256_file(lab),
                         "pitchMeasuredOn": "original" if pitch_input == wav else
                         "canonical copy of the original PCM (strict reader refused the container)"}
        full_scale = float(1 << (8 * width - 1))
        proposals = propose_units(song, phones)
        rejected["short"] += sum(1 for phone in phones if phone.symbol in SUSTAINABLE) - len(proposals)
        for candidate in proposals:
            if not measure_pitch(candidate, frames):
                rejected["pitch"] += 1
                continue
            cut = samples[candidate.cut_start:candidate.cut_end].astype(np.float64)
            if np.any(np.abs(cut) >= CLIP_FRACTION * full_scale):
                rejected["clipping"] += 1
                continue
            snap_loop(candidate, cut)
            accepted.append((candidate, shape_and_level(candidate, cut, full_scale), width))

    groups: dict[tuple[str, int], list[tuple[Candidate, np.ndarray, int]]] = {}
    for item in accepted:
        groups.setdefault((item[0].alias, item[0].root_midi), []).append(item)

    (output / "audio").mkdir(parents=True)
    units, provenance = [], []
    for (alias, root_midi), items in sorted(groups.items()):
        items.sort(key=lambda item: item[0].score())
        for rank, (candidate, audio, width) in enumerate(items[:max_takes]):
            take = rank + 1
            name = f"{file_stem(alias)}_m{root_midi}_t{take}"
            relative = f"audio/{name}.wav"
            write_mono_pcm(output / relative, audio, width)
            unit_id = f"ja.pjs.m{root_midi}.{file_stem(alias)}.{take:02d}"
            units.append({
                "id": unit_id, "alias": alias, "phones": candidate.phones, "kind": candidate.kind,
                "audio": relative, "rootMidi": root_midi, "style": "original", "take": take,
                "priority": max_takes - rank, "gainDb": round(candidate.gain_db, 3),
                "renderer": "raw", "enabled": True, "markers": candidate.markers, "pitchMarks": [],
            })
            provenance.append({
                "unit": unit_id, "song": candidate.song,
                "sourceFrames": [candidate.cut_start, candidate.cut_end],
                "sourcePhones": candidate.source_symbols,
                "measuredMidi": round(root_midi + candidate.cents_off / 100.0, 3),
                "pitchSpreadSemitones": round(candidate.spread, 3),
                "derivedSha256": sha256_file(output / relative),
                "changes": "copied original PCM; 3 ms fade-in and 8 ms fade-out; loop snapped to whole periods",
            })

    manifest = {
        "formatId": "com.project-seam.voicebank", "schemaVersion": 3,
        "id": BANK_ID, "version": BANK_VERSION, "displayName": DISPLAY_NAME,
        "characterId": "", "characterVersion": "", "language": "ja",
        "expectedSampleRate": SAMPLE_RATE, "styles": ["original"], "units": units,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                                          encoding="utf-8")
    (output / "LICENSE").write_bytes(license_path.read_bytes())
    (output / "NOTICE").write_text(
        "Engineering-only voicebank. Not a release resource.\n\n"
        "Audio is cut from the PJS corpus 1.1 (Koguchi, Takamichi and Morise), singing recordings,\n"
        "https://sites.google.com/site/shinnosuketakamichi/research-topics/pjs_corpus,\n"
        "using boundaries from \"PJS Manual Labels\" (https://github.com/UtaUtaUtau/pjs-manual-labels,\n"
        "commit cc08bead6bf2b06e88608a8ece12555bcc720ec9). Both are licensed CC BY-SA 4.0;\n"
        "the licence text is in LICENSE. Changes: units copied from the original PCM without\n"
        "resampling, short fades at the cut edges, loop points chosen at whole pitch periods,\n"
        "devoiced vowel labels mapped to voiced symbols. provenance.json lists every source span.\n"
        "This bank is distributed under the same licence and must not be bundled with the product,\n"
        "whose EULA forbids bank redistribution.\n", encoding="utf-8")
    summary = {
        "bank": {"id": BANK_ID, "version": BANK_VERSION},
        "status": "ENGINEERING_ONLY_NOT_RELEASE_ELIGIBLE",
        "license": "CC-BY-SA-4.0",
        "sources": sources,
        "units": len(units),
        "aliases": len({unit["alias"] for unit in units}),
        "rejectedSpans": rejected,
        "unitProvenance": provenance,
    }
    (output / "provenance.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
                                            encoding="utf-8")
    return summary


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--corpus", type=Path, required=True, help="unpacked PJS_corpus_ver1.1 directory")
    parser.add_argument("--labels", type=Path, required=True, help="manual-label lab/ directory")
    parser.add_argument("--pitch-tool", type=Path, required=True, help="seam_voicebank_cli binary")
    parser.add_argument("--output", type=Path, required=True, help="new bank directory")
    parser.add_argument("--max-takes", type=int, default=2, help="units kept per alias and pitch")
    parser.add_argument("--songs", help="comma-separated song IDs, default all")
    arguments = parser.parse_args(argv)
    if not 1 <= arguments.max_takes <= 8:
        parser.error("--max-takes must be between 1 and 8")
    try:
        summary = build(arguments.corpus, arguments.labels, arguments.pitch_tool, arguments.output,
                        arguments.max_takes, arguments.songs.split(",") if arguments.songs else None)
    except IntakeError as error:
        print(f"refused: {error}", file=sys.stderr)
        return 2
    print(json.dumps({key: summary[key] for key in ("bank", "status", "units", "aliases", "rejectedSpans")}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
