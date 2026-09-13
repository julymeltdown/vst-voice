"""Read-only preparation inspection; no transforms or permission approval."""
import os
from pathlib import Path
import stat
from .audio_source import inspect_pcm_source


def prepare_sources(root: Path, records: list[dict], *, sample_rate: int) -> dict:
    root = root.resolve(strict=True)
    if not root.is_dir() or not isinstance(records, list) or not 1 <= len(records) <= 10000:
        raise ValueError("Preparation requires a source directory and 1..10000 records")
    fields = {"sourceId", "songId", "sessionId", "lineageId", "path", "sourceSha256"}
    seen = set()
    for row in records:
        if not isinstance(row, dict) or set(row) != fields:
            raise ValueError("Invalid preparation source fields")
        if any(not isinstance(value, str) or not value or len(value.encode()) > 4096
               or any(ord(c) < 32 or ord(c) == 127 for c in value) for value in row.values()):
            raise ValueError("Invalid preparation source text")
        if row["sourceId"] in seen:
            raise ValueError("Duplicate preparation source ID")
        if any(len(row[key].encode()) > 256 for key in ("sourceId", "songId", "sessionId", "lineageId")):
            raise ValueError("Preparation identity exceeds split inventory bounds")
        seen.add(row["sourceId"])
    items, split_records = [], []
    remaining = 512 * 1024 * 1024
    for row in records:
        try:
            parts = row["path"].split("/")
            if "\\" in row["path"] or ":" in row["path"] or any(part in ("", ".", "..") for part in parts):
                raise ValueError("Source path must be contained and canonical")
            path = root
            for part in parts:
                path /= part
                if path.is_symlink():
                    raise ValueError("Source symlinks are unsupported")
            if not path.resolve(strict=True).is_relative_to(root):
                raise ValueError("Source path escapes its root")
            flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
            with os.fdopen(os.open(path, flags), "rb") as stream:
                before = os.fstat(stream.fileno())
                if not stat.S_ISREG(before.st_mode) or not 0 < before.st_size <= min(64 * 1024 * 1024, remaining):
                    raise ValueError("Source file exceeds remaining preparation budget")
                payload = stream.read(min(64 * 1024 * 1024, remaining) + 1)
                remaining -= len(payload)
                after = os.fstat(stream.fileno())
                if len(payload) != before.st_size or (before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (after.st_size, after.st_mtime_ns, after.st_ctime_ns):
                    raise ValueError("Source changed during preparation")
            report = inspect_pcm_source(payload, expected_sha256=row["sourceSha256"], sample_rate=sample_rate)
            items.append(dict(sourceId=row["sourceId"], status="INSPECTED_ONLY", inspection=report))
            split_records.append({**{key: row[key] for key in ("sourceId", "songId", "sessionId", "lineageId")},
                                  "audioSha256": report["audioSha256"]})
        except (ValueError, OSError) as error:
            items.append(dict(sourceId=row["sourceId"], status="REJECTED", diagnostic=str(error)[:256]))
    rejected = sum(item["status"] == "REJECTED" for item in items)
    return dict(formatId="com.project-seam.training-source-preparation", schemaVersion=1,
                sources=items, rejectedCount=rejected, splitSources=split_records if not rejected else [],
                sourceRightsAdmitted=False, releaseEligible=False)
