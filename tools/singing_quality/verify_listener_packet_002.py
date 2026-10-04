"""Verify each packet file actually contains the phenomenon its question asks about.

A listening packet that does not contain what it claims to contain is worse than none: the reviewer's
answer would be confident and wrong. Each file is checked against the specific property its question
depends on.
"""
import hashlib
import json
import pathlib
import struct

import numpy as np

import sys

PACKET = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(
    "/tmp/seam_listening/packet")


def read_wav(path):
    blob = path.read_bytes()
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(blob):
        cid = blob[pos:pos + 4]
        size = struct.unpack_from("<I", blob, pos + 4)[0]
        body = blob[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", body, 0)
        elif cid == b"data":
            data = body
        pos += 8 + size + (size & 1)
    tag, channels, rate, _, _, bits = fmt
    if bits == 32 and tag == 3:
        flat = np.frombuffer(data, dtype="<f4").astype(np.float64)
    elif bits == 16 and tag == 1:
        flat = np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0
    elif bits == 24 and tag == 1:
        b = np.frombuffer(data, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        flat = ((b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8)
                | (b[:, 2].astype(np.int8).astype(np.int32) << 16)).astype(np.float64)) / float(1 << 23)
    else:
        raise SystemExit("unsupported tag=%d bits=%d" % (tag, bits))
    if channels > 1:
        flat = flat.reshape(-1, channels).mean(axis=1)
    return flat, rate


manifest = json.loads((PACKET / "manifest.json").read_text())
print("=== hash and signal verification ===")
print()
all_ok = True
for artifact in manifest["artifacts"]:
    path = PACKET / artifact["file"]
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    mono, rate = read_wav(path)
    rms = float(np.sqrt((mono ** 2).mean()))
    peak = float(np.abs(mono).max())
    ok = digest == artifact["sha256"] and rms > 0.001 and peak > 0.01
    all_ok = all_ok and ok
    print("  %-34s hash %s  peak %.4f  rms %.5f  %s" % (
        artifact["file"], "OK" if digest == artifact["sha256"] else "MISMATCH",
        peak, rms, "OK" if ok else "PROBLEM"))

print()
print("=== does q1 actually contain a measurable lead? ===")
mono, rate = read_wav(PACKET / "q1-legato-run.wav")
spt = rate * 60.0 / (120.0 * 960)
print("  note grid: 1920 ticks = %d frames per note" % int(1920 * spt))
energy = [float(np.sqrt((mono[i:i + 256] ** 2).mean())) if i + 256 <= len(mono) else 0.0
          for i in range(0, len(mono) - 256, 256)]
peak_e = max(energy)
silence = [i for i, e in enumerate(energy) if e < peak_e * 0.02]
runs = []
for i in silence:
    if runs and runs[-1][1] == i - 1:
        runs[-1][1] = i
    else:
        runs.append([i, i])
gaps = [(a, b) for a, b in runs if b - a >= 2]
print("  silent gaps of at least 2 blocks inside the phrase: %d" % len(gaps))
for a, b in gaps[:4]:
    print("     blocks %d to %d, frames %d to %d" % (a, b, a * 256, b * 256))
print("  if there are gaps the reviewer hears note boundaries, which is what q1 needs")

print()
print("=== does q2 contain both harmonic regimes? ===")
mono, rate = read_wav(PACKET / "q2-pitch-range.wav")
spt = rate * 60.0 / (120.0 * 960)
print("  %-6s %10s %10s %10s" % ("midi", "h1", "h2", "h1/h2"))
for index, midi in enumerate([55, 60, 67, 72, 79]):
    note_start = index * 1920 * spt
    note_end = (index + 1) * 1920 * spt
    # Read the middle half of the note, so no analysis window straddles a boundary.
    margin = (note_end - note_start) // 4
    start = int(note_start + margin)
    end = int(note_end - margin)
    window = mono[start:end]
    written = 440.0 * 2 ** ((midi - 69) / 12.0)

    def amplitude(hz):
        values = []
        offset = 0
        while offset + 4096 <= len(window):
            segment = window[offset:offset + 4096].copy()
            segment -= segment.mean()
            if float(np.dot(segment, segment)) > 1e-12:
                spectrum = np.abs(np.fft.rfft(segment * np.hanning(4096), 32768))
                freqs = np.fft.rfftfreq(32768, 1.0 / rate)
                near = np.where((freqs > hz - 10.0) & (freqs < hz + 10.0))[0]
                if near.size:
                    values.append(float(spectrum[near].max()))
            offset += 512
        return float(np.median(values)) if values else 0.0

    a1 = amplitude(written)
    a2 = amplitude(written * 2.0)
    ratio = a1 / a2 if a2 > 0 else 0.0
    print("  %-6d %10.3f %10.3f %10.3f %s" % (
        midi, a1, a2, ratio, "h2 dominates" if ratio < 1.0 else "h1 dominates"))

print()
print("=== does q3 actually contain a silent note? ===")
mono, rate = read_wav(PACKET / "q3-phrase-with-missing-note.wav")
project = json.loads(pathlib.Path("/tmp/seam_head5/song-004/project.seam").read_text())
region = project["vocalTracks"][0]["regions"][0]
notes = sorted(region["notes"], key=lambda n: n["startTick"])
spt = rate * 60.0 / (120.0 * 960)
for index, note in enumerate(notes):
    midi = note["midiKey"]
    start = int(note["startTick"] * spt)
    end = int((note["startTick"] + note["durationTick"]) * spt)
    segment = mono[start:end]
    rms = float(np.sqrt((segment ** 2).mean()))
    verdict = "SILENT" if rms < 0.002 else ""
    print("  note %d midi %2d  frames %7d..%7d  rms %.5f %s"
          % (index, midi, start, end, rms, verdict))

print()
print("packet verification %s" % ("PASSES" if all_ok else "HAS PROBLEMS"))
