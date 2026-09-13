"""Label consistency checks and correction queue; never musical approval."""
import math


def score_report(score: dict, *, frame_count: int, phoneme_count: int, explicit_silence: bool = False) -> dict:
    """Validate explicit monophonic score supervision in the source sample clock.

    A syllable owns a contiguous phone range and one or more contiguous notes.
    Rest notes own no syllable. No F0-to-MIDI equality is imposed: expressive
    pitch and unvoiced consonants legitimately differ from the written score.
    """
    fields = {"language", "syllables", "notes"}
    if explicit_silence:
        fields.add("silencePhones")
    if not isinstance(score, dict) or set(score) != fields:
        raise ValueError("Invalid score supervision fields")
    silence = score["silencePhones"] if explicit_silence else []
    if (not isinstance(silence, list) or len(silence) > phoneme_count
            or any(type(i) is not int or not 0 <= i < phoneme_count for i in silence)
            or silence != sorted(set(silence))):
        raise ValueError("Silence phone indices must be unique, ordered and in bounds")
    silence_set = set(silence)
    if score["language"] not in ("ja", "en", "ko"):
        raise ValueError("Unsupported score language")
    syllables, notes = score["syllables"], score["notes"]
    if not isinstance(syllables, list) or not 1 <= len(syllables) <= 4096:
        raise ValueError("Invalid syllable count")
    next_phone = 0
    for syllable in syllables:
        if not isinstance(syllable, dict) or set(syllable) != {"lyric", "phoneStart", "phoneEnd"}:
            raise ValueError("Invalid syllable fields")
        lyric, start, end = syllable["lyric"], syllable["phoneStart"], syllable["phoneEnd"]
        if (not isinstance(lyric, str) or not 1 <= len(lyric.encode()) <= 1024
                or any(ord(c) < 32 or ord(c) == 127 for c in lyric)):
            raise ValueError("Invalid syllable lyric")
        while next_phone in silence_set:
            next_phone += 1
        if (type(start) is not int or type(end) is not int or start != next_phone
                or not start < end <= phoneme_count or any(i in silence_set for i in range(start, end))):
            raise ValueError("Syllables must partition the phoneme sequence")
        next_phone = end
    while next_phone in silence_set:
        next_phone += 1
    if next_phone != phoneme_count:
        raise ValueError("Syllables do not cover all phonemes")
    if not isinstance(notes, list) or not 1 <= len(notes) <= 8192:
        raise ValueError("Invalid score note count")
    next_frame, last_syllable, active_syllable, slurs = 0, -1, None, 0
    for note in notes:
        if not isinstance(note, dict) or set(note) != {"startFrame", "endFrame", "midi", "syllable", "slur"}:
            raise ValueError("Invalid score note fields")
        start, end, midi, syllable, slur = (note[k] for k in ("startFrame", "endFrame", "midi", "syllable", "slur"))
        if type(start) is not int or type(end) is not int or start != next_frame or not start < end <= frame_count:
            raise ValueError("Notes and explicit rests must partition the source frames")
        if type(slur) is not bool:
            raise ValueError("Slur must be boolean")
        if midi is None:
            if syllable is not None or slur:
                raise ValueError("Rest cannot carry a syllable or slur")
            active_syllable = None
        else:
            if type(midi) is not int or not 0 <= midi <= 127 or type(syllable) is not int or not 0 <= syllable < len(syllables):
                raise ValueError("Invalid pitched note identity")
            if slur:
                if active_syllable != syllable:
                    raise ValueError("Slur must continue the immediately preceding sung syllable")
                slurs += 1
            elif syllable != last_syllable + 1:
                raise ValueError("New notes must introduce syllables in order")
            else:
                last_syllable = syllable
            active_syllable = syllable
        next_frame = end
    if next_frame != frame_count or last_syllable != len(syllables) - 1:
        raise ValueError("Score does not cover the source and syllables")
    result = dict(language=score["language"], syllableCount=len(syllables), noteCount=len(notes), slurCount=slurs)
    if explicit_silence:
        result["silencePhoneCount"] = len(silence)
    return result


