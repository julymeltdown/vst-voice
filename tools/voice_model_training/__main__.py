"""Original-model dataset tooling. No command grants source or release approval."""
import argparse
import hashlib
import json
import math
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
from .review import verify_training_review, verify_label_review
from .features import apply_pitch_features, pitch_corrections
from .native_features import extract_pitch
from .label_edits import apply_label_edits
from .conditioning import build_conditioning
from .qualification import qualify_command
from .generated_teacher import label_config_from_exports

# A label configuration inlines every source's labels, so its size grows with the
# corpus rather than staying constant: 1.2 MiB for 40 songs, 11.4 MiB for 400. The
# earlier 8 MiB cap therefore refused corpora that the rest of this toolchain already
# accepts (source preparation takes 10000 records, and the neighbouring capture
# budgets are 64 MiB), which made a trainable corpus size unreachable. The bound is
# raised to the next established budget rather than removed.
MAXIMUM_CONFIGURATION_BYTES = 64 * 1024 * 1024


def acoustic_targets_command(config: Path, expected_hash: str, source: Path, output: Path) -> None:
    value = load_config(config, expected_hash)
    if (not isinstance(value, dict) or set(value) != {"formatId", "schemaVersion", "sourceSha256", "sampleRate",
            "fftSize", "hopSize", "bins", "minimumHz", "maximumHz"}
            or value["formatId"] != "com.project-seam.training-acoustic-config"
            or type(value["schemaVersion"]) is not int or value["schemaVersion"] != 1):
        raise ValueError("Unsupported acoustic target configuration")
    if output.exists() or output.is_symlink():
        raise ValueError("Acoustic output directory must be new")
    if source.is_symlink():
        raise ValueError("Acoustic source cannot be a symlink")
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    with os.fdopen(os.open(source, flags), "rb") as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or not 1 <= info.st_size <= 64 * 1024 * 1024:
            raise ValueError("Acoustic source must be a regular WAV at most 64 MiB")
        payload = stream.read(64 * 1024 * 1024 + 1)
    try:
        from .acoustics import wav_log_mel_targets
        record, targets = wav_log_mel_targets(payload, expected_sha256=value["sourceSha256"],
            sample_rate=value["sampleRate"], fft_size=value["fftSize"], hop_size=value["hopSize"],
            bins=value["bins"], minimum_hz=value["minimumHz"], maximum_hz=value["maximumHz"])
    except ImportError as error:
        raise ValueError("Acoustic extraction requires the optional NumPy environment") from error
    record["configurationSha256"] = expected_hash
    record["targetPath"] = "mel.f32le"
    output.mkdir(mode=0o700)
    with (output / "mel.f32le").open("xb") as stream:
        stream.write(targets.astype("<f4", copy=False).tobytes(order="C"))
        stream.flush()
        os.fsync(stream.fileno())
    # Metadata is the final publication marker; a binary alone is incomplete.
    publish_new(output / "target.json", record)


def correct_labels_command(config: Path, expected_hash: str, root: Path, output: Path) -> None:
    value = load_config(config, expected_hash)
    fields = {"formatId", "schemaVersion", "source", "sampleRate", "label", "edits", "vocabulary", "minimumConfidence"}
    if (not isinstance(value, dict) or set(value) != fields
            or value["formatId"] != "com.project-seam.training-label-correction-config"
            or type(value["schemaVersion"]) is not int or value["schemaVersion"] != 1):
        raise ValueError("Unsupported label correction configuration")
    vocab = value["vocabulary"]
    if (not isinstance(vocab, list) or not 1 <= len(vocab) <= 4096
            or any(not isinstance(s, str) or not 1 <= len(s.encode()) <= 256 for s in vocab)
            or len(set(vocab)) != len(vocab)):
        raise ValueError("Invalid correction vocabulary")
    prepared = prepare_sources(root, [value["source"]], sample_rate=value["sampleRate"])
    if prepared["rejectedCount"]:
        raise ValueError("Correction source inspection failed")
    audio = prepared["sources"][0]["inspection"]
    label = value["label"]
    if (not isinstance(label, dict) or label.get("sourceId") != value["source"]["sourceId"]
            or label.get("frameCount") != audio["frameCount"]):
        raise ValueError("Correction label differs from inspected source identity/geometry")
    updated = apply_label_edits(label, value["edits"], vocabulary=set(vocab), minimum_confidence=value["minimumConfidence"])
    publish_new(output, dict(formatId="com.project-seam.training-label-correction", schemaVersion=1,
        configurationSha256=expected_hash, sourceSha256=audio["sourceSha256"], audioSha256=audio["audioSha256"],
        parentLabelSha256=hashlib.sha256(encode_report(label)).hexdigest(),
        labelSha256=hashlib.sha256(encode_report(updated)).hexdigest(), label=updated, edits=value["edits"],
        consistency=label_report(updated, vocabulary=set(vocab), minimum_confidence=value["minimumConfidence"]),
        reviewAuthenticated=False, trainingAdmitted=False, releaseEligible=False))


