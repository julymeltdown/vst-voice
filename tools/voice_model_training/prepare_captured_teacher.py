"""Prepare one receipt-bound Japanese procedural capture, without admitting training.

Uses the existing ExportService receipt, captured markers and native measured F0.
It neither reconstructs a renderer hash nor treats one song as independent splits.
The receipt digest is an integrity anchor supplied by the caller, not a signature.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys

from .__main__ import encode_report, inspect_label_config, load_config, publish_new
from .conditioning import build_conditioning
from .generated_teacher import export_from_candidate, label_config_from_exports
from .native_features import extract_pitch
from .features import rest_pitch_diagnostics


def _path(root, relative):
    if (not isinstance(relative, str) or not 1 <= len(relative) <= 1024
            or "\\" in relative or ":" in relative
            or any(part in ("", ".", "..") for part in relative.split("/"))):
        raise ValueError("Export paths must be canonical and contained")
    path = root
    for part in relative.split("/"):
        path /= part
        if path.is_symlink():
            raise ValueError("Export paths cannot contain symlinks")
    if not path.resolve(strict=True).is_relative_to(root):
        raise ValueError("Export path escapes its root")
    return path


def _bytes(path, digest, limit):
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    with os.fdopen(os.open(path, flags), "rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or not 1 <= before.st_size <= limit:
            raise ValueError("Export input is not a bounded regular file")
        payload = stream.read(limit + 1)
        after = os.fstat(stream.fileno())
    if (len(payload) != before.st_size or len(payload) > limit
            or (before.st_size, before.st_mtime_ns, before.st_ctime_ns)
            != (after.st_size, after.st_mtime_ns, after.st_ctime_ns)
            or hashlib.sha256(payload).hexdigest() != digest):
        raise ValueError("Export input changed or differs from its receipt digest")
    return payload


def _id(value):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{1,16}", value) or int(value, 16) == 0:
        raise ValueError("Project identity is invalid")
    return int(value, 16)


def _index(rows):
    if not isinstance(rows, list) or not 1 <= len(rows) <= 16384:
        raise ValueError("Project collection is missing or exceeds bounds")
    result = {}
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError("Project entry must be an object")
        key = _id(row.get("id"))
        if key in result:
            raise ValueError("Duplicate project identity")
        result[key] = row
    return result


def capture_inputs(root, receipt_sha256, candidate_path):
    """Capture selected project/metadata/audio/recipe from one committed export."""
    root = Path(root).resolve(strict=True)
    match = re.fullmatch(r"candidates/([0-9a-f]{16})-([0-9a-f]{16})\.json", candidate_path)
    if match is None:
        raise ValueError("Select a canonical candidates/TRACK-REGION.json export")
    receipt = load_config(_path(root, "receipt.json"), receipt_sha256)
    if (not isinstance(receipt, dict) or type(receipt.get("schemaVersion")) is not int
            or receipt["schemaVersion"] != 2 or receipt.get("state") != "COMMITTED"
            or receipt.get("includesProjectAndRecipes") is not True
            or receipt.get("includesProceduralCandidates") is not True
            or type(receipt.get("sampleRate")) is not int or receipt["sampleRate"] != 48000):
        raise ValueError("Require a committed 48 kHz project-and-candidate export receipt v2")
    rows = receipt.get("files")
    if not isinstance(rows, list) or not 1 <= len(rows) <= 4096:
        raise ValueError("Export receipt file inventory is invalid")
    files = {}
    for row in rows:
        if (not isinstance(row, dict) or not isinstance(row.get("path"), str)
                or row["path"] in files or not isinstance(row.get("sha256"), str)
                or not re.fullmatch(r"[0-9a-f]{64}", row["sha256"])):
            raise ValueError("Export receipt entries must have unique paths and SHA-256 digests")
        files[row["path"]] = row["sha256"]
    def read(name, limit=8 * 1024**2, binary=False):
        if name not in files:
            raise ValueError("Selected input is absent from the export receipt")
        path = _path(root, name)
        return _bytes(path, files[name], limit) if binary else load_config(path, files[name])
    project, candidate = read("project.seam"), read(candidate_path)
    if (not isinstance(project, dict) or project.get("formatId") != "com.project-seam.project"
            or type(project.get("schemaVersion")) is not int or not 17 <= project["schemaVersion"] <= 18
            or not isinstance(candidate, dict)):
        raise ValueError("Require captured project schema 17/18 and candidate metadata")
    if _id(project.get("projectId")) != _id(receipt.get("projectId")):
        raise ValueError("Receipt and project identity differ")
    track = _index(project.get("vocalTracks")).get(int(match[1], 16))
    if track is None:
        raise ValueError("Candidate track is absent from captured project")
    region = _index(track.get("regions")).get(int(match[2], 16))
    if region is None:
        raise ValueError("Candidate region is absent from captured project")
    recipe = track.get("proceduralRecipe")
    if (not isinstance(recipe, dict) or recipe.get("contentHash") != candidate.get("recipeHash")
            or recipe.get("id") != candidate.get("recipeId")
            or recipe.get("version") != candidate.get("recipeVersion")
            or recipe.get("style") != candidate.get("style")):
        raise ValueError("Captured track and candidate recipe identity differ")
    recipe_path = recipe.get("path")
    if not isinstance(recipe_path, str) or files.get(recipe_path) != recipe["contentHash"]:
        raise ValueError("Recipe must be byte-bound in the same export receipt")
    read(recipe_path, binary=True)
    notes, lyrics = _index(region.get("notes")), _index(region.get("lyrics"))
    markers = candidate.get("markers")
    if not isinstance(markers, list) or not 1 <= len(markers) <= 4096:
        raise ValueError("Captured markers are missing or exceed bounds")
    captured_ids, captured_phones = [], {}
    for marker in markers:
        key = marker.get("key") if isinstance(marker, dict) else None
        if not isinstance(key, str) or not re.fullmatch(r"[0-9a-f]{16}:(0|[1-9][0-9]{0,4})", key):
            raise ValueError("Preparation requires canonical captured note keys")
        note_id = int(key.split(":")[0], 16)
        captured_phones.setdefault(note_id, []).append(marker.get("phone"))
        if not captured_ids or captured_ids[-1] != note_id:
            captured_ids.append(note_id)
    midi, surfaces = [], []
    for identity in captured_ids:
        note = notes.get(identity)
        if note is None:
            raise ValueError("Captured note is absent from receipt-bound project")
        lyric = lyrics.get(_id(note.get("lyricId")))
        if lyric is None or lyric.get("language") != "ja":
            raise ValueError("Captured-teacher preparation currently requires explicit Japanese lyrics")
        is_pause = note.get("phoneticHint") == "pau"
        if is_pause and any(phone != "pau" for phone in captured_phones[identity]):
            raise ValueError("Captured pause hint and rendered phones disagree")
        midi.append(None if is_pause else note.get("midiKey"))
        surfaces.append(lyric.get("surface"))
    audio_path = candidate_path.removesuffix(".json") + ".wav"
    payload = read(audio_path, 64 * 1024**2, binary=True)
    if candidate.get("sampleRate") != 48000 or candidate.get("audioSha256") != files[audio_path]:
        raise ValueError("Candidate audio identity/clock differs from export receipt")
    provenance = dict(exportReceiptSha256=receipt_sha256, projectSha256=files["project.seam"],
                      candidateSha256=files[candidate_path], sourceSha256=files[audio_path],
                      recipeSha256=files[recipe_path], candidatePath=candidate_path)
    return candidate, payload, surfaces, midi, provenance


def prepare_bundle(*, export_root, receipt_sha256, candidate_path, extractor, output,
                   source_id, song_id, session_id, lineage_id, clone_captures=False,
                   verify_existing=False):
    output = Path(output)
    if type(verify_existing) is not bool:
        raise ValueError("Existing capture verification must be explicit")
    expected_files = {"source.wav", "pitch.json", "export.json", "conditioning.json",
                      "label-config.json", "target.json", "provenance.json", "inspection.json",
                      "mel.f32le", "targets.json", "preparation.json"}
    if verify_existing:
        if (output.is_symlink() or not output.is_dir()
                or {path.name for path in output.iterdir()} != expected_files
                or any(path.is_symlink() or not path.is_file() for path in output.iterdir())):
            raise ValueError("Existing capture must be complete with only regular expected files")
    elif output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError("Preparation output must be new with an existing parent")
    def write_bytes(name, data):
        path = output / name
        if verify_existing:
            with path.open("rb") as stream:
                if stream.read(len(data) + 1) != data:
                    raise ValueError(f"Existing capture differs from fresh derivation: {name}")
        else:
            with path.open("xb") as stream:
                stream.write(data)
    def publish(path, value):
        if verify_existing:
            write_bytes(path.name, encode_report(value))
        else:
            publish_new(path, value)
    candidate, payload, lyrics, midi, provenance = capture_inputs(export_root, receipt_sha256, candidate_path)
    # Copy only the selected, already verified WAV. The native process and all
    # downstream stages consume this stable local capture, never a changing source.
    if not verify_existing:
        output.mkdir(mode=0o700)
    if verify_existing:
        write_bytes("source.wav", payload)
    elif clone_captures:
        from .clone_capture import clone_verified_file
        clone_verified_file(_path(Path(export_root).resolve(strict=True),
                                 candidate_path.removesuffix(".json") + ".wav"),
                            output / "source.wav", provenance["sourceSha256"])
    else:
        with (output / "source.wav").open("xb") as stream:
            stream.write(payload)
    pitch = extract_pitch(Path(extractor), output / "source.wav")
    export = export_from_candidate(candidate=candidate, pitch_features=pitch, source_id=source_id,
        song_id=song_id, session_id=session_id, lineage_id=lineage_id,
        syllable_lyrics=lyrics, note_midi=midi, pcm_payload=payload)
    vocabulary = sorted({phone["symbol"] for phone in export["label"]["phonemes"]})
    conditioning = build_conditioning(export["label"], export["score"], vocabulary=vocabulary, minimum_confidence=0.)
    labels = label_config_from_exports(exports=[export], sample_rate=48000,
        relative_paths={source_id: "source.wav"}, vocabulary=vocabulary, minimum_confidence=0.)
    from .acoustics import wav_log_mel_targets
    target, matrix = wav_log_mel_targets(payload, expected_sha256=export["sourceSha256"], sample_rate=48000)
    if len(matrix) != len(conditioning["frames"]):
        raise ValueError("Acoustic and conditioning clocks differ")
    digests = {}
    for name, value in (("pitch.json", pitch), ("export.json", export), ("conditioning.json", conditioning),
                        ("label-config.json", labels), ("target.json", target), ("provenance.json", provenance)):
        publish(output / name, value)
        digests[name] = hashlib.sha256(encode_report(value)).hexdigest()
    inspection = inspect_label_config(output / "label-config.json", digests["label-config.json"], output)
    # Missing reviewRevision is expected for unapproved preparation. Other defects
    # are rejected by build_conditioning above; inspection is retained, not hidden.
    publish(output / "inspection.json", inspection)
    raw = matrix.astype("<f4", copy=False).tobytes(order="C")
    write_bytes("mel.f32le", raw)
    targets = dict(formatId="com.project-seam.training-target-inventory", schemaVersion=1,
        profileSha256=target["profileSha256"], targets=[dict(sourceId=source_id,
        record="target.json", recordSha256=digests["target.json"], binary="mel.f32le")])
    publish(output / "targets.json", targets)
    digests["targets.json"] = hashlib.sha256(encode_report(targets)).hexdigest()
    report = dict(formatId="com.project-seam.captured-teacher-preparation", schemaVersion=1,
        state="PREPARED_UNAPPROVED", **provenance, sourceId=source_id, sourceFrameCount=export["frameCount"],
        analysisFrameCount=len(matrix), phoneCount=len(export["label"]["phonemes"]),
        noteCount=len(export["score"]["notes"]), syllableCount=len(export["score"]["syllables"]),
        slurCount=sum(note["slur"] for note in export["score"]["notes"]),
        artifacts=digests, targetSha256=target["targetSha256"], labelOrigin=export["labelOrigin"],
        restPitchDiagnostics=rest_pitch_diagnostics(export["score"], pitch),
        noteClock="renderer-marker-ownership-not-piano-roll", independentSplitCreated=False,
        sourceRightsAdmitted=False, labelsAdmitted=False, trainingAdmitted=False, releaseEligible=False)
    # Complete receipt last. Failed attempts remain inspectable but are incomplete.
    publish(output / "preparation.json", report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("export_root", "extractor", "output"):
        parser.add_argument("--" + name.replace("_", "-"), type=Path, required=True)
    for name in ("receipt_sha256", "candidate_path", "source_id", "song_id", "session_id", "lineage_id"):
        parser.add_argument("--" + name.replace("_", "-"), required=True)
    args = parser.parse_args()
    try:
        result = prepare_bundle(**vars(args))
        print(json.dumps(result, ensure_ascii=False, allow_nan=False))
        return 0
    except (ValueError, OSError, KeyError, TypeError, ImportError) as error:
        print(f"Captured teacher preparation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