def label_report(label: dict, *, vocabulary: set[str], minimum_confidence: float) -> dict:
    required = {"sourceId", "frameCount", "hopSize", "phonemes", "f0Hz", "voiced", "reviewRevision"}
    if not isinstance(label, dict) or set(label) != required:
        raise ValueError("Unsupported label fields")
    if not isinstance(label["sourceId"], str) or not 1 <= len(label["sourceId"].encode()) <= 256:
        raise ValueError("Invalid label source identity")
    if type(minimum_confidence) not in (int, float) or not math.isfinite(minimum_confidence) or not 0 <= minimum_confidence <= 1:
        raise ValueError("Explicit confidence threshold must be between zero and one")
    frames, hop = label["frameCount"], label["hopSize"]
    if type(frames) is not int or not 1 <= frames <= 192000 * 600 or type(hop) is not int or not 1 <= hop <= 8192:
        raise ValueError("Invalid label frame geometry")
    size = (frames + hop - 1) // hop
    if size > 1000000 or not isinstance(label["f0Hz"], list) or not isinstance(label["voiced"], list):
        raise ValueError("Label feature budget exceeded")
    if len(label["f0Hz"]) != size or len(label["voiced"]) != size:
        raise ValueError("F0 and voicing must cover exactly the phrase's analysis frames")
    phones = label["phonemes"]
    if not isinstance(phones, list) or not 1 <= len(phones) <= 4096:
        raise ValueError("Invalid phoneme count")
    issues = []
    next_frame = 0
    for index, phone in enumerate(phones):
        if not isinstance(phone, dict) or set(phone) != {"symbol", "startFrame", "endFrame", "confidence"}:
            raise ValueError("Invalid phoneme label fields")
        start, end, confidence = phone["startFrame"], phone["endFrame"], phone["confidence"]
        if not isinstance(phone["symbol"], str) or phone["symbol"] not in vocabulary:
            issues.append(dict(code="unknown-phone", index=index))
        if type(start) is not int or type(end) is not int or start != next_frame or not start < end <= frames:
            raise ValueError("Phoneme alignment must be contiguous and inside the phrase")
        if type(confidence) not in (float, int) or not math.isfinite(confidence) or not 0 <= confidence <= 1:
            raise ValueError("Invalid alignment confidence")
        if confidence < minimum_confidence:
            issues.append(dict(code="low-alignment-confidence", index=index))
        next_frame = end
    if next_frame != frames:
        raise ValueError("Phonemes do not cover the complete phrase")
    for index, (f0, voiced) in enumerate(zip(label["f0Hz"], label["voiced"])):
        if type(voiced) is not bool or type(f0) not in (int, float) or not math.isfinite(f0) or not 0 <= f0 <= 20000:
            raise ValueError("Invalid F0 or voicing value")
        if voiced != (f0 > 0):
            # Reject excessive diagnostics rather than publishing a truncated queue.
            issues.append(dict(code="voicing-f0-mismatch", index=index))
            if len(issues) >= 8192:
                raise ValueError("Label correction count exceeds report budget")
    revision = label["reviewRevision"]
    if revision is not None and (not isinstance(revision, str) or not 1 <= len(revision.encode()) <= 256):
        raise ValueError("Invalid supplied review revision")
    if revision is None:
        issues.append(dict(code="review-revision-missing"))
    if len(issues) >= 8192:
        raise ValueError("Label correction count exceeds report budget")
    return dict(formatId="com.project-seam.training-label-report", schemaVersion=1,
                sourceId=label["sourceId"], minimumConfidence=minimum_confidence,
                correctionQueue=issues, consistencyPassed=not issues,
                reviewAuthenticated=False, trainingAdmitted=False, releaseEligible=False)
