"""Original-model dataset tooling. No command grants source or release approval."""
import argparse
import hashlib
import json
import os
import stat
from pathlib import Path
import sys
import tempfile
import time

from .split import split_sources
from .prepare import prepare_sources
from .labels import label_report, score_report
from .segment import segment_source, crop_labels, crop_score
from .audio_source import inspect_pcm_source
from .permissions import inspect_permission_sources
from .review import verify_training_review


def inspect_permission_config(config: Path, expected_hash: str, root: Path) -> dict:
    value = load_config(config, expected_hash)
    fields = {"formatId", "schemaVersion", "manifest", "sources", "sampleRate", "evidence"}
    if (not isinstance(value, dict) or set(value) != fields
            or value["formatId"] != "com.project-seam.training-permission-config"
            or type(value["schemaVersion"]) is not int or value["schemaVersion"] != 1
            or not isinstance(value["evidence"], dict) or not 1 <= len(value["evidence"]) <= 10000):
        raise ValueError("Invalid permission capture configuration")
    root = root.resolve(strict=True)
    captured, remaining = {}, 64 * 1024 * 1024
    for identity, name in value["evidence"].items():
        if (not isinstance(identity, str) or not 1 <= len(identity.encode()) <= 256
                or not isinstance(name, str) or not 1 <= len(name) <= 128 or name in (".", "..")
                or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_." for c in name)):
            raise ValueError("Evidence requires bounded IDs and flat ASCII filenames")
        path = root / name
        if path.is_symlink():
            raise ValueError("Evidence symlinks are unsupported")
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
        with os.fdopen(os.open(path, flags), "rb") as stream:
            before = os.fstat(stream.fileno())
            if not stat.S_ISREG(before.st_mode) or not 0 < before.st_size <= min(remaining, 4 * 1024 * 1024):
                raise ValueError("Evidence exceeds remaining capture budget")
            blob = stream.read(min(remaining, 4 * 1024 * 1024) + 1)
            remaining -= len(blob)
            after = os.fstat(stream.fileno())
            if (len(blob) != before.st_size or (before.st_size, before.st_mtime_ns, before.st_ctime_ns)
                    != (after.st_size, after.st_mtime_ns, after.st_ctime_ns)):
                raise ValueError("Evidence changed during capture")
        captured[identity] = blob
    report = inspect_permission_sources(value["manifest"], captured, root=root,
                                        sources=value["sources"], sample_rate=value["sampleRate"])
    report["configurationSha256"] = expected_hash
    return report


def permission_command(config: Path, expected_hash: str, root: Path, output: Path) -> int:
    report = inspect_permission_config(config, expected_hash, root)
    publish_new(output, report)
    return 0 if report["assertionsComplete"] else 3


def admit_sources(config: Path, expected_hash: str, root: Path, *, review: dict, policy: dict,
                  trusted_policy_sha256: str, now: int) -> dict:
    verified = verify_training_review(review, policy=policy, trusted_policy_sha256=trusted_policy_sha256,
                                      configuration_sha256=expected_hash, now=now)
    inspected = inspect_permission_config(config, expected_hash, root)
    if not inspected["assertionsComplete"]:
        raise ValueError("Signed configuration lacks required training scope assertions")
    return dict(formatId="com.project-seam.training-source-admission", schemaVersion=1,
                configurationSha256=expected_hash, policySha256=trusted_policy_sha256,
                reviewSha256=verified["reviewSha256"], signerId=verified["signerId"],
                verifiedAt=now, expiresAt=review["expiresAt"], sources=inspected["sources"],
                sourceBytesVerified=True, reviewAuthenticated=True, sourcePermissionsAdmitted=True,
                trainingAdmitted=False, releaseEligible=False)


def load_config(config: Path, expected_hash: str) -> dict:
    if config.is_symlink() or not config.is_file():
        raise ValueError("Configuration must be a regular non-symlink file")
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    with os.fdopen(os.open(config, flags), "rb") as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise ValueError("Opened configuration is not a regular file")
        payload = stream.read(8 * 1024 * 1024 + 1)
    if len(payload) > 8 * 1024 * 1024:
        raise ValueError("Configuration exceeds 8 MiB")
    if hashlib.sha256(payload).hexdigest() != expected_hash:
        raise ValueError("Configuration differs from captured SHA-256")
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate configuration key")
            result[key] = value
        return result
    def invalid_constant(value):
        raise ValueError("Nonfinite JSON constant")
    value = json.loads(payload, object_pairs_hook=unique, parse_constant=invalid_constant)
    return value


def split_command(config: Path, expected_hash: str, output: Path) -> None:
    value = load_config(config, expected_hash)
    if (not isinstance(value, dict) or set(value) != {"formatId", "schemaVersion", "seed", "heldOutSongIds", "sources"}
            or value["formatId"] != "com.project-seam.voice-training-split-config"
            or type(value["schemaVersion"]) is not int or value["schemaVersion"] != 1):
        raise ValueError("Unsupported split configuration")
    result = split_sources(value["sources"], seed=value["seed"], held_out_songs=value["heldOutSongIds"])
    result["configurationSha256"] = expected_hash
    publish_new(output, result)


def prepare_command(config: Path, expected_hash: str, root: Path, output: Path) -> int:
    value = load_config(config, expected_hash)
    if (not isinstance(value, dict) or set(value) != {"formatId", "schemaVersion", "sampleRate", "sources"}
            or value["formatId"] != "com.project-seam.voice-training-prepare-config"
            or type(value["schemaVersion"]) is not int or value["schemaVersion"] != 1
            or type(value["sampleRate"]) is not int or not 8000 <= value["sampleRate"] <= 192000):
        raise ValueError("Unsupported preparation configuration")
    result = prepare_sources(root, value["sources"], sample_rate=value["sampleRate"])
    result["configurationSha256"] = expected_hash
    publish_new(output, result)
    return 3 if result["rejectedCount"] else 0


def labels_command(config: Path, expected_hash: str, root: Path, output: Path) -> int:
    value = load_config(config, expected_hash)
    fields = {"formatId", "schemaVersion", "sampleRate", "sources", "labels", "vocabulary", "minimumConfidence"}
    if (not isinstance(value, dict) or set(value) != fields
            or value["formatId"] != "com.project-seam.voice-training-label-config"
            or type(value["schemaVersion"]) is not int or value["schemaVersion"] not in (1, 2, 3)):
        raise ValueError("Unsupported label configuration")
    vocabulary = value["vocabulary"]
    if (not isinstance(vocabulary, list) or not 1 <= len(vocabulary) <= 4096
            or any(not isinstance(s, str) or not 1 <= len(s.encode()) <= 256 for s in vocabulary)
            or len(set(vocabulary)) != len(vocabulary)):
        raise ValueError("Invalid label vocabulary")
    prepared = prepare_sources(root, value["sources"], sample_rate=value["sampleRate"])
    if prepared["rejectedCount"]:
        raise ValueError("Label source inspection failed; use prepare for source diagnostics")
    labels = value["labels"]
    if not isinstance(labels, list) or len(labels) != len(prepared["sources"]):
        raise ValueError("Exactly one label is required per source")
    sources = {item["sourceId"]: item["inspection"] for item in prepared["sources"]}
    reports = []
    seen = set()
    for item in labels:
        item_fields = {"sourceSha256", "audioSha256", "label"}
        if value["schemaVersion"] >= 2:
            item_fields.add("score")
        if not isinstance(item, dict) or set(item) != item_fields:
            raise ValueError("Invalid source-bound label fields")
        report = label_report(item["label"], vocabulary=set(vocabulary), minimum_confidence=value["minimumConfidence"])
        identity = report["sourceId"]
        if identity in seen or identity not in sources:
            raise ValueError("Duplicate or unknown label source")
        seen.add(identity)
        source = sources[identity]
        if (item["sourceSha256"] != source["sourceSha256"] or item["audioSha256"] != source["audioSha256"]
                or item["label"]["frameCount"] != source["frameCount"]):
            raise ValueError("Label source content or frame geometry differs")
        report.update(sourceSha256=source["sourceSha256"], audioSha256=source["audioSha256"],
                      sampleRate=source["sampleRate"])
        if value["schemaVersion"] >= 2:
            report["scoreSupervision"] = score_report(item["score"], frame_count=source["frameCount"],
                                                     phoneme_count=len(item["label"]["phonemes"]),
                                                     explicit_silence=value["schemaVersion"] == 3)
        reports.append(report)
    reports.sort(key=lambda item: item["sourceId"])
    passed = all(item["consistencyPassed"] for item in reports)
    publish_new(output, dict(formatId="com.project-seam.training-label-batch-report", schemaVersion=value["schemaVersion"],
                            configurationSha256=expected_hash, sources=reports, consistencyPassed=passed,
                            trainingAdmitted=False, releaseEligible=False))
    return 0 if passed else 3


def segment_command(config: Path, expected_hash: str, source_path: Path, output: Path, *, resume: bool = False) -> dict:
    value = load_config(config, expected_hash)
    fields = {"formatId", "schemaVersion", "source", "sampleRate", "segmentId", "startFrame", "endFrame"}
    if isinstance(value, dict) and value.get("schemaVersion") in (2, 3):
        fields.update({"parentLabel", "vocabulary", "minimumConfidence"})
        if value["schemaVersion"] == 3:
            fields.add("parentScore")
    if (not isinstance(value, dict) or set(value) != fields
            or value["formatId"] != "com.project-seam.voice-training-segment-config"
            or type(value["schemaVersion"]) is not int or value["schemaVersion"] not in (1, 2, 3)):
        raise ValueError("Unsupported segment configuration")
    if source_path.is_symlink():
        raise ValueError("Source symlinks are unsupported")
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    with os.fdopen(os.open(source_path, flags), "rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or not 0 < before.st_size <= 64 * 1024 * 1024:
            raise ValueError("Segment source must be a regular file of at most 64 MiB")
        payload = stream.read(64 * 1024 * 1024 + 1)
        after = os.fstat(stream.fileno())
        if (len(payload) != before.st_size or (before.st_size, before.st_mtime_ns, before.st_ctime_ns)
                != (after.st_size, after.st_mtime_ns, after.st_ctime_ns)):
            raise ValueError("Segment source changed during capture")
    audio, record = segment_source(payload, source=value["source"], sample_rate=value["sampleRate"],
                                   segment_id=value["segmentId"], start_frame=value["startFrame"], end_frame=value["endFrame"])
    record.update(configurationSha256=expected_hash, path="audio.wav")
    if value["schemaVersion"] >= 2:
        bound = value["parentLabel"]
        if not isinstance(bound, dict) or set(bound) != {"sourceSha256", "audioSha256", "label"}:
            raise ValueError("Invalid parent label binding")
        parent = inspect_pcm_source(payload, expected_sha256=value["source"]["sourceSha256"], sample_rate=value["sampleRate"])
        label = bound["label"]
        if (not isinstance(label, dict) or label.get("sourceId") != value["source"]["sourceId"]
                or label.get("frameCount") != parent["frameCount"]
                or bound["sourceSha256"] != parent["sourceSha256"] or bound["audioSha256"] != parent["audioSha256"]):
            raise ValueError("Parent label differs from captured source")
        vocab = value["vocabulary"]
        if (not isinstance(vocab, list) or not 1 <= len(vocab) <= 4096
                or any(not isinstance(s, str) or not 1 <= len(s.encode()) <= 256 for s in vocab)
                or len(set(vocab)) != len(vocab)):
            raise ValueError("Invalid crop vocabulary")
        cropped = crop_labels(label, segment_id=value["segmentId"], start_frame=value["startFrame"],
                              end_frame=value["endFrame"], vocabulary=set(vocab), minimum_confidence=value["minimumConfidence"])
        record.update(schemaVersion=value["schemaVersion"], label=dict(sourceSha256=record["sourceSha256"],
                      audioSha256=record["audioSha256"], label=cropped))
        if value["schemaVersion"] == 3:
            record["label"]["score"] = crop_score(value["parentScore"], label,
                start_frame=value["startFrame"], end_frame=value["endFrame"])
    # Exclusive directory creation reserves this artifact. On interruption retain
    # partial output, never overwrite it; the manifest is the final commit marker.
    if resume and output.exists():
        if output.is_symlink() or not output.is_dir():
            raise ValueError("Resume target must be a stable non-symlink directory")
        names = {p.name for p in output.iterdir()}
        if names - {"audio.wav", "segment.json"}:
            raise ValueError("Resume target contains unexpected artifacts")
        if "audio.wav" not in names:
            raise ValueError("Resume requires complete captured audio; use a new destination")
        verify_exact_file(output / "audio.wav", audio)
        if "segment.json" in names:
            verify_exact_file(output / "segment.json", encode_report(record))
            return record
        publish_new(output / "segment.json", record)
        return record
    output.mkdir(mode=0o700)
    with (output / "audio.wav").open("xb") as stream:
        stream.write(audio)
        stream.flush()
        os.fsync(stream.fileno())
    publish_new(output / "segment.json", record)
    return record


def segment_batch_command(config: Path, expected_hash: str, root: Path, output: Path, report: Path, *, resume: bool) -> int:
    value = load_config(config, expected_hash)
    if (not isinstance(value, dict) or set(value) != {"formatId", "schemaVersion", "entries"}
            or value["formatId"] != "com.project-seam.voice-training-segment-batch"
            or type(value["schemaVersion"]) is not int or value["schemaVersion"] != 1
            or not isinstance(value["entries"], list) or not 1 <= len(value["entries"]) <= 64):
        raise ValueError("Invalid segment batch; expected 1..64 entries")
    root = root.resolve(strict=True)
    if not root.is_dir() or report.exists() or report.is_symlink():
        raise ValueError("Batch needs an input directory and a new report path")
    seen = set()
    for row in value["entries"]:
        if not isinstance(row, dict) or set(row) != {"configuration", "configurationSha256", "source", "outputName"}:
            raise ValueError("Invalid batch entry fields")
        for key in ("configuration", "source", "outputName"):
            name = row[key]
            if (not isinstance(name, str) or not 1 <= len(name) <= 128
                    or name in (".", "..") or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_." for c in name)):
                raise ValueError("Batch paths must be flat ASCII filenames")
        digest = row["configurationSha256"]
        if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            raise ValueError("Invalid batch configuration digest")
        if row["outputName"].casefold() in seen:
            raise ValueError("Duplicate batch output name")
        seen.add(row["outputName"].casefold())
    if output.exists():
        if not resume or output.is_symlink() or not output.is_dir():
            raise ValueError("Existing batch output requires explicit resume and a real directory")
    else:
        output.mkdir(mode=0o700)
    results, inventory, owners = [], [], {}
    for row in value["entries"]:
        try:
            artifact = segment_command(root / row["configuration"], row["configurationSha256"], root / row["source"],
                            output / row["outputName"], resume=resume)
            owners.setdefault(artifact["sourceId"], []).append(row["outputName"])
            inventory.append({key: artifact[key] for key in ("sourceId", "songId", "sessionId", "lineageId", "audioSha256")})
            results.append(dict(outputName=row["outputName"], configurationSha256=row["configurationSha256"],
                                sourceId=artifact["sourceId"], sourceSha256=artifact["sourceSha256"],
                                segmentRecordSha256=hashlib.sha256(encode_report(artifact)).hexdigest(),
                                status="PREPARED_UNAPPROVED"))
        except (ValueError, OSError, RecursionError) as error:
            results.append(dict(outputName=row["outputName"], configurationSha256=row["configurationSha256"],
                                status="REJECTED", diagnostic=str(error)[:256]))
    failed = sum(r["status"] == "REJECTED" for r in results)
    duplicates = [dict(sourceId=identity, outputs=sorted(outputs)) for identity, outputs in sorted(owners.items()) if len(outputs) > 1]
    publish_new(report, dict(formatId="com.project-seam.training-segment-batch-report", schemaVersion=2,
                            configurationSha256=expected_hash, entries=results, rejectedCount=failed,
                            duplicateSourceIds=duplicates,
                            splitSources=sorted(inventory, key=lambda r: r["sourceId"]) if not failed and not duplicates else [],
                            trainingAdmitted=False, releaseEligible=False))
    return 3 if failed or duplicates else 0


def verify_exact_file(path: Path, expected: bytes) -> None:
    if path.is_symlink():
        raise ValueError("Resume artifact cannot be a symlink")
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    with os.fdopen(os.open(path, flags), "rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_size != len(expected):
            raise ValueError("Resume artifact shape differs")
        captured = stream.read(len(expected) + 1)
        after = os.fstat(stream.fileno())
        if (captured != expected or (before.st_size, before.st_mtime_ns, before.st_ctime_ns)
                != (after.st_size, after.st_mtime_ns, after.st_ctime_ns)):
            raise ValueError("Resume artifact content differs")


def encode_report(result: dict) -> bytes:
    return (json.dumps(result, sort_keys=True, ensure_ascii=False, separators=(",", ":")) + "\n").encode()


def publish_new(output: Path, result: dict) -> None:
    encoded = encode_report(result)
    # Same-directory hard-link publication refuses both existing files and links.
    # Never overwrite a previous split. Parent directory must already exist.
    descriptor, temporary = tempfile.mkstemp(prefix=".seam-split-", dir=output.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.link(temporary, output)
    finally:
        os.unlink(temporary)


def main():
    parser = argparse.ArgumentParser(description="Original-model preparation tools; no musical or release approval.")
    commands = parser.add_subparsers(dest="command", required=True)
    split = commands.add_parser("split", help="Split captured source identities without leakage; never overwrite output.")
    split.add_argument("configuration", type=Path, help="UTF-8 JSON configuration, maximum 8 MiB")
    split.add_argument("configuration_sha256", help="Expected SHA-256 of the exact configuration bytes")
    split.add_argument("output", type=Path, help="New split JSON; parent must exist; binds config and source inventory hashes")
    prepare = commands.add_parser("prepare", help="Inspect captured mono PCM sources; no transforms or rights approval.")
    prepare.add_argument("configuration", type=Path, help="UTF-8 JSON configuration, maximum 8 MiB")
    prepare.add_argument("configuration_sha256", help="Expected SHA-256 of exact configuration bytes")
    prepare.add_argument("source_root", type=Path, help="Explicit stable source directory; max 64 MiB/file and 512 MiB total")
    prepare.add_argument("output", type=Path, help="New report; never overwrite; exit 3 retains per-source rejection diagnostics")
    labels = commands.add_parser("label-report", aliases=["labels"], help="Check source-bound labels and publish a correction queue; no training approval.")
    labels.add_argument("configuration", type=Path)
    labels.add_argument("configuration_sha256")
    labels.add_argument("source_root", type=Path)
    labels.add_argument("output", type=Path)
    segment = commands.add_parser("segment", help="Extract exact PCM frames; create a new artifact directory, never overwrite.")
    segment.add_argument("configuration", type=Path, help="Captured UTF-8 JSON, maximum 8 MiB")
    segment.add_argument("configuration_sha256", help="SHA-256 of exact configuration bytes")
    segment.add_argument("source_path", type=Path, help="Captured mono integer PCM WAV, maximum 64 MiB")
    segment.add_argument("output", type=Path, help="New directory containing audio.wav and final segment.json identity record")
    segment.add_argument("--resume", action="store_true", help="Verify exact existing audio; finish a missing record or verify complete output; never overwrite")
    batch = commands.add_parser("segment-batch", help="Process 1..64 captured phrase configs; retain per-entry failures; no approval.")
    batch.add_argument("configuration", type=Path)
    batch.add_argument("configuration_sha256")
    batch.add_argument("source_root", type=Path, help="Stable directory of flat source/config filenames; max 64 MiB per source read")
    batch.add_argument("output", type=Path, help="Private clip directory; existing directory requires --resume")
    batch.add_argument("report", type=Path, help="New report for this attempt, never overwritten; exit 3 means entry failures")
    batch.add_argument("--resume", action="store_true")
    permissions = commands.add_parser("permission-report", help="Inspect captured permission assertions and audio; does not authorize training.")
    permissions.add_argument("configuration", type=Path, help="Captured JSON, maximum 8 MiB")
    permissions.add_argument("configuration_sha256")
    permissions.add_argument("source_root", type=Path, help="Stable audio/evidence root; evidence max 4 MiB each, 64 MiB total")
    permissions.add_argument("output", type=Path, help="New report; no overwrite; exit 3 means missing asserted scopes")
    admit = commands.add_parser("admit", help="Verify reviewed source permissions; does not start or admit model training.")
    admit.add_argument("configuration", type=Path, help="Captured permission config, at most 8 MiB")
    admit.add_argument("configuration_sha256")
    admit.add_argument("source_root", type=Path)
    admit.add_argument("output", type=Path, help="New time-bound source admission report; never overwritten")
    admit.add_argument("--review", type=Path, required=True, help="Supplied signed review JSON, at most 8 MiB")
    admit.add_argument("--review-sha256", required=True, help="Captured SHA-256 of exact review file bytes")
    admit.add_argument("--policy", type=Path, required=True, help="Externally trusted reviewer policy JSON")
    admit.add_argument("--policy-file-sha256", required=True, help="Captured SHA-256 of exact policy file bytes")
    admit.add_argument("--trusted-policy-sha256", required=True, help="Independent canonical policy hash; never infer from the review")
    args = parser.parse_args()
    try:
        if args.command == "admit":
            review = load_config(args.review, args.review_sha256)
            policy = load_config(args.policy, args.policy_file_sha256)
            result = admit_sources(args.configuration, args.configuration_sha256, args.source_root,
                review=review, policy=policy, trusted_policy_sha256=args.trusted_policy_sha256, now=int(time.time()))
            if int(time.time()) >= result["expiresAt"]:
                raise ValueError("Review expired while inspecting sources")
            publish_new(args.output, result)
            return 0
        if args.command == "permission-report":
            return permission_command(args.configuration, args.configuration_sha256, args.source_root, args.output)
        if args.command == "segment-batch":
            return segment_batch_command(args.configuration, args.configuration_sha256, args.source_root,
                                         args.output, args.report, resume=args.resume)
        if args.command == "segment":
            segment_command(args.configuration, args.configuration_sha256, args.source_path, args.output, resume=args.resume)
            return 0
        if args.command in ("labels", "label-report"):
            return labels_command(args.configuration, args.configuration_sha256, args.source_root, args.output)
        if args.command == "prepare":
            return prepare_command(args.configuration, args.configuration_sha256, args.source_root, args.output)
        split_command(args.configuration, args.configuration_sha256, args.output)
    except (ValueError, OSError, RecursionError) as error:
        print(f"{args.command} failed: {str(error)[:256]}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
