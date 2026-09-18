"""Turn one procedural render into captured training inputs; no approval is implied.

The neural pipeline needs a source with permission, admitted labels and a vocoder
before it can train. A procedural render supplies the first two in a form no external
recording can: the renderer knows which phone it produced and when, so the phone
timeline is the renderer's own plan rather than an annotation someone had to make.

Two limits are load-bearing and must not be reported away.

* The emitted phone spans are the renderer's *intent*. They say which phone the engine
  meant to produce over a span, not that the audio acoustically contains that phone
  with that boundary. Training on them treats the teacher's articulation as the
  target, so they can measure whether a student reproduces the teacher and cannot
  establish that either one is phonetically correct.
* Copyright and permission evidence is still required. This module does not create a
  source permission, admit labels or authorize training.

The export is deliberately shaped as the exact label and score documents the existing
preparation pipeline already consumes, so no downstream stage needs a special case.
"""
from __future__ import annotations

import hashlib
import math
import re

from .audio_source import read_pcm_source, inspect_pcm_source
from .features import apply_pitch_features
from .labels import score_report


FORMAT_ID = "com.project-seam.training-generated-teacher"
SCHEMA_VERSION = 1


def _require_int(value: object, name: str, *, minimum: int = 0, maximum: int) -> int:
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"Generated teacher {name} is out of bounds")
    return value


def _require_text(value: object, name: str, *, maximum: int = 256) -> str:
    if (not isinstance(value, str) or not 1 <= len(value.encode()) <= maximum
            or any(ord(c) < 32 or ord(c) == 127 for c in value)):
        raise ValueError(f"Generated teacher {name} is invalid")
    return value


def pcm_sha256(payload: bytes) -> str:
    """Digest over the bytes exactly as they were captured."""
    return hashlib.sha256(payload).hexdigest()


def audio_sha256(payload: bytes) -> str:
    """Digest over the PCM payload and its geometry, matching the source inspector.

    Identical samples at a different clock are a different source, so the geometry is
    hashed with the payload rather than beside it.
    """
    source, _ = read_pcm_source(payload, expected_sha256=pcm_sha256(payload))
    return source["audioSha256"]


def build_label(*, source_id: str, hop_size: int, frame_count: int,
                phone_spans: list[dict], f0_hz: list[float], voiced: list[bool]) -> dict:
    """Assemble one label document in the schema the pipeline already admits.

    Phone spans must partition the phrase contiguously: the label schema treats a gap
    as an unspecified region rather than as silence, and the pipeline refuses a
    non-covering alignment instead of inferring one.
    """
    _require_text(source_id, "sourceId")
    hop = _require_int(hop_size, "hopSize", minimum=1, maximum=8192)
    frames = _require_int(frame_count, "frameCount", minimum=1, maximum=192000 * 600)
    analysis_frames = (frames + hop - 1) // hop
    if len(f0_hz) != analysis_frames or len(voiced) != analysis_frames:
        raise ValueError("Generated teacher F0 and voicing must cover the phrase's analysis frames")
    if not phone_spans:
        raise ValueError("Generated teacher needs at least one phone span")
    phonemes, next_frame = [], 0
    for span in phone_spans:
        if not isinstance(span, dict) or set(span) != {"symbol", "startFrame", "endFrame", "confidence"}:
            raise ValueError("Generated teacher phone span fields are invalid")
        symbol = _require_text(span["symbol"], "phone symbol")
        start = _require_int(span["startFrame"], "phone startFrame", maximum=frames)
        end = _require_int(span["endFrame"], "phone endFrame", maximum=frames)
        confidence = span["confidence"]
        if (type(confidence) not in (int, float) or not math.isfinite(confidence)
                or not 0 <= confidence <= 1):
            raise ValueError("Generated teacher alignment confidence is invalid")
        if start != next_frame or not start < end:
            raise ValueError("Generated teacher phone spans must be contiguous and non-empty")
        phonemes.append(dict(symbol=symbol, startFrame=start, endFrame=end, confidence=float(confidence)))
        next_frame = end
    if next_frame != frames:
        raise ValueError("Generated teacher phone spans must cover the complete phrase")
    features = []
    for f0, is_voiced in zip(f0_hz, voiced):
        if (type(is_voiced) is not bool or type(f0) not in (int, float)
                or not math.isfinite(f0) or not 0 <= f0 <= 20000):
            raise ValueError("Generated teacher F0 or voicing value is invalid")
        if is_voiced != (f0 > 0):
            raise ValueError("Generated teacher voicing must agree with its F0")
        features.append(float(f0))
    return dict(sourceId=source_id, frameCount=frames, hopSize=hop, phonemes=phonemes,
                f0Hz=features, voiced=list(voiced), reviewRevision=None)


