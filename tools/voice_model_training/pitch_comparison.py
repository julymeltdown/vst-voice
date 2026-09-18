"""Time-aligned native F0 diagnostics, never singer or release qualification.

Both tracks are measured independently by the existing full-hop extractor. No
reference melody, temporal offset, octave correction, or warping is fitted to the
candidate. Padded analysis windows and uncertain voiced estimates remain visible
as unmeasurable spans, rather than receiving invented pitch values.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import stat
import statistics
import sys

from .audio_source import read_pcm_source
from .native_features import extract_pitch


def _digest(value):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def _track(value, digest, sample_rate, frame_count, hop_size):
    fields = {"formatId", "schemaVersion", "sourceSha256", "sampleRate", "frameCount", "windowFrames", "hopSize",
        "minimumHz", "maximumHz", "voicingThreshold", "algorithm", "coverage", "pitchFrames",
        "trainingAdmitted", "releaseEligible"}
    if (not isinstance(value, dict) or set(value) != fields
            or value["formatId"] != "com.project-seam.training-pitch-features"
            or type(value["schemaVersion"]) is not int or value["schemaVersion"] != 1
            or value["algorithm"] != "fft-autocorrelation-v1" or value["coverage"] != "full-hop-zero-padded"
            or value["trainingAdmitted"] is not False or value["releaseEligible"] is not False):
        raise ValueError("Unsupported native full-hop pitch contract")
    if not _digest(digest) or value["sourceSha256"] != digest:
        raise ValueError("Native pitch source binding differs from captured WAV bytes")
    window = 128
    while window < sample_rate // 24:
        window *= 2
    for key, expected in (("sampleRate", sample_rate), ("frameCount", frame_count),
                          ("hopSize", hop_size), ("windowFrames", window)):
        if type(value[key]) is not int or value[key] != expected:
            raise ValueError("Native pitch source geometry or analysis grid differs")
    for key, expected in (("minimumHz", 60), ("maximumHz", 1200), ("voicingThreshold", .32)):
        if type(value[key]) not in (int, float) or value[key] != expected:
            raise ValueError("Native pitch estimator configuration differs")
    rows = value["pitchFrames"]
    if not isinstance(rows, list) or len(rows) != (frame_count + hop_size - 1) // hop_size:
        raise ValueError("Native pitch frames must cover the exact source grid")
    for index, row in enumerate(rows):
        if (not isinstance(row, dict) or set(row) != {"sourceFrame", "f0Hz", "confidence", "voiced"}
                or type(row["sourceFrame"]) is not int or row["sourceFrame"] != index * hop_size
                or type(row["voiced"]) is not bool):
            raise ValueError("Invalid native pitch frame or unequal grid")
        f0, confidence = row["f0Hz"], row["confidence"]
        # FFT roundoff may put normalized correlation a few ulps above one.
        if (type(confidence) not in (int, float) or not math.isfinite(confidence)
                or not 0 <= confidence <= 1 + 1e-9
                or type(f0) not in (int, float) or not math.isfinite(f0)
                or (not 0 < f0 < sample_rate / 2 if row["voiced"] else f0 != 0)
                or row["voiced"] != (confidence >= value["voicingThreshold"])):
            raise ValueError("Invalid native pitch confidence, F0 or voicing")
    return rows, window


def compare_pitch_tracks(reference, candidate, *, reference_sha256, candidate_sha256,
                         sample_rate, frame_count, hop_size=256,
                         minimum_confidence=.6, maximum_error_cents=50.) -> dict:
    """Compare independently extracted F0 at exactly equal source-frame offsets.