def refresh_pitch_command(config: Path, expected_hash: str, root: Path, executable: Path, output: Path) -> None:
    value = load_config(config, expected_hash)
    fields = {"formatId", "schemaVersion", "source", "sampleRate", "label", "vocabulary", "minimumConfidence"}
    if (not isinstance(value, dict) or set(value) != fields
            or value["formatId"] != "com.project-seam.training-pitch-refresh-config"
            or type(value["schemaVersion"]) is not int or value["schemaVersion"] != 1):
        raise ValueError("Unsupported pitch refresh configuration")
    vocabulary = value["vocabulary"]
    if (not isinstance(vocabulary, list) or not 1 <= len(vocabulary) <= 4096
            or any(not isinstance(s, str) or not 1 <= len(s.encode()) <= 256 for s in vocabulary)
            or len(set(vocabulary)) != len(vocabulary)):
        raise ValueError("Invalid pitch refresh vocabulary")
    root = root.resolve(strict=True)
    prepared = prepare_sources(root, [value["source"]], sample_rate=value["sampleRate"])
    if prepared["rejectedCount"]:
        raise ValueError("Pitch refresh source inspection failed")
    label = value["label"]
    label_report(label, vocabulary=set(vocabulary), minimum_confidence=value["minimumConfidence"])
    source = value["source"]
    if label["sourceId"] != source["sourceId"]:
        raise ValueError("Pitch refresh label source identity differs")
    features = extract_pitch(executable, root / source["path"])
    updated = apply_pitch_features(label, features, source_sha256=source["sourceSha256"],
        sample_rate=value["sampleRate"], vocabulary=set(vocabulary), minimum_confidence=value["minimumConfidence"])
    corrections = pitch_corrections(label, features, source_sha256=source["sourceSha256"],
        sample_rate=value["sampleRate"], vocabulary=set(vocabulary), minimum_confidence=value["minimumConfidence"])
    publish_new(output, dict(formatId="com.project-seam.training-refreshed-pitch-label", schemaVersion=2,
        configurationSha256=expected_hash, sourceSha256=source["sourceSha256"],
        audioSha256=prepared["sources"][0]["inspection"]["audioSha256"], label=updated,
        features=features, pitchCorrections=corrections, reviewAuthenticated=False, trainingAdmitted=False, releaseEligible=False))


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


def admit_segment(permission_config: Path, permission_hash: str, source_root: Path, *,
                  segment_config: Path, segment_hash: str, source_path: Path, output: Path,
                  review: dict, policy: dict, trusted_policy_sha256: str, now: int, resume: bool = False,
                  fresh_pitch_extractor: Path | None = None) -> dict:
    """Authenticate the parent and derive the clip; never trust a stored receipt."""
    admission = admit_sources(permission_config, permission_hash, source_root, review=review,
                              policy=policy, trusted_policy_sha256=trusted_policy_sha256, now=now)
    config = load_config(segment_config, segment_hash)
    if not isinstance(config, dict) or not isinstance(config.get("source"), dict):
        raise ValueError("Segment lacks captured parent identity")
    declared = config["source"]
    parent = next((r for r in admission["sources"] if r["sourceId"] == declared.get("sourceId")), None)
    if parent is None or any(declared.get(k) != parent[k] for k in ("sourceSha256", "songId", "sessionId", "lineageId")):
        raise ValueError("Segment parent differs from reviewed source or lineage")
    artifact = segment_command(segment_config, segment_hash, source_path, output, resume=resume,
                               fresh_pitch_extractor=fresh_pitch_extractor)
    if artifact["parentAudioSha256"] != parent["audioSha256"]:
        raise ValueError("Derived segment PCM parent differs from inspected admission")
    return dict(formatId="com.project-seam.training-segment-admission", schemaVersion=1,
                parentConfigurationSha256=permission_hash, segmentConfigurationSha256=segment_hash,
                policySha256=trusted_policy_sha256, reviewSha256=admission["reviewSha256"],
                expiresAt=admission["expiresAt"], verifiedAt=now, identityId=parent["identityId"],
                segmentRecordSha256=hashlib.sha256(encode_report(artifact)).hexdigest(),
                sourceId=artifact["sourceId"], sourceSha256=artifact["sourceSha256"], audioSha256=artifact["audioSha256"],
                sourcePermissionsAdmitted=True, trainingAdmitted=False, releaseEligible=False)


def _flat_root_file(root: Path, name: str, *, description: str) -> Path:
    if (not isinstance(name, str) or not 1 <= len(name) <= 128 or name in (".", "..")
            or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_./" for c in name)
            or name.startswith("/") or "\\" in name or any(part in ("", ".", "..") for part in name.split("/"))):
        raise ValueError(f"{description} path must be a contained relative path")
    path = root
    for part in name.split("/"):
        path /= part
        if path.is_symlink():
            raise ValueError(f"{description} symlinks are unsupported")
    if not path.resolve(strict=True).is_relative_to(root.resolve(strict=True)):
        raise ValueError(f"{description} escapes its root")
    return path


def _verify_pitch_extractor(path: Path, expected_sha256: str) -> None:
    if (not isinstance(expected_sha256, str) or len(expected_sha256) != 64
            or any(c not in "0123456789abcdef" for c in expected_sha256)
            or path.is_symlink() or not os.access(path, os.X_OK)):
        raise ValueError("Fresh pitch extractor requires a captured digest and executable file")
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    with os.fdopen(os.open(path, flags), "rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or not 1 <= before.st_size <= 256 * 1024 * 1024:
            raise ValueError("Fresh pitch extractor exceeds its executable-file bound")
        digest = hashlib.sha256()
        size = 0
        while True:
            block = stream.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
            size += len(block)
        after = os.fstat(stream.fileno())
    if ((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns)
            != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns)
            or size != before.st_size or digest.hexdigest() != expected_sha256):
        raise ValueError("Fresh pitch extractor differs from its captured digest")


