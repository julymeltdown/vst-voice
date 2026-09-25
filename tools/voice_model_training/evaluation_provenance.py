"""Dependency-light identity capture for offline model evaluation receipts."""

import hashlib
import importlib.metadata
import os
import platform
from pathlib import Path
import stat


MAX_PITCH_EXECUTABLE_BYTES = 512 * 1024 * 1024
_HASH_CHUNK_BYTES = 1024 * 1024


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _hash_pitch_executable(path: Path) -> tuple[str, int]:
    """Hash one bounded, stable regular file without loading it all into memory."""
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    descriptor = os.open(path, flags)
    try:
        before = os.fstat(descriptor)
        if (not stat.S_ISREG(before.st_mode) or before.st_size < 0
                or before.st_size > MAX_PITCH_EXECUTABLE_BYTES):
            raise ValueError("Pitch executable is not a bounded regular file")
        digest = hashlib.sha256()
        size = 0
        while True:
            block = os.read(descriptor, _HASH_CHUNK_BYTES)
            if not block:
                break
            size += len(block)
            if size > MAX_PITCH_EXECUTABLE_BYTES:
                raise ValueError("Pitch executable changed beyond its size limit")
            digest.update(block)
        after = os.fstat(descriptor)
        identity = lambda value: (value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns)
        if identity(before) != identity(after) or size != before.st_size:
            raise ValueError("Pitch executable changed during provenance capture")
        return digest.hexdigest(), size
    finally:
        os.close(descriptor)


def capture_evaluation_provenance(evaluator_path: Path,
                                  pitch_executable: Path) -> dict:
    """Bind a new receipt to evaluator/pitch bytes and numerical runtime."""
    pitch_path = pitch_executable.resolve(strict=True)
    if not pitch_path.is_file():
        raise ValueError("Pitch executable must resolve to a regular file")
    pitch_sha256, pitch_size = _hash_pitch_executable(pitch_path)

    def package_version(name):
        try:
            return importlib.metadata.version(name)
        except importlib.metadata.PackageNotFoundError:
            return None

    return {
        "evaluator": {
            "sha256": _sha(evaluator_path.read_bytes()),
        },
        "provenanceHelper": {
            "module": "tools.voice_model_training.evaluation_provenance",
            "sha256": _sha(Path(__file__).read_bytes()),
        },
        "pitchExecutable": {
            "sha256": pitch_sha256,
            "sizeBytes": pitch_size,
        },
        "runtime": {
            "pythonImplementation": platform.python_implementation(),
            "pythonVersion": platform.python_version(),
            "platform": platform.platform(),
            "numpy": package_version("numpy"),
            "scipy": package_version("scipy"),
            "onnxruntime": package_version("onnxruntime"),
        },
    }