def build_score(*, language: str, syllable_lyrics: list[str], note_spans: list[dict],
                frame_count: int, silence_indices: list[int]) -> dict:
    """Assemble the explicit score supervision document for the same phrase.

    Notes and explicit rests must partition the source frames, and each syllable owns a
    contiguous phone range, so the score says what the teacher intended to sing without
    asserting that the audio matches it.
    """
    if language not in ("ja", "en", "ko"):
        raise ValueError("Generated teacher score language is unsupported")
    if not syllable_lyrics or any(not isinstance(lyric, str) or not lyric for lyric in syllable_lyrics):
        raise ValueError("Generated teacher score needs non-empty syllable lyrics")
    # Silence indices are positions in the phone sequence, and only the assembled label knows how long
    # that sequence is, so they are passed through in order and the admitted score check enforces the
    # real bound against the label's own phoneme count.
    silence = _require_int_sequence(silence_indices)
    syllables, next_phone = [], 0
    for index, lyric in enumerate(syllable_lyrics):
        start = next_phone
        end = start + 1
        syllables.append(dict(lyric=lyric, phoneStart=start, phoneEnd=end))
        next_phone = end
    notes, next_frame, last_syllable = [], 0, -1
    for note in note_spans:
        if not isinstance(note, dict) or set(note) != {"startFrame", "endFrame", "midi", "syllable", "slur"}:
            raise ValueError("Generated teacher score note fields are invalid")
        start = _require_int(note["startFrame"], "note startFrame", maximum=frame_count)
        end = _require_int(note["endFrame"], "note endFrame", maximum=frame_count)
        if start != next_frame or not start < end:
            raise ValueError("Generated teacher score notes must partition the source frames")
        midi, syllable, slur = note["midi"], note["syllable"], note["slur"]
        if type(slur) is not bool:
            raise ValueError("Generated teacher slur must be boolean")
        if midi is None:
            if syllable is not None or slur:
                raise ValueError("A generated rest cannot carry a syllable or slur")
        else:
            _require_int(midi, "note midi", maximum=127)
            _require_int(syllable, "note syllable", maximum=len(syllables) - 1)
            if slur and syllable != last_syllable:
                raise ValueError("A generated slur must continue the preceding syllable")
            if not slur:
                last_syllable = syllable
        notes.append(dict(startFrame=start, endFrame=end, midi=midi, syllable=syllable, slur=slur))
        next_frame = end
    if next_frame != frame_count:
        raise ValueError("Generated teacher score does not cover the complete phrase")
    return dict(language=language, syllables=syllables, notes=notes, silencePhones=silence)


def _require_int_sequence(values: object) -> list[int]:
    if not isinstance(values, list):
        raise ValueError("Generated teacher silence indices must be a list")
    result = []
    for index, value in enumerate(values):
        if type(value) is not int or value < 0 or (index and value <= values[index - 1]):
            raise ValueError("Generated teacher silence indices must be unique and ordered")
        result.append(value)
    return result


