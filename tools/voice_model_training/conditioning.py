"""Expand reviewed label geometry onto its analysis clock; no admission side effect."""
from .labels import label_report, score_report


def build_conditioning(label: dict, score: dict, *, vocabulary: list[str],
                       minimum_confidence: float, maximum_frames: int = 65536,
                       breathiness: list[float] | None = None) -> dict:
    """Use left-edge sample positions and half-open phone/note intervals.

    Token zero is reserved for padding; vocabulary order defines positive IDs.
    Rest is a separate mask, so MIDI zero remains a legitimate pitched note.
    Phone and note clocks remain independent (consonants may anticipate notes).
    This pure transform neither authenticates a review nor loads source audio.
    """
    if (not isinstance(vocabulary, list) or not 1 <= len(vocabulary) <= 4096
            or any(not isinstance(s, str) or not 1 <= len(s.encode()) <= 256 for s in vocabulary)
            or len(set(vocabulary)) != len(vocabulary)):
        raise ValueError("Conditioning requires an ordered unique vocabulary")
    if type(maximum_frames) is not int or not 1 <= maximum_frames <= 1000000:
        raise ValueError("Invalid conditioning frame budget")
    report = label_report(label, vocabulary=set(vocabulary), minimum_confidence=minimum_confidence)
    if any(issue["code"] != "review-revision-missing" for issue in report["correctionQueue"]):
        raise ValueError("Resolve label corrections before constructing conditioning")
    score_report(score, frame_count=label["frameCount"], phoneme_count=len(label["phonemes"]), explicit_silence=True)
    count = len(label["f0Hz"])
    if count > maximum_frames:
        raise ValueError("Conditioning frame budget exceeded")
    if breathiness is not None:
        if not isinstance(breathiness, list) or len(breathiness) != count:
            raise ValueError("Breathiness conditioning length must match frame count")
        if any(type(v) not in (int, float) or not 0.0 <= v <= 1.0 for v in breathiness):
            raise ValueError("Breathiness values must be normalized floats in [0, 1]")
    tokens = {symbol: index + 1 for index, symbol in enumerate(vocabulary)}
    phones, notes = label["phonemes"], score["notes"]
    phone_index = note_index = 0
    rows = []
    for index in range(count):
        sample = index * label["hopSize"]
        while sample >= phones[phone_index]["endFrame"]:
            phone_index += 1
        while sample >= notes[note_index]["endFrame"]:
            note_index += 1
        phone, note = phones[phone_index], notes[note_index]
        rows.append(dict(sourceFrame=sample, validSamples=min(label["hopSize"], label["frameCount"] - sample),
                         phoneIndex=phone_index, phoneId=tokens[phone["symbol"]], noteIndex=note_index,
                         midi=note["midi"], rest=note["midi"] is None, slur=note["slur"],
                         syllable=note["syllable"], f0Hz=label["f0Hz"][index], voiced=label["voiced"][index],
                         breathiness=float(breathiness[index]) if breathiness is not None else 0.0))
    return dict(formatId="com.project-seam.training-frame-conditioning", schemaVersion=2,
                sourceId=label["sourceId"], hopSize=label["hopSize"], sampleCount=label["frameCount"],
                frameAnchor="left-edge", vocabulary=list(vocabulary), paddingId=0,
                language=score["language"], frames=rows, hasBreathiness=bool(breathiness is not None),
                conditioningRevision=2,
                trainingAdmitted=False, releaseEligible=False)
