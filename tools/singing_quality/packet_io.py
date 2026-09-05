from __future__ import annotations

import hashlib
import os
from pathlib import Path
import stat
from typing import Final

from .contract_types import CorpusError, relative_path

MAXIMUM_ASSET_BYTES: Final = 32 * 1024 * 1024
MAXIMUM_PACKET_INPUT_BYTES: Final = 128 * 1024 * 1024


def inspect_path(root: Path, relative: str) -> Path:
    current = root
    for part in relative_path(relative).split("/"):
        current = current / part
        if current.is_symlink():
            raise CorpusError("asset_symlink", relative)
    return current


def read_bounded(path: Path) -> bytes:
    try:
        before = path.lstat()
        if stat.S_ISLNK(before.st_mode):
            raise CorpusError("asset_symlink", str(path))
        if not stat.S_ISREG(before.st_mode) or before.st_size > MAXIMUM_ASSET_BYTES:
            raise CorpusError("asset_size", str(path))
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
        with os.fdopen(os.open(path, flags), "rb") as stream:
            opened = os.fstat(stream.fileno())
            if (opened.st_dev, opened.st_ino) != (before.st_dev, before.st_ino):
                raise CorpusError("asset_changed", str(path))
            payload = stream.read(MAXIMUM_ASSET_BYTES + 1)
        if len(payload) > MAXIMUM_ASSET_BYTES:
            raise CorpusError("asset_size", str(path))
        return payload
    except FileNotFoundError as error:
        raise CorpusError("asset_missing", str(path)) from error
    except OSError as error:
        raise CorpusError("asset_io", str(error)) from error


def digest_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def write_new(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(payload)
