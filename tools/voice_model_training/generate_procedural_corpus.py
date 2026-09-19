"""Author a procedural singing corpus through the production pilot export path.

The training corpus started at six songs, about seven seconds of audio, which is far
too little for an acoustic model to learn anything: a model trained on it produced
audio with no measurable pitch at all. This generates the many short phrases such a
model needs, from the project's own renderer, so no third-party recording enters the
corpus and every sample carries a receipt binding its project, audio and recipe.

It writes a corpus configuration in the shape ``prepare_corpus`` consumes, and it
refuses to overwrite. Nothing here grants source rights, admits labels or claims a
usable voice; the operator still declares scopes and signs the reviews.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

# A syllable inventory the procedural engine already renders. Choosing only these
# keeps the corpus inside the engine's demonstrated articulations rather than
# inventing coverage the renderer has not been shown to support.
SYLLABLES = ("あ", "い", "う", "え", "お", "か", "き", "く", "け", "こ", "さ", "す", "せ", "そ",
             "た", "ち", "つ", "て", "と", "な", "に", "ぬ", "ね", "の", "は", "ひ", "ふ", "へ",
             "ほ", "ま", "み", "む", "め", "も", "ら", "り", "る", "れ", "ろ", "わ", "ん")
# A pentatonic-ish range around female singing pitch, varied per phrase so the model
# sees melody rather than one repeated note.
SCALE = (60, 62, 64, 67, 69, 71, 72, 74, 76, 79)
MINIMUM_TICKS, MAXIMUM_TICKS = 480, 61440


def _phrase(seed, index, notes, include_pauses=False):
    """Deterministic lyrics, melody and durations for one phrase."""
    # A small per-phrase random stream keeps the corpus reproducible from its seed
    # without making every song share the caller's global RNG state.
    digest = hashlib.sha256(f"{seed}:{index}".encode("utf-8")).digest()
    tokens = []
    for position in range(notes):
        window = digest[position % len(digest)] + (position * 7)
        syllable = SYLLABLES[(window + position) % len(SYLLABLES)]
        midi = SCALE[(window * 3 + position * 5) % len(SCALE)]
        # Mix note lengths so the model sees sustained and short notes rather than
        # a uniform grid that would teach it only one duration. The shortest value
        # must still leave room for the engine's widest articulation: an affricate
        # needs a released closure plus its frication tail, and the pilot refuses a
        # note that cannot hold both rather than truncating the phone.
        duration = (480, 720, 960)[(window + position * 3) % 3]
        tokens.append(f"{syllable}:{midi}:{duration}")
    if include_pauses:
        if notes < 3:
            raise ValueError("Pause examples require at least three score events")
        # Replace one interior event; preserve the bounded event count and
        # duration. Surrounding sung events establish attack/release context.
        position = notes // 2
        _, midi, duration = tokens[position].split(":")
        tokens[position] = f"pau:{midi}:{duration}"
    return tokens


def _total_ticks(tokens):
    return sum(int(token.rsplit(":", 1)[1]) for token in tokens)


def generate(*, pilot, output, count, seed, extractor, training_scopes, notes=16, start=0,
             include_pauses=False):
    """Render ``count`` phrases and write the corpus configuration last."""
    pilot = Path(pilot)
    if not pilot.is_file() or not os.access(pilot, os.X_OK):
        raise ValueError("Pilot executable must be an existing executable file")
    output = Path(output)
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError("Corpus output must be new with an existing parent")
    if type(count) is not int or not 1 <= count <= 5000:
        raise ValueError("Corpus size must be between 1 and 5000 phrases")
    if (not isinstance(seed, str) or not 1 <= len(seed.encode()) <= 256
            or any(ord(c) < 32 or ord(c) == 127 for c in seed)):
        raise ValueError("Corpus seed must be bounded printable text")
    if type(notes) is not int or not 1 <= notes <= 64:
        raise ValueError("Phrase notes must be between 1 and 64")
    if type(include_pauses) is not bool or (include_pauses and notes < 3):
        raise ValueError("Pause examples require a boolean option and at least three events")
    if type(start) is not int or start < 0:
        raise ValueError("Corpus start index must be a non-negative integer")
    if not isinstance(extractor, str) or not extractor:
        raise ValueError("A native pitch extractor path is required for the corpus configuration")
    # Scopes are the operator's own declaration and are never defaulted here.
    from .permissions import TRAINING_PERMISSIONS
    if (not isinstance(training_scopes, list) or not training_scopes
            or len(set(training_scopes)) != len(training_scopes)
            or any(scope not in TRAINING_PERMISSIONS for scope in training_scopes)):
        raise ValueError("Declare at least one known training scope for the corpus")
    output.mkdir(mode=0o700)
    songs, rendered, skipped = [], 0, 0
    for index in range(start, start + count):
        tokens = _phrase(seed, index, notes, include_pauses)
        # The pilot refuses a phrase longer than its own declared bound; keep the
        # corpus inside that bound instead of discovering it as a render failure.
        if not MINIMUM_TICKS <= _total_ticks(tokens) <= MAXIMUM_TICKS:
            skipped += 1
            continue
        export_root = output / f"phrase-{index:05d}"
        completed = subprocess.run([str(pilot), str(export_root), "phrase", *tokens],
                                   capture_output=True, text=True, timeout=600)
        if completed.returncode != 0:
            raise ValueError(f"Pilot render failed for phrase {index}: "
                             + completed.stderr[-200:].strip())
        take = export_root / "baseline"
        receipt = take / "receipt.json"
        if not receipt.is_file():
            raise ValueError(f"Pilot did not commit a receipt for phrase {index}")
        # The candidate path encodes the track and region identity, so it is read
        # from the committed export rather than assumed from the pilot's internals.
        candidates = sorted((take / "candidates").glob("*.json"))
        if len(candidates) != 1:
            raise ValueError(f"Phrase {index} must commit exactly one candidate")
        source_id = f"procedural-song-{index:05d}"
        songs.append(dict(exportRoot=str(take.resolve()),
                          receiptSha256=hashlib.sha256(receipt.read_bytes()).hexdigest(),
                          candidatePath="candidates/" + candidates[0].name,
                          sourceId=source_id, songId=source_id,
                          sessionId=f"corpus-session-{index:05d}",
                          lineageId=f"procedural-render-{index:05d}"))
        rendered += 1
    if not songs:
        raise ValueError("No phrase was rendered; check the seed and note count")
    # One held-out song is chosen by index, not by any measured outcome, so the
    # split cannot be selected to flatter a candidate.
    held_out = [songs[-1]["songId"]]
    configuration = dict(formatId="com.project-seam.captured-teacher-corpus-config", schemaVersion=1,
        seed=f"procedural-corpus-{seed}", extractor=extractor,
        songs=songs, heldOutSongIds=held_out, trainingScopes=sorted(training_scopes))
    publish = output / "corpus-sources.json"
    with publish.open("xb") as stream:
        stream.write((json.dumps(configuration, ensure_ascii=False, sort_keys=True, indent=2) + "\n").encode())
    return dict(songs=len(songs), skipped=skipped, heldOutSongIds=held_out,
                sourcesPath=str(publish), sourcesSha256=hashlib.sha256(publish.read_bytes()).hexdigest())


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pilot", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--count", type=int, required=True)
    parser.add_argument("--seed", required=True)
    parser.add_argument("--extractor", required=True,
                        help="Trusted native CLI used later for measured pitch")
    parser.add_argument("--training-scope", action="append", dest="training_scopes", required=True,
                        help="Operator-declared scope; repeat once per scope")
    parser.add_argument("--notes", type=int, default=16)
    parser.add_argument("--start", type=int, default=0)
    parser.add_argument("--include-pauses", action="store_true",
                        help="Render an explicit interior pau event in every phrase")
    args = parser.parse_args(argv)
    try:
        print(json.dumps(generate(**vars(args)), ensure_ascii=False))
        return 0
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(f"Corpus generation failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
