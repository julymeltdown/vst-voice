"""Select the native target supplied by CTest; manual discovery keeps its fallback."""
from __future__ import annotations

import os
from pathlib import Path


def voicebank_cli(repository_root: Path) -> Path:
    configured = os.environ.get("SEAM_VOICEBANK_CLI")
    if configured is None:
        return repository_root / "build/release/seam_voicebank_cli"
    binary = Path(configured)
    if not binary.is_file():
        raise FileNotFoundError(f"Configured SEAM_VOICEBANK_CLI is not a file: {configured!r}")
    return binary
