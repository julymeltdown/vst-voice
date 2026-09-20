"""Conservative score fingerprints for the generated, single-region pilot.

Voice recipe identity is deliberately separate from musical content. These
fingerprints detect exact and transposed/time-scaled reuse; absence does not
prove semantic, phonetic, speaker or audio independence.
"""
import hashlib
import json
import math
import unicodedata
from fractions import Fraction


def fingerprint(events, *, ppq=960):
    if (type(ppq) is not int or not 1 <= ppq <= 15360
            or not isinstance(events, list) or not 1 <= len(events) <= 64):
        raise ValueError("Expected a bounded pilot phrase and positive PPQ")
    normalized, end = [], 0
    for event in events:
        if not isinstance(event, dict) or set(event) != {"language", "lyric", "midi", "startTick", "durationTick"}:
            raise ValueError("Invalid pilot phrase event fields")
        language, lyric, midi, start, duration = (event[key] for key in
            ("language", "lyric", "midi", "startTick", "durationTick"))
        if (language not in ("ja", "en", "ko") or not isinstance(lyric, str)
                or not 1 <= len(lyric.encode()) <= 1024 or any(ord(c) < 32 or ord(c) == 127 for c in lyric)
                or type(start) is not int or type(duration) is not int
                or not end <= start <= 61440 or not 1 <= duration <= 61440 or start + duration > 61440
                or (midi is not None and (type(midi) is not int or not 0 <= midi <= 127))
                or (lyric == "pau") != (midi is None)):
            raise ValueError("Invalid ordered, monophonic pilot phrase")
        normalized.append(dict(event, lyric=unicodedata.normalize("NFC", lyric)))
        end = start + duration
    origin = normalized[0]["startTick"]
    pitched = [event["midi"] for event in normalized if event["midi"] is not None]
    if not pitched:
        raise ValueError("Pilot phrase must contain a pitched event")
    unit = 0
    for event in normalized:
        event["startTick"] -= origin
        unit = math.gcd(unit, event["startTick"])
        unit = math.gcd(unit, event["durationTick"])
    def hashed(value):
        return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False,
                                         separators=(",", ":"), allow_nan=False).encode()).hexdigest()
    exact = [dict(event, startTick=str(Fraction(event["startTick"], ppq)),
                  durationTick=str(Fraction(event["durationTick"], ppq))) for event in normalized]
    family = [dict(event, startTick=event["startTick"] // unit, durationTick=event["durationTick"] // unit,
                   midi=None if event["midi"] is None else event["midi"] - pitched[0]) for event in normalized]
    return dict(scoreExactSha256=hashed(exact), scoreFamilySha256=hashed(family),
        lyricSequenceSha256=hashed([[event["language"], event["lyric"]] for event in normalized]),
        melodyRhythmSha256=hashed([{key: event[key] for key in ("startTick", "durationTick", "midi")} for event in family]))


def project_events(project):
    """Read only the bounded single-track/single-region procedural pilot shape.

    Performance/automation is intentionally not part of a content fingerprint:
    expressive edits must not disguise reuse of the underlying phrase.
    """
    if not isinstance(project, dict):
        raise ValueError("Expected a pilot project object")
    tracks = project.get("vocalTracks")
    if (not isinstance(tracks, list) or len(tracks) != 1
            or not isinstance(tracks[0], dict)
            or not isinstance(tracks[0].get("regions"), list)
            or len(tracks[0]["regions"]) != 1
            or not isinstance(tracks[0]["regions"][0], dict)):
        raise ValueError("Expected a single-track, single-region pilot project")
    region = tracks[0]["regions"][0]
    lyrics = region.get("lyrics")
    if not isinstance(lyrics, list) or not 1 <= len(lyrics) <= 64:
        raise ValueError("Expected bounded pilot lyrics")
    if any(not isinstance(row, dict) or not {"id", "language", "surface"} <= row.keys()
           or not isinstance(row["id"], str) or not row["id"] for row in lyrics):
        raise ValueError("Invalid pilot lyric row")
    by_id = {row["id"]: row for row in lyrics}
    if len(by_id) != len(lyrics):
        raise ValueError("Duplicate pilot lyric identity")
    notes = region.get("notes")
    if not isinstance(notes, list) or not 1 <= len(notes) <= 64:
        raise ValueError("Expected bounded pilot notes")
    events = []
    for note in notes:
        if (not isinstance(note, dict)
                or not {"lyricId", "midiKey", "startTick", "durationTick"} <= note.keys()
                or not isinstance(note["lyricId"], str)):
            raise ValueError("Invalid pilot note row")
        if note.get("lyricId") not in by_id:
            raise ValueError("Pilot note has no captured lyric")
        lyric = by_id[note["lyricId"]]
        events.append(dict(language=lyric["language"], lyric=lyric["surface"],
            midi=None if lyric["surface"] == "pau" else note["midiKey"],
            startTick=note["startTick"], durationTick=note["durationTick"]))
    fingerprint(events, ppq=project.get("ppq"))
    return events


def token_events(tokens):
    """Convert the generator's pre-render ja: pitch:duration event tokens."""
    if not isinstance(tokens, list) or not 1 <= len(tokens) <= 64:
        raise ValueError("Expected bounded generated tokens")
    events, start = [], 0
    for token in tokens:
        if not isinstance(token, str) or len(token) > 128:
            raise ValueError("Invalid generated token")
        lyric, midi, duration = token.split(":")
        duration = int(duration)
        events.append(dict(language="ja", lyric=lyric, midi=None if lyric == "pau" else int(midi),
                           startTick=start, durationTick=duration))
        start += duration
    fingerprint(events)
    return events
