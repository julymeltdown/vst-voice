from __future__ import annotations

from pathlib import Path
from typing import Any

from common import MAX_EVIDENCE_BYTES, load_json, validate_evidence


def validate_evidence_record(
    record: dict[str, Any],
    root: Path,
    *,
    maximum_bytes: int = MAX_EVIDENCE_BYTES,
) -> list[str]:
    return validate_evidence(record, Path(root), maximum_bytes=maximum_bytes)


def verify_evidence(root: Path, item: dict[str, Any]) -> list[str]:
    return validate_evidence_record(item, root)
