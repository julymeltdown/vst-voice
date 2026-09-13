"""Sample-exact PCM phrase extraction with immutable parent lineage."""
import hashlib
import io
import wave

from .audio_source import inspect_pcm_source
from .labels import label_report, score_report
from .features import apply_pitch_features


def crop_score(score: dict, label: dict, *, start_frame: int, end_frame: int) -> dict:
    """Crop explicit-silence score ownership against a validated acoustic label.

    Reject cuts leaving a sung syllable without phones or phones without its
    sung note. Such cuts need a revised alignment, not invented supervision.
    """
    if (type(start_frame) is not int or type(end_frame) is not int
            or not 0 <= start_frame < end_frame <= label["frameCount"]):
        raise ValueError("Score crop is outside the source")
    score_report(score, frame_count=label["frameCount"], phoneme_count=len(label["phonemes"]), explicit_silence=True)
    phone_map = {old: new for new, old in enumerate(
        i for i, p in enumerate(label["phonemes"]) if p["startFrame"] < end_frame and p["endFrame"] > start_frame)}
    selected = [n for n in score["notes"] if n["startFrame"] < end_frame and n["endFrame"] > start_frame]
    ids = sorted({n["syllable"] for n in selected if n["syllable"] is not None})
    syllable_map = {old: new for new, old in enumerate(ids)}
    syllables = []
    for old in ids:
        parent = score["syllables"][old]
        phones = [phone_map[i] for i in range(parent["phoneStart"], parent["phoneEnd"]) if i in phone_map]
        if not phones:
            raise ValueError("Cropped sung syllable has no phones; relabel the boundary")
        syllables.append(dict(lyric=parent["lyric"], phoneStart=phones[0], phoneEnd=phones[-1] + 1))
    notes = []
    for n in selected:
        syllable = syllable_map[n["syllable"]] if n["syllable"] is not None else None
        notes.append(dict(n, startFrame=max(n["startFrame"], start_frame) - start_frame,
                          endFrame=min(n["endFrame"], end_frame) - start_frame, syllable=syllable,
                          slur=n["slur"] if notes else False))
    result = dict(language=score["language"], syllables=syllables, notes=notes,
                  silencePhones=[phone_map[i] for i in score["silencePhones"] if i in phone_map])
    score_report(result, frame_count=end_frame - start_frame, phoneme_count=len(phone_map), explicit_silence=True)
    return result


def crop_labels(label: dict, *, segment_id: str, start_frame: int, end_frame: int,
                vocabulary: set[str], minimum_confidence: float, fresh_features: dict | None = None,
                child_source_sha256: str = "", sample_rate: int = 0) -> dict:
    """Rebase structural annotations; caller binds parent/child audio identities.

    Existing F0 frames can be reused only at an analysis-hop-aligned start.
    Other starts require fresh feature extraction, not implicit interpolation.
    This does not crop score supervision or authenticate previous reviews.
    """
    label_report(label, vocabulary=vocabulary, minimum_confidence=minimum_confidence)
    if (not isinstance(segment_id, str) or not 1 <= len(segment_id.encode()) <= 256
            or segment_id == label["sourceId"] or any(ord(c) < 32 or ord(c) == 127 for c in segment_id)):
        raise ValueError("Invalid cropped label identity")
    if (type(start_frame) is not int or type(end_frame) is not int
            or not 0 <= start_frame < end_frame <= label["frameCount"]):
        raise ValueError("Label crop is outside the source")
    hop = label["hopSize"]
    if start_frame % hop and fresh_features is None:
        raise ValueError("Off-grid label crop requires fresh F0/voicing extraction")
    phones = []
    for phone in label["phonemes"]:
        start, end = max(start_frame, phone["startFrame"]), min(end_frame, phone["endFrame"])
        if start < end:
            phones.append(dict(phone, startFrame=start - start_frame, endFrame=end - start_frame))
    first, stop = start_frame // hop, (end_frame + hop - 1) // hop
    result = dict(sourceId=segment_id, frameCount=end_frame - start_frame, hopSize=hop,
                  phonemes=phones, f0Hz=label["f0Hz"][first:stop], voiced=label["voiced"][first:stop],
                  reviewRevision=None)
    if fresh_features is not None:
        # Structural staging only; placeholders are never returned or published.
        count = (result["frameCount"] + 255) // 256
        result.update(hopSize=256, f0Hz=[0] * count, voiced=[False] * count)
        return apply_pitch_features(result, fresh_features, source_sha256=child_source_sha256,
                                    sample_rate=sample_rate, vocabulary=vocabulary, minimum_confidence=minimum_confidence)
    label_report(result, vocabulary=vocabulary, minimum_confidence=minimum_confidence)
    return result


def segment_source(payload: bytes, *, source: dict, sample_rate: int,
                   segment_id: str, start_frame: int, end_frame: int) -> tuple[bytes, dict]:
    """Return a new WAV and provenance; never mutate or publish the source.

    Frame interval is half-open in the original sample clock. There is no
    resampling, normalization, fade, automatic silence detection or approval.
    Song/session/lineage are inherited so descendants remain split together.
    """
    fields = {"sourceId", "songId", "sessionId", "lineageId", "sourceSha256"}
    if not isinstance(source, dict) or set(source) != fields:
        raise ValueError("Invalid segmentation source identity")
    for text in [*source.values(), segment_id]:
        if (not isinstance(text, str) or not 1 <= len(text.encode()) <= 256
                or any(ord(c) < 32 or ord(c) == 127 for c in text)):
            raise ValueError("Invalid segmentation identity text")
    if segment_id == source["sourceId"]:
        raise ValueError("Segment must have a distinct source identity")
    parent = inspect_pcm_source(payload, expected_sha256=source["sourceSha256"], sample_rate=sample_rate)
    if (type(start_frame) is not int or type(end_frame) is not int
            or not 0 <= start_frame < end_frame <= parent["frameCount"]):
        raise ValueError("Segment interval must be inside the captured source")
    with wave.open(io.BytesIO(payload), "rb") as reader:
        reader.setpos(start_frame)
        pcm = reader.readframes(end_frame - start_frame)
    if len(pcm) != (end_frame - start_frame) * parent["sampleWidthBytes"]:
        raise ValueError("Segment PCM length differs from interval")
    output = io.BytesIO()
    with wave.open(output, "wb") as writer:
        writer.setnchannels(1)
        writer.setsampwidth(parent["sampleWidthBytes"])
        writer.setframerate(sample_rate)
        writer.writeframes(pcm)
    result = output.getvalue()
    child = inspect_pcm_source(result, expected_sha256=hashlib.sha256(result).hexdigest(), sample_rate=sample_rate)
    identity = {key: source[key] for key in ("songId", "sessionId", "lineageId")}
    record = dict(formatId="com.project-seam.training-segment", schemaVersion=1,
                  sourceId=segment_id, **identity, sourceSha256=child["sourceSha256"],
                  audioSha256=child["audioSha256"], sampleRate=sample_rate,
                  frameCount=child["frameCount"], parentSourceId=source["sourceId"],
                  parentSourceSha256=parent["sourceSha256"], parentAudioSha256=parent["audioSha256"],
                  transform=dict(kind="pcm-frame-crop", revision=1, startFrame=start_frame, endFrame=end_frame),
                  sourceRightsAdmitted=False, trainingAdmitted=False, releaseEligible=False)
    return result, record
