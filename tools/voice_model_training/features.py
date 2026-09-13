"""Convert captured native pitch features into unreviewed training labels."""
import copy
import math
from .labels import label_report


def pitch_corrections(label: dict, features: dict, *, source_sha256: str, sample_rate: int,
                      vocabulary: set[str], minimum_confidence: float) -> dict:
    """Flag uncertain estimates using an explicitly supplied review threshold."""
    apply_pitch_features(label, features, source_sha256=source_sha256, sample_rate=sample_rate,
                         vocabulary=vocabulary, minimum_confidence=minimum_confidence)
    queue = []
    for index, frame in enumerate(features["pitchFrames"]):
        if frame["voiced"] and frame["confidence"] < minimum_confidence:
            queue.append(dict(code="low-pitch-confidence", frameIndex=index, sourceFrame=frame["sourceFrame"]))
        if frame["sourceFrame"] + features["windowFrames"] > features["frameCount"]:
            queue.append(dict(code="zero-padded-pitch-window", frameIndex=index, sourceFrame=frame["sourceFrame"]))
    return dict(formatId="com.project-seam.training-pitch-corrections", schemaVersion=1,
                minimumConfidence=minimum_confidence, correctionQueue=queue,
                reviewRequired=True, trainingAdmitted=False)


def apply_pitch_features(label: dict, features: dict, *, source_sha256: str, sample_rate: int,
                         vocabulary: set[str], minimum_confidence: float) -> dict:
    label_report(label, vocabulary=vocabulary, minimum_confidence=minimum_confidence)
    fields = {"formatId", "schemaVersion", "sourceSha256", "sampleRate", "frameCount", "windowFrames", "hopSize",
              "minimumHz", "maximumHz", "voicingThreshold", "algorithm", "coverage", "pitchFrames", "trainingAdmitted", "releaseEligible"}
    if (not isinstance(features, dict) or set(features) != fields
            or features["formatId"] != "com.project-seam.training-pitch-features"
            or type(features["schemaVersion"]) is not int or features["schemaVersion"] != 1
            or features["algorithm"] != "fft-autocorrelation-v1" or features["coverage"] != "full-hop-zero-padded"
            or features["trainingAdmitted"] is not False or features["releaseEligible"] is not False):
        raise ValueError("Unsupported native pitch feature contract")
    if (not isinstance(source_sha256, str) or len(source_sha256) != 64
            or any(c not in "0123456789abcdef" for c in source_sha256)
            or features["sourceSha256"] != source_sha256
            or type(sample_rate) is not int or not 8000 <= sample_rate <= 384000):
        raise ValueError("Invalid pitch source binding")
    for key, expected in (("sampleRate", sample_rate), ("frameCount", label["frameCount"]), ("hopSize", 256)):
        if type(features[key]) is not int or features[key] != expected:
            raise ValueError("Pitch feature source geometry differs")
    window = 128
    while window < sample_rate // 24:
        window *= 2
    if type(features["windowFrames"]) is not int or features["windowFrames"] != window:
        raise ValueError("Pitch window differs from declared extractor")
    for key, expected in (("minimumHz", 60), ("maximumHz", 1200), ("voicingThreshold", 0.32)):
        if type(features[key]) not in (int, float) or features[key] != expected:
            raise ValueError("Pitch estimator settings differ")
    frames = features["pitchFrames"]
    count = (label["frameCount"] + 255) // 256
    if not isinstance(frames, list) or len(frames) != count or count > 65536:
        raise ValueError("Pitch features do not cover the source")
    f0, voiced = [], []
    for index, frame in enumerate(frames):
        if not isinstance(frame, dict) or set(frame) != {"sourceFrame", "f0Hz", "confidence", "voiced"}:
            raise ValueError("Invalid pitch frame")
        if type(frame["sourceFrame"]) is not int or frame["sourceFrame"] != index * 256:
            raise ValueError("Pitch frame grid differs")
        confidence = frame["confidence"]
        if type(confidence) not in (int, float) or not math.isfinite(confidence) or not 0 <= confidence <= 1:
            raise ValueError("Invalid pitch confidence")
        f0.append(frame["f0Hz"]); voiced.append(frame["voiced"])
    result = copy.deepcopy(label)
    result.update(hopSize=256, f0Hz=f0, voiced=voiced, reviewRevision=None)
    report = label_report(result, vocabulary=vocabulary, minimum_confidence=minimum_confidence)
    if any(r["code"] == "voicing-f0-mismatch" for r in report["correctionQueue"]):
        raise ValueError("Extracted pitch has inconsistent F0 and voicing")
    return result