def _revalidate_derived_segments(*, permission_config: Path, permission_hash: str, root: Path,
                                 permission_admission: dict, label_config: dict,
                                 rights_review: dict, rights_policy: dict, rights_anchor: str,
                                 now: int, derived_segments: list[dict],
                                 fresh_pitch_extractor: Path | None,
                                 fresh_pitch_extractor_sha256: str | None) -> tuple[list[dict], list[dict]]:
    if not isinstance(derived_segments, list) or len(derived_segments) > 10000:
        raise ValueError("Derived segment inventory exceeds its bound")
    permission_value = load_config(permission_config, permission_hash)
    parent_inputs = {row["sourceId"]: row for row in permission_value["sources"]}
    admitted_parents = {row["sourceId"]: row for row in permission_admission["sources"]}
    labels_by_id = {row["label"]["sourceId"]: row for row in label_config["labels"]}
    label_sources = {row["sourceId"]: row for row in label_config["sources"]}
    if len(labels_by_id) != len(label_config["labels"]) or len(label_sources) != len(label_config["sources"]):
        raise ValueError("Derived segment join requires unique label/source identities")
    additions, bindings, seen_children = [], [], set()
    for item in derived_segments:
        if (not isinstance(item, dict) or set(item) != {"configuration", "artifactDirectory"}
                or not isinstance(item["configuration"], dict)
                or set(item["configuration"]) != {"path", "sha256"}):
            raise ValueError("Invalid derived segment reference")
        reference = item["configuration"]
        config_path = _flat_root_file(root, reference["path"], description="Segment configuration")
        artifact_directory = _flat_root_file(root, item["artifactDirectory"], description="Segment artifact")
        if artifact_directory.is_symlink() or not artifact_directory.is_dir():
            raise ValueError("Derived segment artifact must be an existing real directory")
        if {child.name for child in artifact_directory.iterdir()} != {"audio.wav", "segment.json"}:
            raise ValueError("Derived segment artifact must contain only its audio and final record")
        record_path = artifact_directory / "segment.json"
        if record_path.is_symlink() or not record_path.is_file():
            raise ValueError("Derived segment record must be a regular file")
        segment_config = load_config(config_path, reference["sha256"])
        parent = segment_config.get("source") if isinstance(segment_config, dict) else None
        parent_id = parent.get("sourceId") if isinstance(parent, dict) else None
        if parent_id not in parent_inputs or parent_id not in admitted_parents:
            raise ValueError("Derived segment parent is not in freshly admitted source permissions")
        parent_label = labels_by_id.get(parent_id)
        if (parent_label is None or segment_config.get("schemaVersion") != 3
                or not isinstance(segment_config.get("parentLabel"), dict)
                or segment_config["parentLabel"].get("label") != parent_label["label"]
                or segment_config["parentLabel"].get("sourceSha256") != parent_label["sourceSha256"]
                or segment_config["parentLabel"].get("audioSha256") != parent_label["audioSha256"]
                or segment_config.get("parentScore") != parent_label.get("score")):
            raise ValueError("Segment crop must derive from the currently reviewed parent label and score")
        if (segment_config["source"].get("sourceSha256") != admitted_parents[parent_id]["sourceSha256"]
                or any(segment_config["source"].get(key) != admitted_parents[parent_id][key]
                       for key in ("songId", "sessionId", "lineageId"))):
            raise ValueError("Segment parent identity differs from fresh source admission")
        child_id = segment_config.get("segmentId")
        if not isinstance(child_id, str) or child_id in seen_children or child_id in admitted_parents:
            raise ValueError("Derived segment identity is duplicate or shadows a directly admitted source")
        child_label = labels_by_id.get(child_id)
        child_source = label_sources.get(child_id)
        expected_audio_path = f"{item['artifactDirectory'].rstrip('/')}/audio.wav"
        if (child_label is None or child_source is None or child_source["path"] != expected_audio_path
                or any(child_source[key] != segment_config["source"][key]
                       for key in ("songId", "sessionId", "lineageId"))):
            raise ValueError("Derived child must have a reviewed label at its admitted artifact path")
        hop_size = parent_label["label"].get("hopSize")
        if type(hop_size) is not int or hop_size <= 0:
            raise ValueError("Parent label has no valid analysis hop")
        needs_fresh_pitch = segment_config.get("startFrame") % hop_size != 0
        if needs_fresh_pitch:
            if fresh_pitch_extractor is None or fresh_pitch_extractor_sha256 is None:
                raise ValueError("Off-grid derived crops require the captured fresh pitch extractor")
            _verify_pitch_extractor(fresh_pitch_extractor, fresh_pitch_extractor_sha256)
        parent_source_path = _flat_root_file(root, parent_inputs[parent_id]["path"], description="Parent source")
        admission = admit_segment(permission_config, permission_hash, root,
            segment_config=config_path, segment_hash=reference["sha256"], source_path=parent_source_path,
            output=artifact_directory, review=rights_review, policy=rights_policy,
            trusted_policy_sha256=rights_anchor, now=now, resume=True,
            fresh_pitch_extractor=fresh_pitch_extractor if needs_fresh_pitch else None)
        extractor_binding = {}
        if needs_fresh_pitch:
            _verify_pitch_extractor(fresh_pitch_extractor, fresh_pitch_extractor_sha256)
            extractor_binding["freshPitchExtractorSha256"] = fresh_pitch_extractor_sha256
        segment_record = load_config(record_path, admission["segmentRecordSha256"])
        expected_label = segment_record.get("label") if isinstance(segment_record, dict) else None
        if (not isinstance(expected_label, dict) or child_label["sourceSha256"] != admission["sourceSha256"]
                or child_label["audioSha256"] != admission["audioSha256"]
                or child_label["label"] != expected_label.get("label")
                or child_label.get("score") != expected_label.get("score")
                or child_source["sourceSha256"] != admission["sourceSha256"]):
            raise ValueError("Reviewed child labels differ from freshly regenerated segment supervision")
        parent_admission = admitted_parents[parent_id]
        additions.append(dict(parent_admission, sourceId=child_id, sourceSha256=admission["sourceSha256"],
            audioSha256=admission["audioSha256"], sampleRate=segment_record["sampleRate"],
            frameCount=child_label["label"]["frameCount"], derivedFromSourceId=parent_id,
            segmentRecordSha256=admission["segmentRecordSha256"]))
        bindings.append(dict(segmentConfigurationSha256=reference["sha256"],
            segmentRecordSha256=admission["segmentRecordSha256"],
            sourceId=child_id, sourceSha256=admission["sourceSha256"], audioSha256=admission["audioSha256"],
            parentSourceId=parent_id, rightsReviewSha256=admission["reviewSha256"], **extractor_binding))
        seen_children.add(child_id)
    return additions, sorted(bindings, key=lambda row: row["sourceId"])


