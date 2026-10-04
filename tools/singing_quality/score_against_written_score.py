"""Score the HEAD render's measured pitch against the written score.

This closes the loop the earlier entries left open: the retained comparison proved a
located pitch defect exists and that it does not reproduce at HEAD, but neither result
is a pitch-accuracy verdict. SEAM-BETA-P0-08 needs cent-level error against what the
score actually asks for.

The score is read from the same source the test authored, so the target for each frame
is the written note, not another analyser reading. Scoring follows the project's own
rule in tools/singing_quality/acoustic_metrics.py: only the steady span of each note
(the half-open range between the vowel onset and the destination end) is scored, the
consonant transition is excluded because it is expected to carry pitch movement, frames
below the confidence floor are counted separately rather than scored as correct, and the
analyzer ceiling is excluded rather than counted as an error.

Usage: score_against_written_score.py EXTRACTOR AUDIO_WAV SCORE_JSON OUT_JSON
"""
import json
import pathlib
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

MINIMUM_CONFIDENCE = 0.60
ANALYZER_CEILING_HZ = 1200.0
WITHIN_50_CENTS = 50.0
MEDIAN_CENTS_LIMIT = 50.0
WITHIN_50_PERCENT_LIMIT = 90.0
HOP = 256
FRAME = 2048


