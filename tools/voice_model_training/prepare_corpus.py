"""Prepare several receipt-bound songs as one corpus with an explicit held-out set.

One song cannot supply a held-out partition: every phrase of it shares the speaker,
the session and the renderer revision, so a split inside it measures memorization.
This step therefore takes several independently authored songs and refuses a corpus
whose held-out songs share audio, song, session or lineage identity with training.

It prepares material and previews the split. It does not sign a rights or label
review, admit training or qualify a singer; those authorities stay external and are
still required before any training run may use these bytes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

from .__main__ import encode_report, load_config, publish_new
from .generated_teacher import label_config_from_exports
from .permissions import TRAINING_PERMISSIONS
from .prepare_captured_teacher import prepare_bundle
from .split import split_sources


ENTRY_FIELDS = {"exportRoot", "receiptSha256", "candidatePath", "sourceId", "songId", "sessionId", "lineageId"}


def load_corpus_config(path, expected_hash):
    """Read the corpus declaration and bind it to caller-supplied bytes."""
    value = load_config(Path(path), expected_hash)
    fields = {"formatId", "schemaVersion", "seed", "extractor", "songs", "heldOutSongIds", "trainingScopes"}
    if (not isinstance(value, dict) or set(value) != fields
            or value["formatId"] != "com.project-seam.captured-teacher-corpus-config"
            or type(value["schemaVersion"]) is not int or value["schemaVersion"] != 1):
        raise ValueError("Unsupported captured-teacher corpus configuration")
    if (not isinstance(value["seed"], str) or not 1 <= len(value["seed"].encode()) <= 256
            or any(ord(c) < 32 or ord(c) == 127 for c in value["seed"])):
        raise ValueError("Corpus seed must be bounded printable text")
    songs = value["songs"]
    if not isinstance(songs, list) or not 2 <= len(songs) <= 64:
        # A corpus of one cannot hold anything out; refuse rather than pretend.
        raise ValueError("A corpus requires 2..64 songs")
    for entry in songs:
        if not isinstance(entry, dict) or set(entry) != ENTRY_FIELDS:
            raise ValueError("Corpus entries must declare exactly the export and identity fields")
        for key in ("sourceId", "songId", "sessionId", "lineageId"):
            text = entry[key]
            if (not isinstance(text, str) or not 1 <= len(text.encode()) <= 256
                    or any(ord(c) < 32 or ord(c) == 127 for c in text)):
                raise ValueError("Corpus identity fields must be bounded printable text")
        if (not isinstance(entry["receiptSha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", entry["receiptSha256"])
                or not isinstance(entry["candidatePath"], str)
                or not re.fullmatch(r"candidates/[0-9a-f]{16}-[0-9a-f]{16}\.json", entry["candidatePath"])
                or not isinstance(entry["exportRoot"], str) or not entry["exportRoot"]):
            raise ValueError("Corpus entries require a canonical capture path and receipt digest")
    held = value["heldOutSongIds"]
    if (not isinstance(held, list) or not held or len(set(held)) != len(held)
            or any(not isinstance(song, str) for song in held)):
        raise ValueError("Corpus requires an explicit non-empty held-out song list")
    if not set(held) < {entry["songId"] for entry in songs}:
        # A held-out set equal to every song leaves nothing to train on.
        raise ValueError("Held-out songs must be a strict subset of the corpus")
    # The scope declaration is the operator's claim about their own material. It is
    # carried, not inferred and not defaulted, because asserting a redistribution or
    # commercial scope on someone's behalf is a legal statement this tool cannot make.
    scopes = value["trainingScopes"]
    # An empty declaration asserts that nothing may be done with the material, so a
    # corpus built from it could never be trained on. Refuse instead of publishing
    # an artifact that only looks prepared.
    if (not isinstance(scopes, list) or not 1 <= len(scopes) or len(set(scopes)) != len(scopes)
            or any(scope not in TRAINING_PERMISSIONS for scope in scopes)):
        raise ValueError("Corpus training scopes must be a unique subset of the known scopes")
    return value


def prepare_corpus(*, config, config_sha256, output):
    """Prepare every song independently, then publish the corpus binding last."""
    output = Path(output)
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError("Corpus output must be new with an existing parent")
    value = load_corpus_config(config, config_sha256)
    # Identity collisions are cheapest to refuse before any capture or extraction.
    if len({entry["sourceId"] for entry in value["songs"]}) != len(value["songs"]):
        raise ValueError("Corpus source identifiers must be unique")
    if len({entry["songId"] for entry in value["songs"]}) != len(value["songs"]):
        raise ValueError("Each prepared song must declare a distinct song identity")
    output.mkdir(mode=0o700)
    songs, digests = [], {}
    for index, entry in enumerate(value["songs"]):
        directory = output / f"song-{index:03d}"
        report = prepare_bundle(export_root=Path(entry["exportRoot"]),
            receipt_sha256=entry["receiptSha256"], candidate_path=entry["candidatePath"],
            extractor=Path(value["extractor"]), output=directory, source_id=entry["sourceId"],
            song_id=entry["songId"], session_id=entry["sessionId"], lineage_id=entry["lineageId"])
        report["directory"] = directory.name
        report["preparationSha256"] = hashlib.sha256(encode_report(report)).hexdigest()
        songs.append(report)
    # The split preview is computed from already-verified identities. It proves the
    # held-out assignment is leak-free; it is not a training admission.
    declared = {entry["sourceId"]: entry for entry in value["songs"]}
    rows = [dict(sourceId=song["sourceId"], songId=declared[song["sourceId"]]["songId"],
                 sessionId=declared[song["sourceId"]]["sessionId"],
                 lineageId=declared[song["sourceId"]]["lineageId"],
                 audioSha256=song["sourceSha256"]) for song in songs]
    split = split_sources(rows, seed=value["seed"], held_out_songs=value["heldOutSongIds"])
    # A corpus is trainable only if it can both train and hold something out. An
    # empty validation partition is normal for a small corpus and is recorded
    # rather than refused; an empty train or test partition makes the run
    # meaningless, so it is refused here.
    for partition in ("train", "test"):
        if not split["counts"][partition]:
            raise ValueError(f"Corpus split has no {partition} source")
    partitioned = {identifier: group["partition"] for group in split["groups"] for identifier in group["sourceIds"]}
    for song in songs:
        songs_entry = declared[song["sourceId"]]
        forced = songs_entry["songId"] in set(value["heldOutSongIds"])
        song["partition"] = partitioned[song["sourceId"]]
        song["heldOutRequested"] = forced
        # Every declared held-out song must land in the test partition. The
        # converse is not required: the seed may also assign an undeclared song
        # to test, which removes it from training rather than leaking it.
        if forced and song["partition"] != "test":
            raise ValueError("Declared held-out songs must partition into the test set")
    # Dataset assembly consumes one label configuration covering every source, so the
    # per-song captures are merged here rather than left for the caller to hand-edit.
    # Each export is re-read and digest-checked against the preparation receipt, so a
    # changed file cannot enter the corpus configuration silently.
    exports, relative_paths = [], {}
    for index, song in enumerate(songs):
        directory = output / song["directory"]
        payload = (directory / "export.json").read_bytes()
        if hashlib.sha256(payload).hexdigest() != song["artifacts"]["export.json"]:
            raise ValueError("A prepared export differs from its recorded digest")
        export = json.loads(payload)
        exports.append(export)
        relative_paths[export["sourceId"]] = f"{song['directory']}/source.wav"
    vocabulary = sorted({phone["symbol"] for export in exports for phone in export["label"]["phonemes"]})
    labels = label_config_from_exports(exports=exports, sample_rate=48000,
        relative_paths=relative_paths, vocabulary=vocabulary, minimum_confidence=0.0)
    publish_new(output / "labels.json", labels)
    labels_sha256 = hashlib.sha256(encode_report(labels)).hexdigest()
    # A permission capture in the shape admission consumes. It asserts the operator's
    # declared scopes against the actual bytes; it is not a signature, it carries no
    # legal interpretation, and admission still requires an independently trusted
    # reviewer to sign this exact configuration hash.
    scopes = {scope: True for scope in TRAINING_PERMISSIONS}
    declared_scopes = set(value["trainingScopes"])
    for scope in TRAINING_PERMISSIONS:
        scopes[scope] = scope in declared_scopes
    evidence_path = output / "corpus-evidence.txt"
    evidence = ("SEAM first-party procedural corpus. Every WAV was rendered by this "
                "repository's own procedural singer through the production export path; "
                "no third-party recording is present. Declared scopes: "
                + ", ".join(sorted(declared_scopes)) + "\n").encode()
    with evidence_path.open("xb") as stream:
        stream.write(evidence)
    evidence_sha256 = hashlib.sha256(evidence).hexdigest()
    permission_rows = [dict(sourceId=song["sourceId"], sourceSha256=song["sourceSha256"],
        identityId="seam-procedural-" + song["sourceId"], kind="PROCEDURAL_SYNTHESIS",
        evidenceId="corpus-evidence", evidenceSha256=evidence_sha256, permissions=scopes,
        reviewRevision="unreviewed-corpus-preparation") for song in songs]
    permission_config = dict(formatId="com.project-seam.training-permission-config", schemaVersion=1,
        sampleRate=48000,
        sources=[dict(sourceId=song["sourceId"], songId=declared[song["sourceId"]]["songId"],
                      sessionId=declared[song["sourceId"]]["sessionId"],
                      lineageId=declared[song["sourceId"]]["lineageId"],
                      path=song["directory"] + "/source.wav", sourceSha256=song["sourceSha256"])
                 for song in songs],
        evidence={"corpus-evidence": evidence_path.name},
        manifest=dict(formatId="com.project-seam.training-permission-manifest", schemaVersion=1,
                      sources=permission_rows))
    publish_new(output / "permissions.json", permission_config)
    permissions_sha256 = hashlib.sha256(encode_report(permission_config)).hexdigest()
    digests["songs"] = hashlib.sha256(encode_report([
        {key: song[key] for key in ("sourceId", "directory", "partition", "heldOutRequested",
                                    "sourceSha256", "preparationSha256")} for song in songs])).hexdigest()
    # The split separates songs by song, session and lineage, not by which voice
    # recipe rendered them. Two partitions rendered from one recipe therefore still
    # share a voice design; report that instead of letting it pass silently.
    recipes_by_partition = {}
    for song in songs:
        recipes_by_partition.setdefault(song["recipeSha256"], set()).add(song["partition"])
    shared_recipe = sorted(digest for digest, partitions in recipes_by_partition.items()
                           if len(partitions) > 1)
    corpus = dict(formatId="com.project-seam.captured-teacher-corpus", schemaVersion=1,
        state="PREPARED_UNAPPROVED", configurationSha256=config_sha256, seed=value["seed"],
        extractor=value["extractor"], songs=songs, split=split,
        heldOutSongIds=sorted(value["heldOutSongIds"]), songsSha256=digests["songs"],
        labelsSha256=labels_sha256, vocabulary=vocabulary,
        permissionsSha256=permissions_sha256, trainingScopes=sorted(declared_scopes),
        assertionsComplete=len(declared_scopes) == len(TRAINING_PERMISSIONS),
        totalSourceFrames=sum(song["sourceFrameCount"] for song in songs),
        totalAnalysisFrames=sum(song["analysisFrameCount"] for song in songs),
        distinctAudioCount=len({song["sourceSha256"] for song in songs}),
        distinctRecipeCount=len(recipes_by_partition),
        recipeAcrossPartitions=shared_recipe,
        heldOutPartitionedAsRequested=all(song["heldOutRequested"] == (song["partition"] == "test")
                                          for song in songs),
        seedAssignedTestSongs=sorted(song["sourceId"] for song in songs
                                     if song["partition"] == "test" and not song["heldOutRequested"]),
        # These remain false: this step prepares bytes and previews a partition.
        # A signed rights/label review, an admitted dataset and a qualified singer
        # are still absent, and reconstruction quality is still unmeasured.
        sourceRightsAdmitted=False, labelsAdmitted=False, trainingAdmitted=False,
        singerQualified=False, releaseEligible=False)
    publish_new(output / "corpus.json", corpus)
    return corpus


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--config-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = prepare_corpus(config=args.config, config_sha256=args.config_sha256, output=args.output)
        print(json.dumps(dict(state=result["state"], songs=len(result["songs"]),
            counts=result["split"]["counts"], distinctAudioCount=result["distinctAudioCount"],
            totalAnalysisFrames=result["totalAnalysisFrames"], trainingAdmitted=False),
            ensure_ascii=False))
        return 0
    except (ValueError, OSError, KeyError, TypeError, ImportError) as error:
        print(f"Corpus preparation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