def assemble_dataset(permission_config: Path, permission_hash: str, label_config: Path, label_hash: str,
                     root: Path, *, rights_review: dict, rights_policy: dict, rights_anchor: str,
                     label_review: dict, label_policy: dict, label_anchor: str, now: int,
                     seed: str, held_out_songs: list[str], conditioning_directory: Path | None = None,
                     reuse_conditioning: bool = False, derived_segments: list[dict] | None = None,
                     fresh_pitch_extractor: Path | None = None,
                     fresh_pitch_extractor_sha256: str | None = None) -> dict:
    """Revalidate both authorities before building a source/label/split snapshot."""
    if type(reuse_conditioning) is not bool or (reuse_conditioning and conditioning_directory is None):
        raise ValueError("Read-only conditioning reuse requires a shard directory")
    if (fresh_pitch_extractor is None) != (fresh_pitch_extractor_sha256 is None):
        raise ValueError("Fresh pitch extractor path and captured digest must be supplied together")
    if fresh_pitch_extractor is not None:
        _verify_pitch_extractor(fresh_pitch_extractor, fresh_pitch_extractor_sha256)
    rights = admit_sources(permission_config, permission_hash, root, review=rights_review,
                           policy=rights_policy, trusted_policy_sha256=rights_anchor, now=now)
    annotations = admit_labels(label_config, label_hash, root, review=label_review,
                               policy=label_policy, trusted_policy_sha256=label_anchor, now=now)
    captured_labels = load_config(label_config, label_hash)
    added_sources, derived_bindings = _revalidate_derived_segments(permission_config=permission_config,
        permission_hash=permission_hash, root=root, permission_admission=rights,
        label_config=captured_labels, rights_review=rights_review, rights_policy=rights_policy,
        rights_anchor=rights_anchor, now=now, derived_segments=[] if derived_segments is None else derived_segments,
        fresh_pitch_extractor=fresh_pitch_extractor,
        fresh_pitch_extractor_sha256=fresh_pitch_extractor_sha256)
    rights_sources = rights["sources"] + added_sources
    rights_by_id = {r["sourceId"]: r for r in rights_sources}
    labels_by_id = {r["sourceId"]: r for r in annotations["sources"]}
    if set(rights_by_id) != set(labels_by_id):
        raise ValueError("Rights and label admission cover different source sets")
    declared = {r["sourceId"]: r for r in captured_labels["sources"]}
    for identity, source in rights_by_id.items():
        if any(source[k] != labels_by_id[identity][k] for k in ("sourceSha256", "audioSha256", "sampleRate")):
            raise ValueError("Rights and labels refer to different audio")
        if any(source[k] != declared[identity][k] for k in ("songId", "sessionId", "lineageId")):
            raise ValueError("Reviewed source lineage differs between configurations")
    rows = [{k: r[k] for k in ("sourceId", "songId", "sessionId", "lineageId", "audioSha256")}
            for r in rights_sources]
    split = split_sources(rows, seed=seed, held_out_songs=held_out_songs)
    # Preflight total work; sharded mode retains only one phrase's expanded rows.
    total_frames = sum(len(entry["label"]["f0Hz"]) for entry in captured_labels["labels"])
    if total_frames > (1000000 if conditioning_directory is not None else 65536):
        raise ValueError("Dataset conditioning exceeds the selected storage frame budget")
    if any(len(entry["label"]["f0Hz"]) > 65536 for entry in captured_labels["labels"]):
        raise ValueError("Segment long sources before conditioning (65536 frames per phrase)")
    if conditioning_directory is not None:
        if reuse_conditioning:
            if conditioning_directory.is_symlink() or not conditioning_directory.is_dir():
                raise ValueError("Conditioning reuse requires an existing real directory")
        else:
            conditioning_directory.mkdir(mode=0o700)  # Exclusive: never mix attempts.
    conditioning, stored_bytes = [], 0
    for index, entry in enumerate(sorted(captured_labels["labels"], key=lambda e: e["label"]["sourceId"])):
        control = entry.get("conditioning")
        features = build_conditioning(entry["label"], entry["score"],
                        vocabulary=captured_labels["vocabulary"], minimum_confidence=captured_labels["minimumConfidence"],
                        breathiness=None if control is None else control["breathiness"])
        if conditioning_directory is None:
            conditioning.append(features)
        else:
            payload = encode_report(features)
            stored_bytes += len(payload)
            if stored_bytes > 256 * 1024 * 1024:
                raise ValueError("Conditioning shard disk budget exceeded; incomplete attempt retained")
            name = f"phrase-{index:06d}.json"
            if reuse_conditioning:
                verify_exact_file(conditioning_directory / name, payload)
            else:
                publish_new(conditioning_directory / name, features)
            conditioning.append(dict(sourceId=features["sourceId"], path=name,
                sha256=hashlib.sha256(payload).hexdigest(), sizeBytes=len(payload), frameCount=len(features["frames"]),
                conditioningRevision=features["conditioningRevision"],
                conditioningControls=["breathiness"] if features["hasBreathiness"] else []))
    conditioning_hash = hashlib.sha256(encode_report(conditioning)).hexdigest()
    identity = dict(permissionConfigurationSha256=permission_hash, labelConfigurationSha256=label_hash,
                    rightsReviewSha256=rights["reviewSha256"], labelReviewSha256=annotations["reviewSha256"],
                    conditioningSha256=conditioning_hash, split=split)
    if derived_bindings:
        identity["derivedSegments"] = derived_bindings
    issues = [dict(code="missing-partition", partition=p) for p in split["missingPartitions"]]
    if split["duplicateAudioGroups"]:
        issues.append(dict(code="duplicate-selection-review-required"))
    result = dict(formatId="com.project-seam.training-dataset-snapshot", schemaVersion=3 if conditioning_directory is not None else 2,
                datasetSha256=hashlib.sha256(encode_report(identity)).hexdigest(), bindings=identity,
                expiresAt=min(rights["expiresAt"], annotations["expiresAt"]), verifiedAt=now,
                sources=rights_sources, labels=captured_labels["labels"], vocabulary=captured_labels["vocabulary"],
                conditioning=conditioning, conditioningFrameCount=total_frames,
                preparationIssues=issues, sourcePermissionsAdmitted=True, labelsAdmitted=True,
                trainingAdmitted=False, releaseEligible=False)
    if conditioning_directory is not None:
        result["conditioningDirectory"] = conditioning_directory.name
        result["conditioningBytes"] = stored_bytes
    return result