The caller must bind both expected hashes to actual WAV bytes. This lower-level
function validates contracts, not the provenance of caller-provided tracks. The
two-WAV entrypoint below performs extraction and byte binding itself.
"""
    if (type(sample_rate) is not int or not 8000 <= sample_rate <= 192000
            or type(frame_count) is not int or not 1 <= frame_count <= 16000000
            or type(hop_size) is not int or hop_size != 256
            or (frame_count + hop_size - 1) // hop_size > 65536
            or type(minimum_confidence) not in (int, float) or not math.isfinite(minimum_confidence)
            or not .32 <= minimum_confidence <= 1
            or type(maximum_error_cents) not in (int, float) or not math.isfinite(maximum_error_cents)
            or not 0 <= maximum_error_cents <= 2400):
        raise ValueError("Invalid pitch comparison grid or diagnostic thresholds")
    source, window = _track(reference, reference_sha256, sample_rate, frame_count, hop_size)
    rendered, _ = _track(candidate, candidate_sha256, sample_rate, frame_count, hop_size)
    errors, hz_errors, statuses, spans = [], [], [], []
    edge_frames, low_frames, mismatches, unvoiced, compared = 0, 0, 0, 0, 0
    outside, reference_voiced, candidate_voiced = 0, 0, 0
    for left, right in zip(source, rendered):
        offset = left["sourceFrame"]
        reference_voiced += left["voiced"]
        candidate_voiced += right["voiced"]
        reasons = []
        if offset + window > frame_count:
            reasons.append("zero-padded-analysis-window")
            edge_frames += 1
        uncertain = []
        for name, row in (("reference", left), ("candidate", right)):
            if row["voiced"] and row["confidence"] < minimum_confidence:
                uncertain.append(name + "-low-confidence")
        # Tail windows are already unusable regardless of their confidence;
        # count interior uncertainty separately for a truthful overall outcome.
        if uncertain and not reasons:
            low_frames += 1
        reasons.extend(uncertain)
        cents = None
        if reasons:
            status = "unmeasurable"
            if spans and spans[-1]["endFrame"] == offset and spans[-1]["reasons"] == reasons:
                spans[-1]["endFrame"] = min(offset + hop_size, frame_count)
                spans[-1]["analysisFrameCount"] += 1
            else:
                spans.append(dict(startFrame=offset, endFrame=min(offset + hop_size, frame_count),
                                  analysisFrameCount=1, reasons=reasons))
        elif left["voiced"] != right["voiced"]:
            status = "voicing-mismatch"
            mismatches += 1
        elif not left["voiced"]:
            status = "both-unvoiced"
            unvoiced += 1
        else:
            cents = 1200 * math.log2(right["f0Hz"] / left["f0Hz"])
            hz_errors.append(right["f0Hz"] - left["f0Hz"])
            compared += 1
            outside += abs(cents) > maximum_error_cents
            status = "compared"
        errors.append(cents)
        statuses.append(status)
    measurable = [value for value in errors if value is not None]
    absolute = [abs(value) for value in measurable]
    status = ("MISMATCH" if outside or mismatches else "UNRESOLVED" if not compared or low_frames
              else "MATCH_ON_MEASURABLE_FRAMES")
    def rounded(value):
        return round(value, 6) if value is not None else None
    return dict(formatId="com.project-seam.framewise-pitch-comparison", schemaVersion=1,
        sampleRate=sample_rate, frameCount=frame_count, hopSize=hop_size, windowFrames=window,
        referenceSha256=reference_sha256, candidateSha256=candidate_sha256,
        referenceTrackSha256=hashlib.sha256(json.dumps(reference, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest(),
        candidateTrackSha256=hashlib.sha256(json.dumps(candidate, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest(),
        algorithm="fft-autocorrelation-v1", alignment="exact-source-frame-no-shift-no-warp",
        minimumConfidence=minimum_confidence, maximumErrorCents=maximum_error_cents,
        analysisFrames=len(source), referenceVoicedFrames=reference_voiced, candidateVoicedFrames=candidate_voiced,
        measurableVoicedPairs=compared, withinToleranceFrames=compared - outside, outsideToleranceFrames=outside,
        voicingMismatchFrames=mismatches, unvoicedPairs=unvoiced,
        unmeasurableFrames=statuses.count("unmeasurable"), edgeWindowFrames=edge_frames,
        lowConfidenceFrames=low_frames, unmeasurableSpans=spans,
        meanAbsoluteCents=rounded(statistics.mean(absolute) if absolute else None),
        medianAbsoluteCents=rounded(statistics.median(absolute) if absolute else None),
        rootMeanSquareCents=rounded(math.sqrt(statistics.mean(value * value for value in measurable)) if measurable else None),
        maximumAbsoluteCents=rounded(max(absolute) if absolute else None),
        f0RmseHz=rounded(math.sqrt(statistics.mean(value * value for value in hz_errors)) if hz_errors else None),
        frameErrorsCents=[rounded(value) for value in errors], frameStatuses=statuses,
        status=status, comparisonSatisfied=status == "MATCH_ON_MEASURABLE_FRAMES",
        fullyMeasured=not spans, singerQualified=False, trainingAdmitted=False, releaseEligible=False)


def _capture(path, maximum_bytes):
    path = Path(path)
    if path.is_symlink():
        raise ValueError("Pitch comparison inputs cannot be symlinks")
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    with os.fdopen(os.open(path, flags), "rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or not 1 <= before.st_size <= maximum_bytes:
            raise ValueError("Pitch comparison input must be a bounded regular file")
        payload = stream.read(maximum_bytes + 1)
        after = os.fstat(stream.fileno())
        if (len(payload) != before.st_size or (before.st_size, before.st_mtime_ns, before.st_ctime_ns)
                != (after.st_size, after.st_mtime_ns, after.st_ctime_ns)):
            raise ValueError("Pitch comparison input changed during capture")
    return payload, hashlib.sha256(payload).hexdigest()


def compare_wavs(reference, candidate, *, executable, minimum_confidence=.6, maximum_error_cents=50.) -> dict:
    """Capture two WAV identities and invoke the trusted native extractor on each."""
    paths = [Path(reference), Path(candidate)]
    identities = []
    for path in paths:
        payload, digest = _capture(path, 64 * 1024 * 1024)
        identity, _ = read_pcm_source(payload, expected_sha256=digest)
        identities.append(identity)
    if any(identities[0][key] != identities[1][key] for key in ("sampleRate", "frameCount")):
        raise ValueError("Pitch comparison WAV clock or length geometry differs; no resampling or trimming")
    _, executable_digest = _capture(executable, 128 * 1024 * 1024)
    tracks = [extract_pitch(Path(executable), path) for path in paths]
    result = compare_pitch_tracks(*tracks, reference_sha256=identities[0]["sourceSha256"],
        candidate_sha256=identities[1]["sourceSha256"], sample_rate=identities[0]["sampleRate"],
        frame_count=identities[0]["frameCount"], minimum_confidence=minimum_confidence,
        maximum_error_cents=maximum_error_cents)
    return dict(formatId="com.project-seam.wav-pitch-comparison", schemaVersion=1,
        reference=dict(identities[0], path=str(paths[0].resolve())),
        candidate=dict(identities[1], path=str(paths[1].resolve())),
        extractor=dict(path=str(Path(executable).resolve()), sha256=executable_digest),
        referenceTrack=tracks[0], candidateTrack=tracks[1], comparison=result,
        singerQualified=False, trainingAdmitted=False, releaseEligible=False)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("reference", "candidate", "extractor", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--minimum-confidence", type=float, default=.6)
    parser.add_argument("--maximum-error-cents", type=float, default=50.)
    args = parser.parse_args(argv)
    try:
        if args.output.exists() or args.output.is_symlink() or not args.output.parent.is_dir():
            raise ValueError("Pitch comparison output must be new with an existing parent")
        result = compare_wavs(args.reference, args.candidate, executable=args.extractor,
            minimum_confidence=args.minimum_confidence, maximum_error_cents=args.maximum_error_cents)
        from .__main__ import publish_new
        publish_new(args.output, result)
        print(json.dumps(dict(status=result["comparison"]["status"], output=str(args.output),
                             singerQualified=False, releaseEligible=False)))
        return 0
    except (ValueError, OSError, RuntimeError) as error:
        print(str(error)[:512], file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