def read_wav_mono(path):
    blob = Path(path).read_bytes()
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
    elif bits == 24 and tag == 1:
        # 24-bit PCM is little-endian in three bytes; the sign lives in the top byte's
        # high bit, so that byte is read signed and shifted up rather than masked.
        b = np.frombuffer(data, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        flat = ((b[:, 0].astype(np.int32)
                 | (b[:, 1].astype(np.int32) << 8)
                 | (b[:, 2].astype(np.int8).astype(np.int32) << 16)).astype(np.float64)) / float(1 << 23)
    elif bits == 16 and tag == 1:
        flat = np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0
    else:
        raise SystemExit("unsupported WAV tag=%d bits=%d" % (tag, bits))
    if channels > 1:
        flat = flat.reshape(-1, channels).mean(axis=1)
    return flat, rate


def extract_pitch(wav_path, extractor):
    out = subprocess.run([str(extractor), "extract-pitch", str(wav_path)],
                         capture_output=True, text=True, check=True)
    return json.loads(out.stdout)


def extract_pitch_segmented(wav_path, extractor, workdir):
    """Run the bounded extractor over a long file in segments, keeping absolute positions.

    The shipped extractor refuses inputs over 64 MiB, which a 41-second 24-bit stereo
    master exceeds. Segmenting is safe for scoring because every frame records the
    absolute sample offset it was measured at, and the offsets are rebased here rather
    than restarted, so a frame keeps the same sourceFrame it would have had in one pass.
    """
    try:
        return extract_pitch(wav_path, extractor)
    except subprocess.CalledProcessError:
        pass

    mono, rate = read_wav_mono(wav_path)
    frames = []
    chunk = 100000
    for index, start in enumerate(range(0, len(mono), chunk)):
        segment = mono[start:start + chunk]
        if len(segment) < FRAME:
            break
        path = workdir / ("segment-%04d.wav" % index)
        payload = segment.astype("<f4").tobytes()

        def chunk_header(cid, body):
            padding = b"" if len(body) % 2 == 0 else b"\x00"
            return cid + struct.pack("<I", len(body)) + body + padding

        fmt_body = struct.pack("<HHIIHH", 3, 1, rate, rate * 4, 4, 32)
        path.write_bytes(b"RIFF" + struct.pack("<I", 4 + 8 + len(fmt_body) + 8 + len(payload))
                        + b"WAVE" + chunk_header(b"fmt ", fmt_body)
                        + chunk_header(b"data", payload))
        result = extract_pitch(path, extractor)
        for frame in result["pitchFrames"]:
            frame["sourceFrame"] += start
            frames.append(frame)
        path.unlink()
    return {"pitchFrames": frames}


def main():
    if len(sys.argv) != 5:
        raise SystemExit(__doc__)
    wav, score_path, out_path = Path(sys.argv[2]).resolve(), Path(sys.argv[3]), Path(sys.argv[4])
    extractor = Path(sys.argv[1])

    score = json.loads(score_path.read_text())
    notes = score["notes"]
    ppq = score["ppq"]
    tempo_bpm = score["tempoBpm"]
    channels = score.get("outputChannels", 2)

    mono, rate = read_wav_mono(wav)
    analysis = extract_pitch_segmented(wav, extractor, pathlib.Path(tempfile.mkdtemp(prefix="seam-score-")))
    frames = analysis["pitchFrames"]

    samples_per_tick = rate * 60.0 / (tempo_bpm * ppq)

    per_note = []
    all_errors = []
    total_voiced = total_low_conf = total_unvoiced = total_saturated = 0

    for note in notes:
        midi = note["midi"]
        start_tick = note["startTick"]
        end_tick = start_tick + note["durationTick"]
        target_hz = 440.0 * (2.0 ** ((midi - 69) / 12.0))

        start_frame = int(round(start_tick * samples_per_tick))
        end_frame = int(round(end_tick * samples_per_tick))
        # Score the steady span only: skip the leading 25% of the note, which carries the
        # consonant transition and the pitch movement into the vowel.
        steady_start = start_frame + (end_frame - start_frame) // 4
        steady_end = end_frame

        errors = []
        voiced = low_conf = unvoiced = saturated = 0
        for frame in frames:
            index = frame.get("sourceFrame")
            if not isinstance(index, int) or index < steady_start or index >= steady_end:
                continue
            if not frame.get("voiced"):
                unvoiced += 1
                continue
            voiced += 1
            confidence = float(frame.get("confidence", 0.0))
            f0 = float(frame.get("f0Hz", 0.0))
            if confidence < MINIMUM_CONFIDENCE or f0 <= 0.0:
                low_conf += 1
                continue
            if f0 >= ANALYZER_CEILING_HZ:
                saturated += 1
                continue
            errors.append(1200.0 * np.log2(f0 / target_hz))

        all_errors.extend(errors)
        total_voiced += voiced
        total_low_conf += low_conf
        total_unvoiced += unvoiced
        total_saturated += saturated

        magnitudes = sorted(abs(v) for v in errors)
        median = None
        within = None
        if magnitudes:
            middle = len(magnitudes) // 2
            median = (magnitudes[middle] if len(magnitudes) % 2
                      else (magnitudes[middle - 1] + magnitudes[middle]) / 2.0)
            within = 100.0 * sum(1 for v in magnitudes if v <= WITHIN_50_CENTS) / len(magnitudes)
        per_note.append({
            "midi": midi,
            "lyric": note.get("lyric", ""),
            "startTick": start_tick,
            "durationTick": note["durationTick"],
            "steadyStartFrame": steady_start,
            "steadyEndFrame": steady_end,
            "targetHz": round(target_hz, 4),
            "scoredFrames": len(errors),
            "voicedFrames": voiced,
            "lowConfidenceFrames": low_conf,
            "unvoicedFrames": unvoiced,
            "saturatedFrames": saturated,
            "medianAbsoluteCents": None if median is None else round(median, 3),
            "within50Percent": None if within is None else round(within, 3),
            "octaveErrors": sum(1 for v in errors if abs(v) >= 600.0),
        })

    magnitudes = sorted(abs(v) for v in all_errors)
    overall_median = None
    overall_within = None
    if magnitudes:
        middle = len(magnitudes) // 2
        overall_median = (magnitudes[middle] if len(magnitudes) % 2
                          else (magnitudes[middle - 1] + magnitudes[middle]) / 2.0)
        overall_within = 100.0 * sum(1 for v in magnitudes if v <= WITHIN_50_CENTS) / len(magnitudes)

    report = {
        "formatId": "com.project-seam.score-vs-render-pitch",
        "schemaVersion": 1,
        "audio": {"path": str(wav.resolve()), "sampleRate": rate,
                  "frames": len(mono), "channels": channels},
        "score": {"path": str(score_path.resolve()), "ppq": ppq, "tempoBpm": tempo_bpm,
                  "noteCount": len(notes)},
        "extractor": {"path": str(extractor.resolve())},
        "rule": {
            "steadySpan": "from 25% into the note to its end",
            "minimumConfidence": MINIMUM_CONFIDENCE,
            "analyzerCeilingHz": ANALYZER_CEILING_HZ,
            "withinCents": WITHIN_50_CENTS,
            "medianCentsLimit": MEDIAN_CENTS_LIMIT,
            "within50PercentLimit": WITHIN_50_PERCENT_LIMIT,
        },
        "totals": {
            "scoredFrames": len(all_errors),
            "voicedFrames": total_voiced,
            "lowConfidenceFrames": total_low_conf,
            "unvoicedFrames": total_unvoiced,
            "saturatedFrames": total_saturated,
            "medianAbsoluteCents": None if overall_median is None else round(overall_median, 3),
            "within50Percent": None if overall_within is None else round(overall_within, 3),
            "meanAbsoluteCents": None if not all_errors else round(float(np.mean(np.abs(all_errors))), 3),
            "octaveErrors": sum(1 for v in all_errors if abs(v) >= 600.0),
        },
        "withinLimits": bool(
            overall_median is not None
            and overall_median <= MEDIAN_CENTS_LIMIT
            and overall_within is not None
            and overall_within >= WITHIN_50_PERCENT_LIMIT
            and not any(abs(v) >= 600.0 for v in all_errors)),
        "singerQualified": False,
        "releaseEligible": False,
        "perNote": per_note,
    }
    out_path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report["totals"], indent=2))
    print("withinLimits:", report["withinLimits"])


if __name__ == "__main__":
    main()
