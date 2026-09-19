"""Explicit macOS copy-on-write capture; never fall back to hard links or copies."""
import ctypes
import hashlib
import os
import re
from pathlib import Path
import stat
import sys


def clone_verified_file(source, destination, digest, maximum_bytes=64 * 1024**2):
    if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise ValueError("Clone digest must be a SHA-256 identity")
    if type(maximum_bytes) is not int or not 0 < maximum_bytes <= 64 * 1024**2:
        raise ValueError("Clone byte limit must be positive and bounded")
    if sys.platform != "darwin":
        raise ValueError("Copy-on-write captures require macOS clonefile support")
    source, destination = Path(source), Path(destination)
    if destination.exists() or destination.is_symlink():
        raise ValueError("Cloned capture must have a new destination")
    if source.is_symlink() or not source.is_file():
        raise ValueError("Clone source must be a regular non-symlink file")
    with os.fdopen(os.open(source, os.O_RDONLY | os.O_NOFOLLOW), "rb") as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or not 0 < info.st_size <= maximum_bytes:
            raise ValueError("Clone source exceeds capture bounds")
        parent = os.open(destination.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            library = ctypes.CDLL(None, use_errno=True)
            clone = library.fclonefileat
            clone.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
            clone.restype = ctypes.c_int
            if clone(stream.fileno(), parent, os.fsencode(destination.name), 0) != 0:
                error = ctypes.get_errno()
                raise OSError(error, os.strerror(error))
        finally:
            os.close(parent)
    with os.fdopen(os.open(destination, os.O_RDONLY | os.O_NOFOLLOW), "rb") as stream:
        cloned = os.fstat(stream.fileno())
        payload = stream.read(maximum_bytes + 1)
        os.fsync(stream.fileno())
    if ((cloned.st_dev, cloned.st_ino) == (info.st_dev, info.st_ino)
            or len(payload) > maximum_bytes or hashlib.sha256(payload).hexdigest() != digest):
        raise ValueError("Cloned capture identity differs; incomplete output retained")
