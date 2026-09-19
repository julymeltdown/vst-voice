"""Bind a prepared corpus and its authored reviews into a dataset configuration.

Dataset assembly reads one configuration naming the permission capture, the label
configuration and the reviews that authorize them. Preparing a corpus produces the
first two; authoring reviews produces the rest. Nothing joined them, so the step
between a prepared corpus and an admitted dataset was manual.

This writes that configuration and nothing else. It copies each reviewed artifact
into the corpus directory under a flat name because dataset references are flat
ASCII filenames beside the source root, records the digest of the bytes actually
present, and refuses any artifact that is absent. It admits nothing: assembly and
review verification happen when the training path revalidates these references.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

from .__main__ import encode_report, load_config, publish_new

REFERENCES = ("permissionConfig", "labelConfig", "rightsReview", "rightsPolicy", "labelReview", "labelPolicy")


def _copy_in(corpus, path, name):
    """Place one reviewed artifact in the corpus root and return its digest."""
    source = Path(path)
    if source.is_symlink() or not source.is_file():
        raise ValueError(f"Reviewed artifact {name} must be a regular file")
    payload = source.read_bytes()
    if not 1 <= len(payload) <= 8 * 1024 * 1024:
        raise ValueError(f"Reviewed artifact {name} is empty or exceeds the capture budget")
    target = corpus / name
    if target.exists():
        # An existing copy is accepted only if it is byte-identical, so a re-run
        # cannot silently retarget a dataset at different review bytes.
        if target.read_bytes() != payload:
            raise ValueError(f"Existing {name} differs from the reviewed artifact")
    else:
        with target.open("xb") as stream:
            stream.write(payload)
            stream.flush()
    return hashlib.sha256(target.read_bytes()).hexdigest()


def assemble_corpus_dataset(*, corpus, receipt, rights_review, rights_policy, label_review,
                            label_policy, output, seed=None, held_out_songs=None):
    corpus = Path(corpus)
    if not corpus.is_dir() or corpus.is_symlink():
        raise ValueError("Corpus must be an existing real directory")
    captured = load_config(corpus / "corpus.json", receipt)
    if (not isinstance(captured, dict)
            or captured.get("formatId") != "com.project-seam.captured-teacher-corpus"
            or captured.get("state") != "PREPARED_UNAPPROVED"):
        raise ValueError("Dataset assembly requires a prepared captured-teacher corpus")
    if captured.get("sourceRightsAdmitted") is not False or captured.get("trainingAdmitted") is not False:
        raise ValueError("A prepared corpus must not already claim admission")
    # Re-verify the two configuration files against the digests the corpus receipt
    # recorded, so assembly cannot be pointed at a different permission or label set.
    permissions = load_config(corpus / "permissions.json", captured["permissionsSha256"])
    labels = load_config(corpus / "labels.json", captured["labelsSha256"])
    for value, name in ((permissions, "permission"), (labels, "label")):
        if not isinstance(value, dict):
            raise ValueError(f"The captured {name} configuration must be an object")
    output = Path(output)
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError("Dataset configuration output must be new with an existing parent")
    references = {
        "permissionConfig": ("permissions.json", captured["permissionsSha256"]),
        "labelConfig": ("labels.json", captured["labelsSha256"]),
        "rightsReview": ("rightsReview.json", None),
        "rightsPolicy": ("rightsPolicy.json", None),
        "labelReview": ("labelReview.json", None),
        "labelPolicy": ("labelPolicy.json", None),
    }
    supplied = {"rightsReview": rights_review, "rightsPolicy": rights_policy,
                "labelReview": label_review, "labelPolicy": label_policy}
    for key, path in supplied.items():
        name = references[key][0]
        references[key] = (name, _copy_in(corpus, path, name))
    # The review and its policy must agree on the configuration being authorized,
    # or the dataset would name a review that cannot verify its own subject.
    for kind, review_key in (("rights", "rightsReview"), ("label", "labelReview")):
        review = load_config(corpus / references[review_key][0], references[review_key][1])
        subject = captured["permissionsSha256"] if kind == "rights" else captured["labelsSha256"]
        if review.get("configurationSha256") != subject:
            raise ValueError(f"The {kind} review does not authorize this corpus configuration")
    if seed is None:
        seed = captured["seed"]
    if (not isinstance(seed, str) or not 1 <= len(seed.encode()) <= 256
            or any(ord(c) < 32 or ord(c) == 127 for c in seed)):
        raise ValueError("Dataset seed must be bounded printable text")
    if held_out_songs is None:
        held_out_songs = list(captured["heldOutSongIds"])
    declared_songs = {song["sourceId"] for song in captured["songs"]}
    if (not isinstance(held_out_songs, list) or not held_out_songs
            or len(set(held_out_songs)) != len(held_out_songs)
            or not set(held_out_songs) <= declared_songs):
        raise ValueError("Held-out songs must be a non-empty subset of the prepared corpus")
    configuration = dict(formatId="com.project-seam.training-dataset-config", schemaVersion=1,
                         seed=seed, heldOutSongs=sorted(held_out_songs),
                         **{key: dict(path=name, sha256=digest) for key, (name, digest) in references.items()})
    publish_new(output, configuration)
    return dict(configuration=configuration,
                configurationSha256=hashlib.sha256(encode_report(configuration)).hexdigest(),
                heldOutSongs=sorted(held_out_songs),
                sourceRightsAdmitted=False, labelsAdmitted=False, trainingAdmitted=False,
                releaseEligible=False)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--receipt", required=True, help="Captured corpus.json SHA-256")
    for name in ("rights-review", "rights-policy", "label-review", "label-policy"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seed")
    parser.add_argument("--held-out-song", action="append", dest="held_out_songs")
    args = parser.parse_args(argv)
    try:
        result = assemble_corpus_dataset(corpus=args.corpus, receipt=args.receipt,
            rights_review=args.rights_review, rights_policy=args.rights_policy,
            label_review=args.label_review, label_policy=args.label_policy, output=args.output,
            seed=args.seed, held_out_songs=args.held_out_songs)
        print(json.dumps({key: result[key] for key in (
            "configurationSha256", "heldOutSongs", "trainingAdmitted", "releaseEligible")},
            ensure_ascii=False))
        return 0
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(f"Dataset configuration assembly failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