def load_dataset_inputs(config: Path, expected_hash: str, root: Path, *,
                        rights_anchor: str, label_anchor: str) -> dict:
    """Capture the shared assembly/training inputs without publishing anything.

    File hashes are checked here; source bytes, signatures and review lifetime
    are checked by assemble_dataset at the actual use boundary.
    """
    value = load_config(config, expected_hash)
    refs = {"permissionConfig", "labelConfig", "rightsReview", "rightsPolicy", "labelReview", "labelPolicy"}
    fields = refs | {"formatId", "schemaVersion", "seed", "heldOutSongs"}
    schema = value.get("schemaVersion") if isinstance(value, dict) else None
    if schema in (2, 3):
        fields.add("derivedSegments")
    if schema == 3:
        fields.add("freshPitchExtractor")
    if (not isinstance(value, dict) or set(value) != fields
            or value["formatId"] != "com.project-seam.training-dataset-config"
            or type(schema) is not int or schema not in (1, 2, 3)):
        raise ValueError("Unsupported dataset assembly configuration")
    root = root.resolve(strict=True)
    references = {}
    for key in refs:
        ref = value[key]
        if not isinstance(ref, dict) or set(ref) != {"path", "sha256"}:
            raise ValueError("Dataset references require path and captured SHA-256")
        name = ref["path"]
        if (not isinstance(name, str) or not 1 <= len(name) <= 128 or name in (".", "..")
                or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_." for c in name)):
            raise ValueError(f"Dataset {key} reference must be a flat ASCII filename")
        references[key] = load_config(root / name, ref["sha256"])
    permission, labels = value["permissionConfig"], value["labelConfig"]
    derived_segments = value.get("derivedSegments", [])
    if not isinstance(derived_segments, list) or len(derived_segments) > 10000:
        raise ValueError("Derived segment inventory exceeds its bound")
    captured_segments = []
    for item in derived_segments:
        if (not isinstance(item, dict) or set(item) != {"configuration", "artifactDirectory"}
                or not isinstance(item["configuration"], dict)
                or set(item["configuration"]) != {"path", "sha256"}):
            raise ValueError("Invalid derived segment reference")
        reference = item["configuration"]
        name = reference["path"]
        if (not isinstance(name, str) or not 1 <= len(name) <= 128 or name in (".", "..")
                or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_./" for c in name)
                or name.startswith("/") or "\\" in name
                or any(part in ("", ".", "..") for part in name.split("/"))):
            raise ValueError("Segment configuration path must be contained and relative")
        load_config(_flat_root_file(root, name, description="Segment configuration"), reference["sha256"])
        captured_segments.append(dict(configuration=dict(path=name, sha256=reference["sha256"]),
                                      artifactDirectory=item["artifactDirectory"]))
    extractor = value.get("freshPitchExtractor") if schema == 3 else None
    captured_extractor = {}
    if extractor is not None:
        if not isinstance(extractor, dict) or set(extractor) != {"path", "sha256"}:
            raise ValueError("Fresh pitch extractor reference requires path and SHA-256")
        extractor_path = _flat_root_file(root, extractor["path"], description="Fresh pitch extractor")
        _verify_pitch_extractor(extractor_path, extractor["sha256"])
        captured_extractor = dict(fresh_pitch_extractor=extractor_path,
                                  fresh_pitch_extractor_sha256=extractor["sha256"])
    return dict(permission_config=root / permission["path"], permission_hash=permission["sha256"],
        label_config=root / labels["path"], label_hash=labels["sha256"], root=root,
        rights_review=references["rightsReview"], rights_policy=references["rightsPolicy"], rights_anchor=rights_anchor,
        label_review=references["labelReview"], label_policy=references["labelPolicy"], label_anchor=label_anchor,
        seed=value["seed"], held_out_songs=value["heldOutSongs"], derived_segments=captured_segments,
        **captured_extractor)


def assemble_dataset_command(config: Path, expected_hash: str, root: Path, output: Path, *,
                             rights_anchor: str, label_anchor: str, conditioning_directory: Path | None = None,
                             reuse_conditioning: bool = False) -> int:
    if output.exists() or output.is_symlink():
        raise ValueError("Dataset snapshot must be new")
    if conditioning_directory is not None:
        if (conditioning_directory.parent.resolve(strict=True) != output.parent.resolve(strict=True)
                or conditioning_directory.name == output.name
                or (conditioning_directory.exists() and not reuse_conditioning) or conditioning_directory.is_symlink()):
            raise ValueError("Conditioning directory must be a sibling; existing shards require explicit reuse")
    inputs = load_dataset_inputs(config, expected_hash, root,
                                 rights_anchor=rights_anchor, label_anchor=label_anchor)
    snapshot = assemble_dataset(**inputs, now=int(time.time()),
        conditioning_directory=conditioning_directory, reuse_conditioning=reuse_conditioning)
    if int(time.time()) >= snapshot["expiresAt"]:
        raise ValueError("Dataset review expired during assembly")
    snapshot["assemblyConfigurationSha256"] = expected_hash
    publish_new(output, snapshot)
    return 3 if snapshot["preparationIssues"] else 0


def load_config(config: Path, expected_hash: str) -> dict:
    if config.is_symlink() or not config.is_file():
        raise ValueError("Configuration must be a regular non-symlink file")
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    with os.fdopen(os.open(config, flags), "rb") as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise ValueError("Opened configuration is not a regular file")
        payload = stream.read(MAXIMUM_CONFIGURATION_BYTES + 1)
    if len(payload) > MAXIMUM_CONFIGURATION_BYTES:
        raise ValueError("Configuration exceeds the capture budget")
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


def inspect_label_config(config: Path, expected_hash: str, root: Path) -> dict:
    value = load_config(config, expected_hash)
    fields = {"formatId", "schemaVersion", "sampleRate", "sources", "labels", "vocabulary", "minimumConfidence"}
    if (not isinstance(value, dict) or set(value) != fields
            or value["formatId"] != "com.project-seam.voice-training-label-config"
            or type(value["schemaVersion"]) is not int or value["schemaVersion"] not in (1, 2, 3, 4)):
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
        if value["schemaVersion"] == 4:
            item_fields.add("conditioning")
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
                                                     explicit_silence=value["schemaVersion"] >= 3)
        if value["schemaVersion"] == 4:
            conditioning = item["conditioning"]
            expected = len(item["label"]["f0Hz"])
            if (not isinstance(conditioning, dict)
                    or set(conditioning) != {"revision", "breathiness"}
                    or type(conditioning["revision"]) is not int or conditioning["revision"] != 2
                    or not isinstance(conditioning["breathiness"], list)
                    or len(conditioning["breathiness"]) != expected
                    or any(type(control) not in (int, float) or not math.isfinite(control)
                           or not 0.0 <= control <= 1.0 for control in conditioning["breathiness"])):
                raise ValueError("Breathiness supervision must be revision 2 normalized [0, 1] at every analysis frame")
            report["conditioningSupervision"] = dict(revision=2, controls=["breathiness"], frameCount=expected)
        reports.append(report)
    reports.sort(key=lambda item: item["sourceId"])
    passed = all(item["consistencyPassed"] for item in reports)
    return dict(formatId="com.project-seam.training-label-batch-report", schemaVersion=value["schemaVersion"],
                            configurationSha256=expected_hash, sources=reports, consistencyPassed=passed,
                            trainingAdmitted=False, releaseEligible=False)


