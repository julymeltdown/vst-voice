"""Check legacy packet 002 integrity without granting musical acceptance.

Optional historical acoustic diagnostics are observations, not acceptance tests.
Packet 003 must supply matched controls and bound provenance separately.
"""
import argparse
import hashlib
import json
import pathlib
import struct
import sys

import numpy as np

REQUIRED_FILES = {
    "q1-legato-run.wav", "q1-detached-run.wav", "q2-pitch-range.wav",
    "q2-single-weak-fundamental.wav", "q3-phrase-with-missing-note.wav",
    "q3-phrase-control.wav",
}
REQUIRED_CASES = {"q1-lead-timing", "q2-harmonic-balance", "q3-missing-note"}


def read_wav_bytes(path):
    if path.stat().st_size > 256 * 1024 * 1024:
        raise ValueError("WAV exceeds 256 MiB")
    with path.open('rb') as stream:
        blob = stream.read(256 * 1024 * 1024 + 1)
    if len(blob) > 256 * 1024 * 1024:
        raise ValueError("WAV exceeds 256 MiB")
    return blob


def read_wav(path):
    return parse_wav(read_wav_bytes(path))


def parse_wav(blob):
    if blob[:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise ValueError("not RIFF/WAVE")
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(blob):
        cid = blob[pos:pos + 4]
        size = struct.unpack_from("<I", blob, pos + 4)[0]
        if pos + 8 + size > len(blob):
            raise ValueError("truncated WAV chunk")
        body = blob[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", body, 0)
        elif cid == b"data":
            data = body
        pos += 8 + size + (size & 1)
    if fmt is None or data is None:
        raise ValueError("missing WAV format or data")
    tag, channels, rate, _, alignment, bits = fmt
    if not 1 <= channels <= 8 or rate <= 0 or alignment != channels * (bits // 8):
        raise ValueError("invalid WAV format")
    if not data or not alignment or len(data) % alignment:
        raise ValueError("empty or incomplete WAV frames")
    if bits == 32 and tag == 3:
        flat = np.frombuffer(data, dtype="<f4").astype(np.float64)
    elif bits == 16 and tag == 1:
        flat = np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0
    elif bits == 24 and tag == 1:
        b = np.frombuffer(data, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        flat = ((b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8)
                | (b[:, 2].astype(np.int8).astype(np.int32) << 16)).astype(np.float64)) / float(1 << 23)
    else:
        raise ValueError("unsupported tag=%d bits=%d" % (tag, bits))
    if not np.isfinite(flat).all():
        raise ValueError("non-finite WAV samples")
    if channels > 1:
        flat = flat.reshape(-1, channels).mean(axis=1)
    return flat, rate


def verify_integrity(packet):
    manifest = json.loads((packet / "manifest.json").read_text())
    if not isinstance(manifest, dict):
        raise ValueError("manifest root must be an object")
    if type(manifest.get("schemaVersion")) is not int or manifest.get("formatId") != "com.project-seam.listening-packet" or manifest.get("schemaVersion") != 1:
        raise ValueError("expected legacy packet 002 schema version 1")
    artifacts = manifest["artifacts"]
    names = [a["file"] for a in artifacts]
    cases = manifest["cases"]
    case_ids = [c["id"] for c in cases]
    if len(names) != len(set(names)) or set(names) != REQUIRED_FILES:
        raise ValueError("artifact inventory must contain the six packet 002 files exactly once")
    if len(case_ids) != 3 or set(case_ids) != REQUIRED_CASES:
        raise ValueError("case inventory must contain all three packet 002 questions exactly once")
    expected = {
        "q1-lead-timing": {"q1-legato-run.wav", "q1-detached-run.wav"},
        "q2-harmonic-balance": {"q2-pitch-range.wav", "q2-single-weak-fundamental.wav"},
        "q3-missing-note": {"q3-phrase-with-missing-note.wav", "q3-phrase-control.wav"},
    }
    for case in cases:
        if len(case["files"]) != 2 or set(case["files"]) != expected[case["id"]]:
            raise ValueError("case files do not match the declared question")
    all_ok = True
    for artifact in artifacts:
        path = packet / artifact["file"]
        if path.is_symlink() or not path.is_file():
            raise ValueError("artifact is missing or is not a regular packet file: " + path.name)
        payload = read_wav_bytes(path)
        digest = hashlib.sha256(payload).hexdigest()
        mono, _ = parse_wav(payload)
        rms = float(np.sqrt((mono ** 2).mean()))
        peak = float(np.abs(mono).max())
        ok = digest == artifact["sha256"] and rms > 0.001 and peak > 0.01
        all_ok = all_ok and ok
        print("%s: hash=%s peak=%.4f rms=%.5f %s" % (
            path.name, "OK" if digest == artifact["sha256"] else "MISMATCH",
            peak, rms, "OK" if ok else "PROBLEM"))
    return all_ok


def print_diagnostics(packet, project_path):
    print("Historical diagnostics only: these do not establish audible quality or a matched control.")
    print()
    print("=== does q1 actually contain a measurable lead? ===")
    mono, rate = read_wav(packet / "q1-legato-run.wav")
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
    mono, rate = read_wav(packet / "q2-pitch-range.wav")
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
    mono, rate = read_wav(packet / "q3-phrase-with-missing-note.wav")
    project = json.loads(project_path.read_text())
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


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("packet", type=pathlib.Path)
    parser.add_argument("--diagnostics", action="store_true",
                        help="print historical Q1/Q2/Q3 observations, not acceptance")
    parser.add_argument("--q3-project", type=pathlib.Path,
                        help="explicit source project for optional Q3 diagnostics")
    args = parser.parse_args(argv)
    if args.diagnostics and args.q3_project is None:
        parser.error("--diagnostics requires --q3-project; no implicit project is trusted")
    try:
        if not verify_integrity(args.packet):
            print("PACKET_INTEGRITY=FAIL")
            return 1
    except (OSError, ValueError, KeyError, TypeError, IndexError, struct.error) as error:
        print("PACKET_INTEGRITY=FAIL: " + str(error), file=sys.stderr)
        return 1
    print("PACKET_INTEGRITY=PASS; musical acceptance and provenance are NOT_VERIFIED")
    if args.diagnostics:
        try:
            print_diagnostics(args.packet, args.q3_project)
        except (OSError, ValueError, KeyError, TypeError, IndexError, struct.error) as error:
            print("PACKET_DIAGNOSTICS=FAIL: " + str(error), file=sys.stderr)
            return 3
        print("PACKET_DIAGNOSTICS=OBSERVED; no acceptance verdict")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
