"""Bounded conditioning batches for a freshly revalidated dataset snapshot.

This is feature I/O, not training authorization. The caller must refresh source
and review admission before a training run; a saved snapshot is not authority.
"""
import hashlib
import json
import os
from pathlib import Path
import stat

from .conditioning import build_conditioning
from .split import split_sources


def _encoded(value):
    return (json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":")) + "\n").encode()


def iter_supervised_batches(snapshot: dict, directory: Path, targets: dict, *,
                            expected_profile_sha256: str, partition: str, batch_frames: int = 256,
                            context_frames: int = 0):
    """Join captured target records/files with conditioning; no rights admission.

    targets maps each source ID to (captured metadata, binary Path). The caller
    owns metadata provenance and independently chooses the training profile hash.
    Only one source's target matrix is retained; emitted arrays are owned copies.
    """
    import numpy as np
    if (not isinstance(expected_profile_sha256, str) or len(expected_profile_sha256) != 64
            or any(c not in "0123456789abcdef" for c in expected_profile_sha256)):
        raise ValueError("Training requires an explicit captured acoustic profile hash")
    sources = {row["sourceId"]: row for row in snapshot["sources"]}
    if not isinstance(targets, dict) or set(targets) != set(sources):
        raise ValueError("Target inventory must cover exactly the dataset source IDs")
    active_id, matrix, record = None, None, None
    for batch in iter_conditioning_batches(snapshot, directory, partition=partition, batch_frames=batch_frames,
                                           context_frames=context_frames):
        identity = batch["sourceId"]
        if active_id != identity:
            record, path = targets[identity]
            source = sources[identity]
            profile = record["profile"]
            digest = hashlib.sha256(json.dumps(profile, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
            frames, bins = record["analysisFrameCount"], profile["bins"]
            if (record.get("formatId") != "com.project-seam.training-acoustic-target"
                    or type(record.get("schemaVersion")) is not int or record["schemaVersion"] != 1
                    or digest != expected_profile_sha256 or record["profileSha256"] != digest
                    or profile.get("profileId") != "seam-full-hop-slaney-v1"
                    or profile.get("layout") != "TF" or profile.get("dtype") != "float32-le"
                    or type(frames) is not int or not 1 <= frames <= 65536
                    or type(bins) is not int or not 1 <= bins <= 512 or frames * bins > 8388608
                    or any(record[key] != source[key] for key in ("sourceSha256", "audioSha256"))
                    or record["sourceFrameCount"] != source["frameCount"]
                    or profile["sampleRate"] != source["sampleRate"] or profile["hopSize"] != batch["hopSize"]
                    or frames != (source["frameCount"] + batch["hopSize"] - 1) // batch["hopSize"]
                    or record["targetBytes"] != frames * bins * 4):
                raise ValueError("Acoustic target differs from dataset identity, profile or clock")
            path = Path(path)
            if path.is_symlink():
                raise ValueError("Acoustic target cannot be a symlink")
            flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
            with os.fdopen(os.open(path, flags), "rb") as stream:
                info = os.fstat(stream.fileno())
                if not stat.S_ISREG(info.st_mode) or info.st_size != record["targetBytes"]:
                    raise ValueError("Acoustic target file shape differs")
                payload = stream.read(record["targetBytes"] + 1)
            if len(payload) != record["targetBytes"] or hashlib.sha256(payload).hexdigest() != record["targetSha256"]:
                raise ValueError("Acoustic target bytes differ")
            matrix = np.frombuffer(payload, dtype="<f4").reshape(frames, bins)
            if not np.isfinite(matrix).all():
                raise ValueError("Acoustic targets must be finite")
            active_id = identity
        start, count = batch["frameOffset"], len(batch["columns"]["sourceFrame"])
        yield dict(**batch, datasetSha256=snapshot["datasetSha256"], targetSha256=record["targetSha256"],
                   profileSha256=expected_profile_sha256, melTargets=matrix[start:start + count].copy())


def iter_conditioning_batches(snapshot: dict, directory: Path, *, partition: str,
                              batch_frames: int = 256, context_frames: int = 0):
    """Yield unpadded, source-local column batches, never crossing partitions.

    Recompute conditioning from captured labels and compare exact shard bytes
    before yielding any rows from that phrase. Later shard errors can occur after
    earlier batches: a training transaction must not publish a checkpoint on error.
    """
    if partition not in ("train", "validation", "test"):
        raise ValueError("Select an explicit dataset partition")
    if type(batch_frames) is not int or not 1 <= batch_frames <= 4096:
        raise ValueError("Batch size must be 1..4096 frames")
    if type(context_frames) is not int or context_frames < 0 or batch_frames + 2 * context_frames > 4096:
        raise ValueError("Core plus two context halos must fit 4096 frames")
    if (snapshot.get("formatId") != "com.project-seam.training-dataset-snapshot"
            or type(snapshot.get("schemaVersion")) is not int or snapshot["schemaVersion"] != 3):
        raise ValueError("Batch reader requires a schema-3 sharded snapshot")
    bindings = snapshot["bindings"]
    if hashlib.sha256(_encoded(bindings)).hexdigest() != snapshot["datasetSha256"]:
        raise ValueError("Dataset binding digest differs")
    refs = snapshot["conditioning"]
    if hashlib.sha256(_encoded(refs)).hexdigest() != bindings["conditioningSha256"]:
        raise ValueError("Conditioning reference digest differs")
    split = bindings["split"]
    rows = [{key: source[key] for key in ("sourceId", "songId", "sessionId", "lineageId", "audioSha256")}
            for source in snapshot["sources"]]
    if split_sources(rows, seed=split["seed"], held_out_songs=split["heldOutSongIds"]) != split:
        raise ValueError("Dataset partition geometry differs")
    labels = {entry["label"]["sourceId"]: entry for entry in snapshot["labels"]}
    ids = [ref["sourceId"] for ref in refs]
    if (len(labels) != len(snapshot["labels"]) or ids != sorted(labels)
            or set(ids) != {row["sourceId"] for row in rows}):
        raise ValueError("Dataset labels and shard identities differ")
    selected = {source for group in split["groups"] if group["partition"] == partition for source in group["sourceIds"]}
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError("Feature directory must be a real directory")
    for index, ref in enumerate(refs):
        if ref["sourceId"] not in selected:
            continue
        name = f"phrase-{index:06d}.json"
        if ref["path"] != name:
            raise ValueError("Unexpected feature shard path")
        entry = labels[ref["sourceId"]]
        control = entry.get("conditioning")
        features = build_conditioning(entry["label"], entry["score"], vocabulary=snapshot["vocabulary"],
                                      minimum_confidence=0,
                                      breathiness=None if control is None else control["breathiness"])
        payload = _encoded(features)
        if (ref["sizeBytes"] != len(payload) or ref["frameCount"] != len(features["frames"])
                or ref["sha256"] != hashlib.sha256(payload).hexdigest()):
            raise ValueError("Shard reference differs from captured label features")
        path = directory / name
        if path.is_symlink():
            raise ValueError("Feature shard cannot be a symlink")
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
        with os.fdopen(os.open(path, flags), "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size != len(payload):
                raise ValueError("Feature shard size/type differs")
            if stream.read(len(payload) + 1) != payload:
                raise ValueError("Feature shard bytes differ")
        frames = features["frames"]
        token_ids = {symbol: index + 1 for index, symbol in enumerate(features["vocabulary"])}
        # Keep the complete original sequence: frame sampling can miss a short
        # phone, and adjacent identical symbols still represent distinct phones.
        tokens = [token_ids[phone["symbol"]] for phone in entry["label"]["phonemes"]]
        for offset in range(0, len(frames), batch_frames):
            core_end = min(len(frames), offset + batch_frames)
            begin, end = max(0, offset - context_frames), min(len(frames), core_end + context_frames)
            chunk = frames[begin:end]
            yield dict(sourceId=ref["sourceId"], partition=partition, frameOffset=begin,
                       coreFrameOffset=offset, coreFrameCount=core_end - offset,
                       phraseAnalysisFrames=len(frames),
                       tokens=list(tokens), mel2ph=[row["phoneIndex"] + 1 for row in chunk],
                       lossMask=[offset <= index < core_end for index in range(begin, end)],
                       hopSize=features["hopSize"], language=features["language"],
                       columns={key: [row[key] for row in chunk] for key in chunk[0]})