def labels_command(config: Path, expected_hash: str, root: Path, output: Path) -> int:
    report = inspect_label_config(config, expected_hash, root)
    publish_new(output, report)
    return 0 if report["consistencyPassed"] else 3


def admit_labels(config: Path, expected_hash: str, root: Path, *, review: dict, policy: dict,
                 trusted_policy_sha256: str, now: int) -> dict:
    verified = verify_label_review(review, policy=policy, trusted_policy_sha256=trusted_policy_sha256,
                                   configuration_sha256=expected_hash, now=now)
    inspected = inspect_label_config(config, expected_hash, root)
    if inspected["schemaVersion"] not in (3, 4):
        raise ValueError("Label admission requires explicit score and silence ownership")
    admitted = []
    for source in inspected["sources"]:
        if any(issue["code"] != "review-revision-missing" for issue in source["correctionQueue"]):
            raise ValueError("Signed label configuration still contains unresolved corrections")
        admitted.append(dict(sourceId=source["sourceId"], sourceSha256=source["sourceSha256"],
                             audioSha256=source["audioSha256"], sampleRate=source["sampleRate"],
                             scoreSupervision=source["scoreSupervision"]))
    return dict(formatId="com.project-seam.training-label-admission", schemaVersion=1,
                configurationSha256=expected_hash, policySha256=trusted_policy_sha256,
                reviewSha256=verified["reviewSha256"], signerId=verified["signerId"],
                verifiedAt=now, expiresAt=review["expiresAt"], sources=admitted,
                labelsAdmitted=True, sourcePermissionsAdmitted=False, trainingAdmitted=False, releaseEligible=False)


def segment_command(config: Path, expected_hash: str, source_path: Path, output: Path, *, resume: bool = False,
                    fresh_pitch_extractor: Path | None = None) -> dict:
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
    fresh = None
    if fresh_pitch_extractor is not None:
        if value["schemaVersion"] < 2:
            raise ValueError("Fresh crop pitch requires a label-bearing configuration")
        with tempfile.TemporaryDirectory(prefix="seam-pitch-crop-") as temporary:
            captured_path = Path(temporary) / "clip.wav"
            captured_path.write_bytes(audio)
            fresh = extract_pitch(fresh_pitch_extractor, captured_path)
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
                              end_frame=value["endFrame"], vocabulary=set(vocab), minimum_confidence=value["minimumConfidence"],
                              fresh_features=fresh, child_source_sha256=record["sourceSha256"], sample_rate=value["sampleRate"])
        record.update(schemaVersion=value["schemaVersion"], label=dict(sourceSha256=record["sourceSha256"],
                      audioSha256=record["audioSha256"], label=cropped))
        if value["schemaVersion"] == 3:
            record["label"]["score"] = crop_score(value["parentScore"], label,
                start_frame=value["startFrame"], end_frame=value["endFrame"])
        if fresh is not None:
            corrections = pitch_corrections(cropped, fresh, source_sha256=record["sourceSha256"],
                sample_rate=value["sampleRate"], vocabulary=set(vocab), minimum_confidence=value["minimumConfidence"])
            record.update(schemaVersion=5, pitchFeatures=fresh, pitchCorrections=corrections)
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


