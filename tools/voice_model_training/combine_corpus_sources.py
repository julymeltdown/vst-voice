"""Combine captured corpus declarations without copying audio or granting approval.

Every previously non-training song is conservatively forced into the new test set.
This protects prior evaluation material but changes validation/test membership;
the output is a new experiment, not continuation of any old benchmark or model.
"""
import argparse
import hashlib
import json
from pathlib import Path

from .__main__ import encode_report, load_config, publish_new
from .prepare_corpus import load_corpus_config


def combine(inputs, *, seed, output):
    if not 2 <= len(inputs) <= 8:
        raise ValueError("Combine requires two to eight captured corpora")
    output = Path(output)
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError("Output must be new with an existing parent")
    if not isinstance(seed, str) or not 1 <= len(seed.encode()) <= 256 or any(ord(c) < 32 or ord(c) == 127 for c in seed):
        raise ValueError("Seed must be bounded printable text")
    songs, protected, seen = [], set(), set()
    common = None
    for configuration, digest, receipt, receipt_digest in inputs:
        config = load_corpus_config(Path(configuration), digest)
        prepared = load_config(Path(receipt), receipt_digest)
        if (not isinstance(prepared, dict) or prepared.get("formatId") != "com.project-seam.captured-teacher-corpus"
                or prepared.get("state") != "PREPARED_UNAPPROVED"
                or prepared.get("configurationSha256") != digest):
            raise ValueError("Preparation receipt does not bind the supplied source declaration")
        settings = (config["extractor"], sorted(config["trainingScopes"]))
        if common is not None and settings != common:
            raise ValueError("Extractors and declared scopes must agree; no implicit scope expansion")
        common = settings
        declared = {song["sourceId"]: song for song in config["songs"]}
        captured = prepared.get("songs", [])
        if (not isinstance(captured, list) or any(not isinstance(song, dict) for song in captured)
                or len(captured) != len(declared) or {song["sourceId"] for song in captured} != set(declared)):
            raise ValueError("Preparation receipt must cover every declared source exactly once")
        for song in captured:
            if song.get("partition") not in ("train", "validation", "test"):
                raise ValueError("Preparation has an unknown partition")
            if song["partition"] != "train":
                protected.add(declared[song["sourceId"]]["songId"])
        protected.update(config["heldOutSongIds"])
        for song in config["songs"]:
            # Namespace collisions in any identity can otherwise change connected
            # components and conceal a mistaken re-use of a corpus index range.
            identities = {(field, song[field]) for field in ("sourceId", "songId", "sessionId", "lineageId")}
            if seen & identities:
                raise ValueError("Corpus identity collision; never rename captured identities automatically")
            seen.update(identities)
            songs.append(song)
    if len(songs) > 10000 or not protected < {song["songId"] for song in songs}:
        raise ValueError("Combined corpus needs bounded sources and at least one training-eligible song")
    result = dict(formatId="com.project-seam.captured-teacher-corpus-config", schemaVersion=1,
                  seed=seed, extractor=common[0], trainingScopes=common[1], songs=songs,
                  heldOutSongIds=sorted(protected))
    if len(encode_report(result)) > 8 * 1024 * 1024:
        raise ValueError("Combined configuration exceeds the capture byte budget")
    publish_new(output, result)
    return dict(songs=len(songs), protectedNonTrainingSongs=len(protected),
                sourcesSha256=hashlib.sha256(output.read_bytes()).hexdigest(),
                preparationRequired=True, freshReviewsRequired=True, trainingAdmitted=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", action="append", nargs=4, required=True,
                        metavar=("CONFIG", "SHA256", "PREPARATION", "SHA256"))
    parser.add_argument("--seed", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(combine(args.input, seed=args.seed, output=args.output)))
        return 0
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.exit(2, str(error)[:512] + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