def build_export(*, source_id: str, song_id: str, session_id: str, lineage_id: str,
                 sample_rate: int, hop_size: int, frame_count: int,
                 phone_spans: list[dict], f0_hz: list[float], voiced: list[bool],
                 score: dict, pcm_payload: bytes,
                 recipe_sha256: str, engine_id: str, engine_revision: int,
                 score_sha256: str) -> dict:
    """Assemble the captured export a preparation step can read.

    Identities are carried through so a reviewer can bind rights, labels, recipe and
    audio to the same material. Nothing here is a permission: ``sourceRightsAdmitted``
    and ``trainingAdmitted`` stay false, and a later admission step must still verify a
    real review against these bytes.
    """
    label = build_label(source_id=source_id, hop_size=hop_size, frame_count=frame_count,
                        phone_spans=phone_spans, f0_hz=f0_hz, voiced=voiced)
    if score.get("language") not in ("ja", "en", "ko"):
        raise ValueError("Generated teacher score language is unsupported")
    _require_int(sample_rate, "sampleRate", minimum=8000, maximum=192000)
    _require_int(engine_revision, "engineRevision", minimum=1, maximum=1_000_000)
    for name, value in (("recipeSha256", recipe_sha256), ("scoreSha256", score_sha256)):
        if not isinstance(value, str) or len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
            raise ValueError(f"Generated teacher {name} must be a lowercase SHA-256")
    _require_text(engine_id, "engineId")
    captured = inspect_pcm_source(pcm_payload, expected_sha256=pcm_sha256(pcm_payload), sample_rate=sample_rate)
    if captured["frameCount"] != frame_count:
        raise ValueError("Generated teacher declared frames differ from captured WAV")
    return dict(
        formatId=FORMAT_ID, schemaVersion=SCHEMA_VERSION,
        sourceId=_require_text(source_id, "sourceId"),
        songId=_require_text(song_id, "songId"),
        sessionId=_require_text(session_id, "sessionId"),
        lineageId=_require_text(lineage_id, "lineageId"),
        sampleRate=sample_rate, hopSize=hop_size, frameCount=frame_count,
        sourceSha256=captured["sourceSha256"], audioSha256=captured["audioSha256"],
        recipeSha256=recipe_sha256, scoreSha256=score_sha256,
        engineId=engine_id, engineRevision=engine_revision,
        label=label, score=score,
        labelOrigin="renderer-intent-not-acoustic-truth",
        sourceRightsAdmitted=False, labelsAdmitted=False, trainingAdmitted=False,
        releaseEligible=False,
    )


def label_config_from_exports(*, exports: list[dict], sample_rate: int,
                              relative_paths: dict[str, str], vocabulary: list[str],
                              minimum_confidence: float) -> dict:
    """Translate captured teacher exports into the label config the pipeline already admits.

    The exported audio must already exist inside the source root the preparation step will read, so
    each export names its own path relative to that root. This builds the document only; the admitted
    label check still runs on the result, and the rights and annotation review are still separate.
    """
    _require_int(sample_rate, "sampleRate", minimum=8000, maximum=192000)
    if not isinstance(exports, list) or not 1 <= len(exports) <= 10000:
        raise ValueError("Generated teacher label config needs 1..10000 exports")
    if (not isinstance(vocabulary, list) or not 1 <= len(vocabulary) <= 4096
            or any(not isinstance(s, str) or not 1 <= len(s.encode()) <= 256 for s in vocabulary)
            or len(set(vocabulary)) != len(vocabulary)):
        raise ValueError("Generated teacher vocabulary must be ordered and unique")
    if (type(minimum_confidence) not in (int, float) or not math.isfinite(minimum_confidence)
            or not 0 <= minimum_confidence <= 1):
        raise ValueError("Generated teacher confidence threshold must be between zero and one")
    sources, labels, seen = [], [], set()
    for export in exports:
        if not isinstance(export, dict) or export.get("formatId") != FORMAT_ID:
            raise ValueError("Generated teacher label config requires captured export documents")
        source_id = export["sourceId"]
        if source_id in seen:
            raise ValueError("Generated teacher exports must have unique source identities")
        seen.add(source_id)
        if export["sampleRate"] != sample_rate:
            raise ValueError("Generated teacher export sample rate differs from the config")
        path = relative_paths.get(source_id)
        if not isinstance(path, str) or not path:
            raise ValueError("Every generated teacher export needs a source-root-relative path")
        sources.append(dict(sourceId=source_id, songId=export["songId"], sessionId=export["sessionId"],
                            lineageId=export["lineageId"], path=path, sourceSha256=export["sourceSha256"]))
        # The score's silence indices are positions in the phone sequence, and the assembled label is
        # what fixes that sequence's length, so the label is rebuilt here rather than trusted as text.
        label = build_label(source_id=source_id, hop_size=export["hopSize"],
                            frame_count=export["frameCount"],
                            phone_spans=export["label"]["phonemes"],
                            f0_hz=export["label"]["f0Hz"], voiced=export["label"]["voiced"])
        labels.append(dict(sourceSha256=export["sourceSha256"], audioSha256=export["audioSha256"],
                           label=label, score=export["score"]))
    labels.sort(key=lambda item: item["label"]["sourceId"])
    sources.sort(key=lambda item: item["sourceId"])
    # Exactly the fields the admitted label configuration defines. The teaching-material origin and
    # the fact that nothing is admitted live in the captured exports, not here: adding an extra key
    # would be refused by the pipeline, and the origin of the spans is a property of the material
    # rather than of the configuration that points at it.
    return dict(formatId="com.project-seam.voice-training-label-config", schemaVersion=3,
                sampleRate=sample_rate, sources=sources, labels=labels,
                vocabulary=list(vocabulary), minimumConfidence=float(minimum_confidence))