def segment_batch_command(config: Path, expected_hash: str, root: Path, output: Path, report: Path, *, resume: bool,
                          fresh_pitch_extractor: Path | None = None) -> int:
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
                            output / row["outputName"], resume=resume, fresh_pitch_extractor=fresh_pitch_extractor)
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


def encode_report(result: dict | list) -> bytes:
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
    acoustic = commands.add_parser("acoustic-targets", help="Extract byte-bound log-mel data; no training or source-rights approval.")
    acoustic.add_argument("configuration", type=Path, help="Captured explicit analysis profile JSON, at most 8 MiB")
    acoustic.add_argument("configuration_sha256")
    acoustic.add_argument("source", type=Path, help="Mono integer PCM or normalized float32 WAV, at most 64 MiB")
    acoustic.add_argument("output", type=Path, help="New directory; writes mel.f32le then target.json; no overwrite/resume")
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
    segment.add_argument("source_path", type=Path, help="Captured mono integer PCM or normalized float32 WAV, maximum 64 MiB")
    segment.add_argument("output", type=Path, help="New directory containing audio.wav and final segment.json identity record")
    segment.add_argument("--resume", action="store_true", help="Verify exact existing audio; finish a missing record or verify complete output; never overwrite")
    segment.add_argument("--fresh-pitch-extractor", type=Path, help="Trusted native CLI; re-extract on the exact clip, including off-grid starts")
    batch = commands.add_parser("segment-batch", help="Process 1..64 captured phrase configs; retain per-entry failures; no approval.")
    batch.add_argument("configuration", type=Path)
    batch.add_argument("configuration_sha256")
    batch.add_argument("source_root", type=Path, help="Stable directory of flat source/config filenames; max 64 MiB per source read")
    batch.add_argument("output", type=Path, help="Private clip directory; existing directory requires --resume")
    batch.add_argument("report", type=Path, help="New report for this attempt, never overwritten; exit 3 means entry failures")
    batch.add_argument("--resume", action="store_true")
    batch.add_argument("--fresh-pitch-extractor", type=Path, help="Trusted native CLI; each entry must contain labels")
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
    admit.add_argument("--segment-config", type=Path, help="Optional captured crop configuration; requires all segment options")
    admit.add_argument("--segment-sha256")
    admit.add_argument("--segment-source", type=Path)
    admit.add_argument("--segment-output", type=Path, help="Clip directory distinct from the admission report")
    admit.add_argument("--resume-segment", action="store_true", help="Verify/recover exact existing clip; admission report must still be new")
    admit.add_argument("--fresh-pitch-extractor", type=Path, help="Trusted native CLI for derived clips only")
    refresh = commands.add_parser("refresh-pitch", help="Run trusted native extractor and publish unreviewed refreshed labels; POSIX only.")
    refresh.add_argument("configuration", type=Path, help="Captured JSON, at most 8 MiB")
    refresh.add_argument("configuration_sha256")
    refresh.add_argument("source_root", type=Path)
    refresh.add_argument("extractor", type=Path, help="Trusted first-party seam_voicebank_cli; 30-second deadline")
    refresh.add_argument("output", type=Path, help="New report with feature evidence and labels; never overwritten")
    correct = commands.add_parser("correct-labels", help="Apply captured stale-checked label edits; creates no review approval.")
    correct.add_argument("configuration", type=Path, help="Captured edit config, maximum 8 MiB")
    correct.add_argument("configuration_sha256")
    correct.add_argument("source_root", type=Path)
    correct.add_argument("output", type=Path, help="New corrected label/audit report; never overwritten")
    admit_label = commands.add_parser("admit-labels", help="Verify signed musical annotation review against actual source; no source-rights or training approval.")
    admit_label.add_argument("configuration", type=Path, help="Captured schema 3 label configuration, maximum 8 MiB")
    admit_label.add_argument("configuration_sha256")
    admit_label.add_argument("source_root", type=Path)
    admit_label.add_argument("output", type=Path, help="New time-bound label admission report; never overwritten")
    admit_label.add_argument("--review", type=Path, required=True)
    admit_label.add_argument("--review-sha256", required=True)
    admit_label.add_argument("--policy", type=Path, required=True)
    admit_label.add_argument("--policy-file-sha256", required=True)
    admit_label.add_argument("--trusted-policy-sha256", required=True, help="Independent canonical label-review policy hash")
    dataset = commands.add_parser("assemble-dataset", help="Revalidate rights/labels and assemble a split snapshot; never starts training.")
    dataset.add_argument("configuration", type=Path, help="Captured assembly JSON; maximum 8 MiB per referenced JSON")
    dataset.add_argument("configuration_sha256")
    dataset.add_argument("source_root", type=Path)
    dataset.add_argument("output", type=Path, help="New snapshot; exit 3 retains preparation issues; no overwrite")
    dataset.add_argument("--rights-policy-sha256", required=True, help="Independent canonical rights policy trust anchor")
    dataset.add_argument("--label-policy-sha256", required=True, help="Independent canonical annotation policy trust anchor")
    dataset.add_argument("--conditioning-directory", type=Path,
                         help="New sibling directory for phrase shards; 1M total frames, 256 MiB; incomplete attempts retained")
    dataset.add_argument("--reuse-conditioning", action="store_true",
                         help="Read-only verification of existing shards against freshly admitted labels; never repairs files")
    qualify = commands.add_parser("qualify-candidate",
        help="Run a candidate on held-out items and publish a dossier; never records musical approval.")
    qualify.add_argument("configuration", type=Path,
        help="Captured qualification JSON: bundle identity, 1..256 held-out items with phones and frame counts, "
             "2..5 repetitions and optional per-item millisecond budget; maximum 8 MiB")
    qualify.add_argument("configuration_sha256")
    qualify.add_argument("worker", type=Path,
        help="Production worker executable; hashed into the dossier, never copied")
    qualify.add_argument("output", type=Path,
        help="New dossier JSON; exit 4 when a criterion fails; never overwritten")
    teacher = commands.add_parser("generated-teacher-labels",
        help="Translate captured procedural-teacher exports into an admitted label configuration; "
             "creates no permission, label admission or training approval.")
    teacher.add_argument("configuration", type=Path,
        help="Captured translation JSON: sample rate, vocabulary, confidence, and 1..10000 exports "
             "with their source-root-relative audio paths; maximum 8 MiB")
    teacher.add_argument("configuration_sha256")
    teacher.add_argument("source_root", type=Path,
        help="Root the export audio will be prepared from; inspected by the caller, not modified here")
    teacher.add_argument("output", type=Path,
        help="New label configuration JSON; never overwritten; still requires rights and annotation review")
    args = parser.parse_args()
    try:
        if args.command == "acoustic-targets":
            acoustic_targets_command(args.configuration, args.configuration_sha256, args.source, args.output)
            return 0
        if args.command == "assemble-dataset":
            return assemble_dataset_command(args.configuration, args.configuration_sha256, args.source_root, args.output,
                                            rights_anchor=args.rights_policy_sha256, label_anchor=args.label_policy_sha256,
                                            conditioning_directory=args.conditioning_directory, reuse_conditioning=args.reuse_conditioning)
        if args.command == "qualify-candidate":
            return qualify_command(args.configuration, args.configuration_sha256, args.worker, args.output)
        if args.command == "generated-teacher-labels":
            value = load_config(args.configuration, args.configuration_sha256)
            fields = {"formatId", "schemaVersion", "sampleRate", "vocabulary",
                      "minimumConfidence", "exports", "relativePaths"}
            if (not isinstance(value, dict) or set(value) != fields
                    or value["formatId"] != "com.project-seam.voice-training-generated-teacher-config"
                    or type(value["schemaVersion"]) is not int or value["schemaVersion"] != 1):
                raise ValueError("Unsupported generated-teacher configuration")
            if not args.source_root.is_dir():
                raise ValueError("Generated-teacher source root must be an existing directory")
            config = label_config_from_exports(exports=value["exports"],
                sample_rate=value["sampleRate"], relative_paths=value["relativePaths"],
                vocabulary=value["vocabulary"], minimum_confidence=value["minimumConfidence"])
            publish_new(args.output, config)
            return 0
        if args.command == "admit-labels":
            if args.output.exists() or args.output.is_symlink():
                raise ValueError("Label admission report must be new")
            result = admit_labels(args.configuration, args.configuration_sha256, args.source_root,
                review=load_config(args.review, args.review_sha256), policy=load_config(args.policy, args.policy_file_sha256),
                trusted_policy_sha256=args.trusted_policy_sha256, now=int(time.time()))
            if int(time.time()) >= result["expiresAt"]:
                raise ValueError("Label review expired during inspection")
            publish_new(args.output, result)
            return 0
        if args.command == "correct-labels":
            correct_labels_command(args.configuration, args.configuration_sha256, args.source_root, args.output)
            return 0
        if args.command == "refresh-pitch":
            refresh_pitch_command(args.configuration, args.configuration_sha256, args.source_root, args.extractor, args.output)
            return 0
        if args.command == "admit":
            segment_options = (args.segment_config, args.segment_sha256, args.segment_source, args.segment_output)
            if any(v is not None for v in segment_options) and not all(v is not None for v in segment_options):
                raise ValueError("Derived admission requires all four segment options")
            if args.resume_segment and args.segment_config is None:
                raise ValueError("Segment resume requires a derived admission")
            if args.fresh_pitch_extractor is not None and args.segment_config is None:
                raise ValueError("Fresh pitch extraction requires derived admission")
            if args.output.exists() or args.output.is_symlink():
                raise ValueError("Admission report must be new")
            review = load_config(args.review, args.review_sha256)
            policy = load_config(args.policy, args.policy_file_sha256)
            if args.segment_config is not None:
                if args.output.resolve().is_relative_to(args.segment_output.resolve()):
                    raise ValueError("Admission report must be outside the clip directory")
                result = admit_segment(args.configuration, args.configuration_sha256, args.source_root,
                    segment_config=args.segment_config, segment_hash=args.segment_sha256,
                    source_path=args.segment_source, output=args.segment_output, resume=args.resume_segment,
                    fresh_pitch_extractor=args.fresh_pitch_extractor,
                    review=review, policy=policy, trusted_policy_sha256=args.trusted_policy_sha256, now=int(time.time()))
            else:
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
                                         args.output, args.report, resume=args.resume, fresh_pitch_extractor=args.fresh_pitch_extractor)
        if args.command == "segment":
            segment_command(args.configuration, args.configuration_sha256, args.source_path, args.output, resume=args.resume,
                            fresh_pitch_extractor=args.fresh_pitch_extractor)
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
