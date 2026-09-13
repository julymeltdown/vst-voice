"""Deterministic pre-augmentation source splitting; not source-rights admission."""
import hashlib
import json


def split_sources(records: list[dict], *, seed: str, held_out_songs: list[str]) -> dict:
    """Keep connected songs/sessions/lineages/exact audio duplicates together.

    Inputs must come from a separately admitted source manifest. All identifiers
    are dataset-global. This function neither reads audio nor approves rights.
    """
    if not isinstance(records, list) or not 1 <= len(records) <= 100000:
        raise ValueError("Source count must be between 1 and 100000")
    def text(value):
        return (isinstance(value, str) and 0 < len(value.encode("utf-8")) <= 256
                and not any(ord(character) < 32 or ord(character) == 127 for character in value))
    if not text(seed) or not isinstance(held_out_songs, list) or not held_out_songs:
        raise ValueError("A seed and explicit held-out songs are required")
    if not all(text(song) for song in held_out_songs) or len(set(held_out_songs)) != len(held_out_songs):
        raise ValueError("Held-out song identifiers must be unique")
    held_out = set(held_out_songs)
    required = {"sourceId", "songId", "sessionId", "lineageId", "audioSha256"}
    by_id = {}
    for record in records:
        if not isinstance(record, dict) or set(record) != required or not all(text(record[key]) for key in required):
            raise ValueError("Source record fields are invalid")
        digest = record["audioSha256"]
        if len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            raise ValueError("Audio digest must be canonical SHA-256")
        if record["sourceId"] in by_id:
            raise ValueError("Duplicate source identifier")
        by_id[record["sourceId"]] = dict(record)
    if not held_out <= {record["songId"] for record in records}:
        raise ValueError("Held-out song is absent from sources")
    parents = {identifier: identifier for identifier in by_id}
    def root(identifier):
        while parents[identifier] != identifier:
            parents[identifier] = parents[parents[identifier]]
            identifier = parents[identifier]
        return identifier
    seen = {}
    for identifier in sorted(by_id):
        for field in ("songId", "sessionId", "lineageId", "audioSha256"):
            key = (field, by_id[identifier][field])
            if key in seen:
                left, right = root(identifier), root(seen[key])
                parents[max(left, right)] = min(left, right)
            else:
                seen[key] = identifier
    groups = {}
    for identifier in sorted(by_id):
        groups.setdefault(root(identifier), []).append(identifier)
    result = []
    for members in groups.values():
        identity = hashlib.sha256(json.dumps(members, separators=(",", ":")).encode()).hexdigest()
        forced = any(by_id[item]["songId"] in held_out for item in members)
        bucket = int(hashlib.sha256((seed + ":" + identity).encode()).hexdigest(), 16) % 100
        partition = "test" if forced or bucket >= 90 else "validation" if bucket >= 80 else "train"
        result.append(dict(groupId=identity, partition=partition, explicitHeldOut=forced, sourceIds=members))
    canonical_sources = [by_id[key] for key in sorted(by_id)]
    source_hash = hashlib.sha256(json.dumps(canonical_sources, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    counts = {partition: sum(len(group["sourceIds"]) for group in result if group["partition"] == partition)
              for partition in ("train", "validation", "test")}
    partitions = {identifier: group["partition"] for group in result for identifier in group["sourceIds"]}
    audio_groups = {}
    for identifier in sorted(by_id):
        audio_groups.setdefault(by_id[identifier]["audioSha256"], []).append(identifier)
    duplicates = []
    unique_counts = dict.fromkeys(("train", "validation", "test"), 0)
    for digest, members in sorted(audio_groups.items()):
        owners = {partitions[identifier] for identifier in members}
        if len(owners) != 1:
            raise ValueError("Internal split invariant: duplicate audio crossed partitions")
        partition = owners.pop()
        unique_counts[partition] += 1
        if len(members) > 1:
            duplicates.append(dict(audioSha256=digest, sourceIds=members, partition=partition))
    return dict(formatId="com.project-seam.voice-training-split", schemaVersion=2,
                seed=seed, sourceInventoryHash=source_hash, heldOutSongIds=sorted(held_out_songs),
                groups=sorted(result, key=lambda group: group["groupId"]), counts=counts,
                missingPartitions=[key for key, value in counts.items() if not value],
                uniqueAudioCounts=unique_counts, duplicateAudioGroups=duplicates,
                redundantSourceCount=len(records)-len(audio_groups),
                duplicateSelectionPolicy="review-required-no-automatic-removal",
                sourceRightsAdmitted=False, releaseEligible=False)