def export_from_candidate(*, candidate: dict, pitch_features: dict, source_id: str,
                          song_id: str, session_id: str, lineage_id: str,
                          syllable_lyrics: list[str], note_midi: list[int | None],
                          pcm_payload: bytes) -> dict:
    """Build a teacher export from a real procedural candidate and its measured pitch.

    ``candidate`` is the metadata the export writes beside a baked candidate: it carries the
    renderer's own phone markers, each with the span it owned, and the audio digest. ``pitch_features``
    is the native extractor's full-hop measurement of that same audio. The phone spans are taken from
    the renderer's markers rather than from the score, because the markers are what the engine
    actually produced, and they are still intent rather than acoustic proof.

    The measured F0 is used, not the written score, because the teacher is an expressive synthesizer
    and a student should learn what it sang rather than what the note asked for.

    Lyric/MIDI inputs describe captured notes, not individual phones. Canonical marker keys retain
    note ownership across consonants, vowels and continuation notes. Intervals here follow renderer
    ownership, not the original piano-roll clock; anticipated consonants can move those boundaries.
    Unkeyed legacy input is accepted only for unambiguous one-phone-per-note captures.
    """
    if not isinstance(candidate, dict) or candidate.get("formatId") != "com.project-seam.procedural-candidate":
        raise ValueError("Generated teacher requires a captured procedural candidate")
    if candidate.get("approval") != "unapproved":
        raise ValueError("A captured candidate must still be unapproved when it is used as teaching material")
    digest = pcm_sha256(pcm_payload)
    # The native candidate field hashes the entire WAV, unlike the training inspector's audioSha256.
    if candidate.get("audioSha256") != digest:
        raise ValueError("Captured candidate audio hash differs from the source WAV")
    markers = candidate.get("markers")
    if not isinstance(markers, list) or not 1 <= len(markers) <= 4096:
        raise ValueError("Captured candidate markers are missing or oversized")
    frame_count = _require_int(candidate.get("frameCount"), "candidate frameCount", maximum=192000 * 600)
    spans = []
    for marker in markers:
        if not isinstance(marker, dict) or "phone" not in marker:
            raise ValueError("Captured candidate marker is malformed")
        spans.append(dict(symbol=_require_text(marker["phone"], "marker phone"),
                          startFrame=_require_int(marker["startFrame"], "marker startFrame", maximum=frame_count),
                          endFrame=_require_int(marker["endFrame"], "marker endFrame", maximum=frame_count),
                          # The renderer's plan is exact about which phone it produced, so alignment
                          # confidence is fully 1.0 in the span sense even though the span is not
                          # proof of an acoustic boundary.
                          confidence=1.0))
    count = (frame_count + 255) // 256
    if count > 65536:
        raise ValueError("Captured candidate exceeds the pitch feature budget")
    seed_label = build_label(source_id=source_id, hop_size=256, frame_count=frame_count,
                             phone_spans=spans, f0_hz=[0.0] * count, voiced=[False] * count)
    measured = apply_pitch_features(seed_label, pitch_features, source_sha256=digest,
                                    sample_rate=candidate["sampleRate"],
                                    vocabulary={span["symbol"] for span in spans}, minimum_confidence=0.0)
    groups, seen_notes, seen_keys = [], set(), set()
    keyed = ["key" in marker for marker in markers]
    if any(keyed) and not all(keyed):
        raise ValueError("Captured candidate cannot mix keyed and unkeyed markers")
    previous_note, previous_ordinal = None, -1
    for index, marker in enumerate(markers):
        if all(keyed):
            key = marker["key"]
            if not isinstance(key, str) or not re.fullmatch(r"[0-9a-f]{16}:(0|[1-9][0-9]{0,4})", key):
                raise ValueError("Captured candidate marker key is not canonical")
            note, ordinal_text = key.split(":")
            ordinal = int(ordinal_text)
            if int(note, 16) == 0 or ordinal >= 16384 or key in seen_keys:
                raise ValueError("Captured candidate marker key is invalid or duplicated")
            seen_keys.add(key)
        else:
            note, ordinal = index, 0
        if note != previous_note:
            if note in seen_notes:
                raise ValueError("Captured candidate note ownership must be contiguous")
            seen_notes.add(note)
            groups.append([index, index + 1])
            previous_ordinal = -1
        else:
            groups[-1][1] = index + 1
        if ordinal <= previous_ordinal:
            raise ValueError("Captured candidate phone ordinals must increase within a note")
        previous_note, previous_ordinal = note, ordinal
    if (not isinstance(syllable_lyrics, list) or not isinstance(note_midi, list)
            or len(groups) != len(note_midi) or len(groups) != len(syllable_lyrics)):
        raise ValueError("Generated teacher needs one lyric and MIDI value per captured note")
    notes, syllables, silence, active_syllable = [], [], [], None
    for (first, end_phone), lyric, midi in zip(groups, syllable_lyrics, note_midi):
        start, end = spans[first]["startFrame"], spans[end_phone - 1]["endFrame"]
        if midi is None:
            notes.append(dict(startFrame=start, endFrame=end, midi=None, syllable=None, slur=False))
            silence.extend(range(first, end_phone))
            active_syllable = None
        else:
            _require_text(lyric, "note lyric")
            continuation = lyric in ("-", "ー", "〜")
            if continuation:
                if active_syllable is None:
                    raise ValueError("Captured continuation needs an immediately preceding sung syllable")
                syllables[active_syllable]["phoneEnd"] = end_phone
            else:
                active_syllable = len(syllables)
                syllables.append(dict(lyric=lyric, phoneStart=first, phoneEnd=end_phone))
            notes.append(dict(startFrame=start, endFrame=end, midi=_require_int(midi, "note midi", maximum=127),
                              syllable=active_syllable, slur=continuation))
    score = dict(language="ja", syllables=syllables, notes=notes, silencePhones=silence)
    score_report(score, frame_count=frame_count, phoneme_count=len(spans), explicit_silence=True)
    return build_export(source_id=source_id, song_id=song_id, session_id=session_id,
                        lineage_id=lineage_id, sample_rate=candidate["sampleRate"],
                        hop_size=256, frame_count=frame_count, phone_spans=spans,
                        f0_hz=measured["f0Hz"], voiced=measured["voiced"], score=score, pcm_payload=pcm_payload,
                        recipe_sha256=candidate["recipeHash"], engine_id="seam.source-filter.v1",
                        # The captured candidate records the renderer revision that produced the audio,
                        # and that revision is part of what the material is. Defaulting a missing one
                        # would attribute the audio to a revision nobody recorded.
                        engine_revision=_require_int(candidate["proceduralRevision"], "proceduralRevision",
                                                     minimum=1, maximum=1_000_000),
                        score_sha256=candidate["renderContentHash"])
